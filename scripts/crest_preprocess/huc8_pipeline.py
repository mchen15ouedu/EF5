#!/usr/bin/env python3
"""HUC8 tiling + mosaic pipeline for EF5/CREST forcing.

Deployment model:
  1) tile_huc8(): clip CONUS forcing to a HUC8's bbox per timestep -> per-HUC8
     PQF tiles (the reusable "split" library). Built only for HUC8s that are
     needed; idempotent so repeated basins reuse tiles.
  2) mosaic_basin(): for a user-defined basin, find the intersecting HUC8 tiles,
     mosaic them onto the basin window per timestep, write EF5-ready PQF named
     for the model. EF5 reads these directly (type=pqf).

All forcings share their native CONUS grid, so tiles are grid-aligned and the
mosaic is an exact index placement (no resampling, no value change). Validated
to reproduce the CONUS-direct EF5 result.

  prepare_basin(W,S,E,N, start, end, var, tile_root, out_dir)  # end-to-end
"""
import os, glob, datetime as dt
import numpy as np, rasterio, geopandas as gpd
from rasterio.windows import from_bounds
import pyarrow as pa, pyarrow.parquet as pq

HUC8_GDB = '/ourdisk/hpc/caps/mchen15/USGS_gauges/HUC/wbdhu8_a_us_september2022.gdb'

# forcing source config: dir, filename strftime pattern, frequency
SOURCES = {
    'mrms': dict(dir='/ourdisk/hpc/caps/mchen15/MRMS15_24/mrms_15_24',
                 pat='mrms_corr_%Y%m%d%H.tif', freq='h'),
    'temp': dict(dir='/ourdisk/hpc/caps/mchen15/nldas_tif/temp',
                 pat='NLDAS_FORA0125_H.A%Y%m%d.%H00.002.grb.SUB_T.tif', freq='h'),
    'pet':  dict(dir='/ourdisk/hpc/caps/mchen15/PET01_20',
                 pat='et%Y%m%d.bil.tif', freq='d'),
}

_HUC = None
def load_huc8():
    global _HUC
    if _HUC is None:
        _HUC = gpd.read_file(HUC8_GDB, layer='WBDHU8').to_crs('EPSG:4326')
    return _HUC

def huc8s_for_basin(W, S, E, N):
    """HUC8 rows whose polygon intersects the basin extent."""
    from shapely.geometry import box
    g = load_huc8()
    return g[g.intersects(box(W, S, E, N))][['huc8', 'name', 'geometry']].copy()

def _timesteps(start, end, freq):
    step = dt.timedelta(hours=1) if freq == 'h' else dt.timedelta(days=1)
    t = start; out = []
    while t <= end:
        out.append(t); t += step
    return out

def _write_pqf(path, a, xll, yll, cell, nodata):
    nr, nc = a.shape
    meta = {b'ncols': str(nc).encode(), b'nrows': str(nr).encode(),
            b'xllcorner': repr(float(xll)).encode(), b'yllcorner': repr(float(yll)).encode(),
            b'cellsize': repr(float(cell)).encode(), b'nodata': repr(float(nodata)).encode()}
    schema = pa.schema([pa.field('v', pa.float32())]).with_metadata(meta)
    pq.write_table(pa.table({'v': a.reshape(-1).astype('float32')}, schema=schema),
                   path, compression='zstd')

def _read_pqf(path):
    t = pq.read_table(path); m = t.schema.metadata
    g = lambda k: m[k.encode()].decode()
    nc, nr = int(g('ncols')), int(g('nrows'))
    a = t.column('v').to_numpy().reshape(nr, nc)
    return a, float(g('xllcorner')), float(g('yllcorner')), float(g('cellsize')), float(g('nodata'))

def _clip_conus(var, t, W, S, E, N):
    """Clip the CONUS source for `var` at time `t` to [W,S,E,N]; snapped to the
    source grid. Returns (array, xll, yll, cell, nodata) or None if missing."""
    src = SOURCES[var]
    f = os.path.join(src['dir'], t.strftime(src['pat']))
    if not os.path.exists(f):
        return None
    with rasterio.open(f) as s:
        win = from_bounds(W, S, E, N, s.transform)
        # snap to whole cells
        win = win.round_offsets().round_lengths()
        a = s.read(1, window=win)
        tr = s.window_transform(win)
        nodata = s.nodata if s.nodata is not None else -9999.0
    xll = tr.c; yll = tr.f + a.shape[0] * tr.e; cell = tr.a
    return a.astype('float32'), xll, yll, cell, nodata

def tile_huc8(huc8_code, start, end, var, tile_root, halo=0.05):
    """Build per-HUC8 PQF tiles for var over [start,end]. Idempotent."""
    g = load_huc8(); row = g[g['huc8'] == huc8_code]
    if row.empty:
        raise ValueError(f'unknown HUC8 {huc8_code}')
    W, S, E, N = row.iloc[0].geometry.bounds
    W, S, E, N = W - halo, S - halo, E + halo, N + halo
    src = SOURCES[var]
    outdir = os.path.join(tile_root, var, huc8_code); os.makedirs(outdir, exist_ok=True)
    n = 0
    for t in _timesteps(start, end, src['freq']):
        out = os.path.join(outdir, t.strftime(src['pat']).rsplit('.', 1)[0] + '.pqf')
        if os.path.exists(out) and os.path.getsize(out) > 0:
            continue
        clip = _clip_conus(var, t, W, S, E, N)
        if clip is None:
            continue
        _write_pqf(out, *clip); n += 1
    return n

def mosaic_basin(W, S, E, N, start, end, var, tile_root, out_dir, halo=0.03):
    """Mosaic the intersecting HUC8 tiles onto the basin window per timestep and
    write EF5-ready PQF (named exactly like the CONUS source, .pqf)."""
    src = SOURCES[var]
    hucs = huc8s_for_basin(W, S, E, N)['huc8'].tolist()
    os.makedirs(out_dir, exist_ok=True)
    bw, bs, be, bn = W - halo, S - halo, E + halo, N + halo
    n = 0
    for t in _timesteps(start, end, src['freq']):
        name = t.strftime(src['pat']).rsplit('.', 1)[0] + '.pqf'
        out = os.path.join(out_dir, name)
        if os.path.exists(out) and os.path.getsize(out) > 0:
            n += 1; continue
        # collect tiles for this timestep
        tiles = []
        for h in hucs:
            p = os.path.join(tile_root, var, h, name)
            if os.path.exists(p):
                tiles.append(_read_pqf(p))
        if not tiles:
            continue
        cell = tiles[0][3]; nodata = tiles[0][4]
        # basin window snapped to the (shared) grid using the first tile's origin
        xll0, yll0 = tiles[0][1], tiles[0][2]
        c0 = int(round((bw - xll0) / cell)); c1 = int(round((be - xll0) / cell))
        # rows measured from north; build global top from a tile
        top0 = yll0 + tiles[0][0].shape[0] * cell
        r0 = int(round((top0 - bn) / cell)); r1 = int(round((top0 - bs) / cell))
        nc = max(1, c1 - c0); nr = max(1, r1 - r0)
        out_a = np.full((nr, nc), nodata, dtype='float32')
        for a, xll, yll, cl, nd in tiles:
            top = yll + a.shape[0] * cl
            tc0 = int(round((xll - xll0) / cell)); tr0 = int(round((top0 - top) / cell))
            for rr in range(a.shape[0]):
                gr = tr0 + rr - r0
                if gr < 0 or gr >= nr: continue
                gc0 = tc0 - c0
                for cc in range(a.shape[1]):
                    gc = gc0 + cc
                    if 0 <= gc < nc and a[rr, cc] != nd:
                        out_a[gr, gc] = a[rr, cc]
        out_xll = xll0 + c0 * cell; out_yll = top0 - r1 * cell
        _write_pqf(out, out_a, out_xll, out_yll, cell, nodata); n += 1
    return n

def prepare_basin(W, S, E, N, start, end, var, tile_root, out_dir):
    """End-to-end: ensure HUC8 tiles exist for the basin, then mosaic to PQF."""
    hucs = huc8s_for_basin(W, S, E, N)
    for h in hucs['huc8']:
        tile_huc8(h, start, end, var, tile_root)
    return mosaic_basin(W, S, E, N, start, end, var, tile_root, out_dir)

if __name__ == '__main__':
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument('--bbox', nargs=4, type=float, metavar=('W', 'S', 'E', 'N'), required=True)
    ap.add_argument('--start', required=True); ap.add_argument('--end', required=True)
    ap.add_argument('--var', default='mrms', choices=list(SOURCES))
    ap.add_argument('--tile-root', default='/scratch/mchen15/huc8_tiles')
    ap.add_argument('--out', required=True)
    a = ap.parse_args()
    fmt = '%Y%m%d%H' if SOURCES[a.var]['freq'] == 'h' else '%Y%m%d'
    s = dt.datetime.strptime(a.start, fmt); e = dt.datetime.strptime(a.end, fmt)
    n = prepare_basin(*a.bbox, s, e, a.var, a.tile_root, a.out)
    print(f'wrote {n} {a.var} PQF timesteps to {a.out}')
