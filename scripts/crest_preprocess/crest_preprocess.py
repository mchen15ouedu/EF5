#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
CREST input data pre-process
============================================================================
Convert (almost) any geospatial / weather raster into EF5's fastest native
on-disk format -- **BIF** (Binary Interchange Format) -- so the model reads
forcing and parameter grids with a single raw ``fread`` per row instead of the
slow libTIFF scan-line path used for GeoTIFF.

Why BIF?
--------
EF5's grid readers (``src/BifGrid.cpp``) load a tiny fixed header followed by
row-major ``float32`` data with no decompression and no parsing -- this is the
fastest reader in the code base.  After the accompanying C++ patch, EF5 reads
``.bif`` for **all** grid roles:

    * precipitation   (PRECIP_BIF -- already supported upstream)
    * PET             (PET_BIF    -- already supported upstream)
    * temperature     (TEMP_BIF   -- added by the patch)
    * DEM / DDM / FAM (added by the patch -- extension dispatch in BasicGrids)

BIF on-disk layout (must match ``src/BifGrid.h`` exactly)
---------------------------------------------------------
Header is ``#pragma pack(1)`` (no padding), 50 bytes total::

    int32   ncols
    int32   nrows
    float32 xllcorner   (lower-left CORNER, map units -- west edge)
    float32 yllcorner   (lower-left CORNER, map units -- south edge)
    float32 cellsize    (square cells; x-size == y-size)
    float32 nodata
    char[26] padding

Immediately followed by ``nrows * ncols`` ``float32`` values, **row 0 = NORTH**
(top of the raster), written west->east within each row.  EF5 fills the rest of
the bounding box as ``top = yll + nrows*cellsize`` and indexes ``data[y][x]``
with ``y`` increasing southward (see ``Grid::GetRefLoc`` / ``GetGridLoc``), so the
north-up row order is mandatory -- writing south-up silently flips your data.

Endianness: EF5 uses raw ``fread`` so the bytes are interpreted in the *native*
byte order of the machine that runs EF5.  We default to little-endian (x86-64,
Windows & Linux).  Use ``--big-endian`` only if EF5 runs on a big-endian target.

Format coverage
---------------
Everything GDAL can open is supported (155+ raster drivers): GeoTIFF/COG,
GRIB1/GRIB2, netCDF/netCDF4, HDF4/HDF5/HDF-EOS, Zarr, ENVI BIL/BSQ/BIP (.bil),
Arc/Info ASCII (.asc) & Binary Grid (.adf), ERDAS IMAGINE (.img), JPEG2000,
SRTM .hgt, DTED, USGS DEM, VRT, PCRaster, ... -- opened via ``rasterio``.
Multi-dimensional scientific files (a variable plus time / level / ensemble
dimensions) are opened via ``xarray`` so you can pick the variable and timestep:
netCDF through ``netCDF4``/``h5netcdf``, GRIB through ``cfgrib``, Zarr through
``zarr``.

Two ways to use it
------------------
1. Command line -- see ``python crest_preprocess.py --help`` and the examples
   at the bottom of this file / in README.md.
2. Import the functions: ``convert``, ``convert_timeseries``, ``write_bif``,
   ``read_bif``, ``read_reference_grid``, ``load_raster``, ``align_to_reference``.

Dependencies: numpy, rasterio (required); xarray + (netCDF4 or h5netcdf),
cfgrib, rioxarray, zarr (optional -- only for the scientific multi-dim path).
"""

from __future__ import annotations

import argparse
import os
import struct
import sys
import warnings

import numpy as np

# ---------------------------------------------------------------------------
# Optional dependencies are imported lazily so the BIF core works with only
# numpy installed, and so missing-extra errors are actionable.
# ---------------------------------------------------------------------------
try:
    import rasterio
    from rasterio.warp import reproject, Resampling, calculate_default_transform
    from rasterio.transform import Affine, array_bounds
    _HAVE_RASTERIO = True
except Exception:  # pragma: no cover - exercised only when rasterio absent
    _HAVE_RASTERIO = False


# ===========================================================================
# BIF format constants -- DO NOT CHANGE without matching src/BifGrid.h
# ===========================================================================
# '<' = little-endian, no struct padding (mirrors #pragma pack(1)).
# i i  = ncols, nrows ; f f f f = xll, yll, cellsize, nodata ; 26s = padding.
_BIF_HEADER_FMT_LE = "<iiffff26s"
_BIF_HEADER_FMT_BE = ">iiffff26s"
_BIF_HEADER_SIZE = 50  # 4+4+4+4+4+4+26
assert struct.calcsize(_BIF_HEADER_FMT_LE) == _BIF_HEADER_SIZE

DEFAULT_NODATA = -9999.0

# Roles whose data is categorical and must NEVER be bilinearly interpolated.
# Flow-direction codes and (ideally) flow-accumulation counts only make sense
# under nearest-neighbour resampling.
CATEGORICAL_ROLES = {"ddm", "fam", "flowdir", "flowacc"}


class GridSpec(object):
    """A target grid definition (the EF5 model grid, usually the DEM).

    Attributes are kept deliberately close to EF5's own grid model so the
    mapping to the BIF header is unambiguous.
    """

    __slots__ = ("width", "height", "transform", "crs", "cellsize",
                 "xll", "yll", "nodata")

    def __init__(self, width, height, transform, crs, nodata=DEFAULT_NODATA):
        self.width = int(width)
        self.height = int(height)
        self.transform = transform          # rasterio Affine, north-up (e < 0)
        self.crs = crs
        self.cellsize = float(transform.a)  # square cells enforced elsewhere
        self.xll = float(transform.c)                       # west edge
        self.yll = float(transform.f) + self.height * float(transform.e)  # south edge
        self.nodata = float(nodata)

    @property
    def top(self):
        return float(self.transform.f)

    def __repr__(self):
        return ("GridSpec(width=%d, height=%d, cellsize=%.8g, xll=%.8g, "
                "yll=%.8g, crs=%s)" % (self.width, self.height, self.cellsize,
                                       self.xll, self.yll, self.crs))


# ===========================================================================
# Core BIF read / write -- only numpy required
# ===========================================================================
def write_bif(path, data, xll, yll, cellsize, nodata=DEFAULT_NODATA,
              big_endian=False):
    """Write a north-up 2D float array to an EF5 BIF file.

    Parameters
    ----------
    path : str
        Output ``.bif`` path.
    data : array-like, shape (nrows, ncols)
        Grid values with **row 0 = north** (top).  NaNs are replaced by
        ``nodata`` so EF5's ``value != noData`` test works.
    xll, yll : float
        Lower-left CORNER coordinates (west / south edges), map units.
    cellsize : float
        Square cell size in map units.
    nodata : float
        No-data sentinel written into the header (EF5 now reads it -- see the
        BifGrid.cpp patch).
    big_endian : bool
        Write big-endian bytes (only if EF5 runs on a big-endian CPU).
    """
    arr = np.asarray(data)
    if arr.ndim != 2:
        raise ValueError("write_bif expects a 2D array, got shape %r" % (arr.shape,))

    byteorder = ">" if big_endian else "<"
    arr = np.asarray(arr, dtype=np.float64)
    # Replace NaN / inf with the nodata sentinel.
    arr = np.where(np.isfinite(arr), arr, nodata)
    out = np.ascontiguousarray(arr, dtype=np.dtype(byteorder + "f4"))

    nrows, ncols = out.shape
    hdr_fmt = _BIF_HEADER_FMT_BE if big_endian else _BIF_HEADER_FMT_LE
    header = struct.pack(hdr_fmt, int(ncols), int(nrows), float(xll),
                         float(yll), float(cellsize), float(nodata), b"")

    tmp = path + ".tmp"
    with open(tmp, "wb") as fh:
        fh.write(header)
        fh.write(out.tobytes(order="C"))
    os.replace(tmp, path)  # atomic on the same filesystem
    return path


def read_bif(path, big_endian=False):
    """Read an EF5 BIF file.  Returns ``(data, meta)`` where ``data`` is a
    north-up float32 2D array and ``meta`` is a dict with ncols, nrows, xll,
    yll, cellsize, nodata.  Handy for verifying round-trips.
    """
    hdr_fmt = _BIF_HEADER_FMT_BE if big_endian else _BIF_HEADER_FMT_LE
    byteorder = ">" if big_endian else "<"
    with open(path, "rb") as fh:
        raw = fh.read(_BIF_HEADER_SIZE)
        if len(raw) != _BIF_HEADER_SIZE:
            raise ValueError("%s is too short to be a BIF file" % path)
        ncols, nrows, xll, yll, cellsize, nodata, _pad = struct.unpack(hdr_fmt, raw)
        count = ncols * nrows
        data = np.frombuffer(fh.read(count * 4), dtype=np.dtype(byteorder + "f4"),
                             count=count)
        if data.size != count:
            raise ValueError("%s body truncated: expected %d values, got %d"
                             % (path, count, data.size))
        data = data.reshape(nrows, ncols).astype(np.float32)
    meta = dict(ncols=ncols, nrows=nrows, xll=xll, yll=yll,
                cellsize=cellsize, nodata=nodata)
    return data, meta


# ===========================================================================
# Reference-grid (target model grid) loading
# ===========================================================================
def _require_rasterio():
    if not _HAVE_RASTERIO:
        raise ImportError(
            "rasterio is required for this operation. Install it with\n"
            "    pip install rasterio\n"
            "(it bundles GDAL and provides the 155+ raster drivers).")


def read_reference_grid(path):
    """Read the target grid definition from a reference raster.

    Accepts EF5's own grids: GeoTIFF/any-GDAL raster, Arc ASCII ``.asc`` or an
    existing ``.bif``.  Returns a :class:`GridSpec`.  Use this to pass the EF5
    DEM as the alignment target so converted forcings land on exactly the same
    grid (which triggers EF5's fast index-match read path).
    """
    ext = os.path.splitext(path)[1].lower()
    if ext == ".bif":
        _, meta = read_bif(path)
        cell = float(meta["cellsize"])
        top = float(meta["yll"]) + meta["nrows"] * cell
        transform = Affine(cell, 0.0, float(meta["xll"]),
                           0.0, -cell, top) if _HAVE_RASTERIO else \
            _PlainAffine(cell, float(meta["xll"]), top)
        return GridSpec(meta["ncols"], meta["nrows"], transform, None,
                        meta["nodata"])

    if ext == ".asc":
        return _read_asc_header(path)

    # Everything else -> GDAL via rasterio.
    _require_rasterio()
    with rasterio.open(path) as ds:
        t = ds.transform
        if t.e > 0:  # south-up; normalise the spec to north-up
            top = t.f + ds.height * t.e
            t = Affine(t.a, t.b, t.c, t.d, -abs(t.e), top)
        _check_square(t, path)
        nodata = ds.nodata if ds.nodata is not None else DEFAULT_NODATA
        return GridSpec(ds.width, ds.height, t, ds.crs, nodata)


class _PlainAffine(object):
    """Minimal Affine stand-in for the rasterio-absent .bif fast path."""
    __slots__ = ("a", "b", "c", "d", "e", "f")

    def __init__(self, cell, left, top):
        self.a, self.b, self.c = cell, 0.0, left
        self.d, self.e, self.f = 0.0, -cell, top


def _read_asc_header(path):
    """Parse an Esri/Arc ASCII grid header into a GridSpec (supports both
    corner and center origin variants)."""
    hdr = {}
    with open(path, "r") as fh:
        for _ in range(6):
            parts = fh.readline().split()
            if len(parts) >= 2:
                hdr[parts[0].lower()] = parts[1]
    ncols = int(hdr["ncols"])
    nrows = int(hdr["nrows"])
    cell = float(hdr["cellsize"])
    if "xllcorner" in hdr:
        xll = float(hdr["xllcorner"])
        yll = float(hdr["yllcorner"])
    else:  # xllcenter / yllcenter
        xll = float(hdr["xllcenter"]) - cell / 2.0
        yll = float(hdr["yllcenter"]) - cell / 2.0
    top = yll + nrows * cell
    nodata = float(hdr.get("nodata_value", DEFAULT_NODATA))
    transform = Affine(cell, 0.0, xll, 0.0, -cell, top) if _HAVE_RASTERIO \
        else _PlainAffine(cell, xll, top)
    return GridSpec(ncols, nrows, transform, None, nodata)


def _check_square(transform, path, tol=1e-6):
    ax, ey = abs(transform.a), abs(transform.e)
    if abs(ax - ey) > tol * max(ax, ey):
        warnings.warn(
            "%s has non-square cells (dx=%.8g, dy=%.8g). EF5 grids use a single "
            "cellsize; pass --ref <DEM> to resample onto a square model grid."
            % (path, ax, ey))


# ===========================================================================
# Universal raster loading
# ===========================================================================
# Extensions that usually carry a *named variable* plus extra dimensions and so
# are best opened through xarray (variable + time/level selection).
_SCIENTIFIC_EXTS = {".nc", ".nc4", ".cdf", ".netcdf",
                    ".grib", ".grib2", ".grb", ".grb2", ".gb2",
                    ".h5", ".hdf", ".hdf5", ".he5", ".zarr"}


def _is_scientific(path, var):
    ext = os.path.splitext(path)[1].lower()
    if path.endswith(".zarr") or os.path.isdir(path):
        return True
    return ext in _SCIENTIFIC_EXTS or var is not None


def load_raster(path, var=None, band=1, time=None, level=None,
                timestamp=None, scale=1.0, offset=0.0):
    """Load a single 2D north-up layer from any supported file.

    Returns ``(array float32 north-up, transform Affine, crs, nodata)``.

    Parameters
    ----------
    var : str, optional
        Variable / subdataset name for netCDF / GRIB / HDF (e.g. ``"tp"``,
        ``"Rainf_f_tavg"``).  Required when the file holds several variables.
    band : int
        1-based band index for plain rasters (GeoTIFF, BIL, ...).
    time : int, optional
        0-based index along the time dimension (scientific files).
    timestamp : str, optional
        Select a time by label instead of index (e.g. ``"2020-06-01T00:00"``).
    level : int, optional
        0-based index along a vertical / ensemble dimension if present.
    scale, offset : float
        Linear unit transform applied as ``value*scale + offset`` (e.g. convert
        kg m-2 s-1 precip rate to mm over the step, or K to degC).
    """
    if _is_scientific(path, var):
        arr, transform, crs, nodata = _load_scientific(
            path, var=var, time=time, level=level, timestamp=timestamp)
    else:
        arr, transform, crs, nodata = _load_gdal(path, band=band)

    if scale != 1.0 or offset != 0.0:
        valid = arr != nodata
        arr = arr.astype(np.float64)
        arr[valid] = arr[valid] * scale + offset
        arr = arr.astype(np.float32)

    return arr, transform, crs, nodata


def _load_gdal(path, band=1):
    """Open a plain georeferenced raster through rasterio/GDAL and return a
    north-up layer."""
    _require_rasterio()
    with rasterio.open(path) as ds:
        if ds.count == 0:
            # netCDF/HDF opened as a container of subdatasets.
            if ds.subdatasets:
                raise ValueError(
                    "%s exposes subdatasets, not a flat raster. Re-run with "
                    "--var <name>; available subdatasets:\n  %s"
                    % (path, "\n  ".join(ds.subdatasets)))
            raise ValueError("%s has no raster bands" % path)
        data = ds.read(band).astype(np.float32)
        t = ds.transform
        src_nodata = ds.nodata
        if t.e > 0:  # south-up file -> flip to north-up
            data = data[::-1, :]
            top = t.f + ds.height * t.e
            t = Affine(t.a, t.b, t.c, t.d, -abs(t.e), top)
        # Normalize ALL missing-data sentinels to a single nodata value so EF5's
        # `value != noData` test works. Some Arc/GDAL grids (e.g. flow-accum)
        # *declare* one nodata (-9999) but actually fill cells with the float32
        # minimum (-3.4e38); reading raw and masking only the declared value
        # leaks that sentinel through as if it were real data. Treat as missing:
        # non-finite, the declared nodata, and |value| >= 1e30.
        mask = ~np.isfinite(data)
        if src_nodata is not None:
            mask |= (data == np.float32(src_nodata))
        mask |= np.abs(data) >= 1e30
        data = np.where(mask, np.float32(DEFAULT_NODATA), data)
        return data, t, ds.crs, float(DEFAULT_NODATA)


def _load_scientific(path, var=None, time=None, level=None, timestamp=None):
    """Open netCDF / GRIB / HDF / Zarr through xarray, select one 2D slice and
    return it north-up with a derived affine transform + CRS."""
    try:
        import xarray as xr
    except Exception as exc:  # pragma: no cover
        raise ImportError(
            "Reading %s needs xarray. Install the scientific extras:\n"
            "    pip install xarray rioxarray netCDF4 h5netcdf cfgrib zarr\n"
            "Original error: %s" % (path, exc))

    ds = _open_xarray(xr, path)
    try:
        da = _select_variable(ds, var)
        da = _select_dims(da, time=time, level=level, timestamp=timestamp)
        da = _orient_yx(da)  # dims -> (y, x), latitude descending (north-up)

        arr = np.asarray(da.values, dtype=np.float32)
        nodata = _extract_nodata(da)
        if nodata is None:
            nodata = DEFAULT_NODATA
        arr = np.where(np.isnan(arr), nodata, arr).astype(np.float32)

        transform, crs = _transform_from_coords(xr, da)
        return arr, transform, crs, float(nodata)
    finally:
        ds.close()


def _open_xarray(xr, path):
    ext = os.path.splitext(path)[1].lower()
    if path.endswith(".zarr") or (os.path.isdir(path)):
        return xr.open_zarr(path)
    if ext in (".grib", ".grib2", ".grb", ".grb2", ".gb2"):
        return xr.open_dataset(path, engine="cfgrib")
    # netCDF / HDF: let xarray auto-pick netCDF4/h5netcdf/scipy.
    return xr.open_dataset(path)


def _select_variable(ds, var):
    import xarray as xr
    if isinstance(ds, xr.DataArray):
        return ds
    data_vars = list(ds.data_vars)
    if var is not None:
        if var not in ds.variables:
            raise KeyError("Variable %r not found in %s. Available: %s"
                           % (var, getattr(ds, "encoding", {}).get("source", "file"),
                              ", ".join(data_vars)))
        return ds[var]
    # Heuristic: the data variable with the most dimensions / that has y & x.
    candidates = [v for v in data_vars
                  if _has_yx(ds[v])]
    if not candidates:
        candidates = data_vars
    if len(candidates) != 1:
        raise ValueError(
            "%s holds multiple variables; choose one with --var. Candidates: %s"
            % ("file", ", ".join(candidates)))
    return ds[candidates[0]]


def _dim_name(da, options):
    for name in da.dims:
        if str(name).lower() in options:
            return name
    return None


def _has_yx(da):
    y = _dim_name(da, {"y", "lat", "latitude", "rlat", "south_north", "nj"})
    x = _dim_name(da, {"x", "lon", "longitude", "rlon", "west_east", "ni"})
    return y is not None and x is not None


def _select_dims(da, time=None, level=None, timestamp=None):
    # Time selection.
    tdim = _dim_name(da, {"time", "valid_time", "t", "step", "forecast_time"})
    if tdim is not None and tdim in da.dims and da.sizes[tdim] > 1:
        if timestamp is not None:
            da = da.sel({tdim: timestamp}, method="nearest")
        elif time is not None:
            da = da.isel({tdim: int(time)})
        else:
            da = da.isel({tdim: 0})
            warnings.warn("Multiple time steps present; defaulting to index 0. "
                          "Use --time / --timestamp, or convert-timeseries.")
    elif tdim is not None and tdim in da.dims:
        da = da.isel({tdim: 0})

    # Collapse any remaining non-(y,x) dimensions (level, ensemble, ...).
    for d in list(da.dims):
        if _has_yx(da) and d not in (_dim_name(da, {"y", "lat", "latitude", "rlat", "south_north", "nj"}),
                                     _dim_name(da, {"x", "lon", "longitude", "rlon", "west_east", "ni"})):
            idx = int(level) if level is not None else 0
            da = da.isel({d: min(idx, da.sizes[d] - 1)})
    return da


def _orient_yx(da):
    y = _dim_name(da, {"y", "lat", "latitude", "rlat", "south_north", "nj"})
    x = _dim_name(da, {"x", "lon", "longitude", "rlon", "west_east", "ni"})
    if y is None or x is None:
        raise ValueError("Could not identify Y/X dimensions in %r" % (da.dims,))
    da = da.transpose(y, x)
    # Ensure latitude DESCENDING so row 0 = north.
    try:
        coord = da[y].values
        if coord[0] < coord[-1]:
            da = da.isel({y: slice(None, None, -1)})
    except Exception:
        pass
    return da


def _extract_nodata(da):
    for key in ("_FillValue", "missing_value", "nodata", "fmissing_value"):
        if key in da.attrs:
            try:
                return float(da.attrs[key])
            except (TypeError, ValueError):
                pass
    return None


def _transform_from_coords(xr, da):
    """Derive an affine transform (and CRS if rioxarray is available) from the
    1D lat/lon coordinates of a north-up DataArray."""
    # Prefer rioxarray: it understands CF grid_mapping / CRS and cell corners.
    try:
        import rioxarray  # noqa: F401
        da2 = da.rio.write_crs(da.rio.crs or "EPSG:4326", inplace=False) \
            if hasattr(da, "rio") else da
        t = da2.rio.transform()
        crs = da2.rio.crs
        return t, crs
    except Exception:
        pass

    y = _dim_name(da, {"y", "lat", "latitude", "rlat", "south_north", "nj"})
    x = _dim_name(da, {"x", "lon", "longitude", "rlon", "west_east", "ni"})
    lats = np.asarray(da[y].values, dtype=np.float64)
    lons = np.asarray(da[x].values, dtype=np.float64)
    dx = float(np.abs(np.mean(np.diff(lons))))
    dy = float(np.abs(np.mean(np.diff(lats))))
    if abs(dx - dy) > 1e-6 * max(dx, dy):
        warnings.warn("Non-square native cells (dx=%.8g, dy=%.8g); pass --ref "
                      "to resample onto a square model grid." % (dx, dy))
    # Coordinates are cell centres -> shift half a cell to the corner.
    left = float(lons.min()) - dx / 2.0
    top = float(lats.max()) + dy / 2.0
    if _HAVE_RASTERIO:
        transform = Affine(dx, 0.0, left, 0.0, -dy, top)
    else:
        transform = _PlainAffine(dx, left, top)
    crs = "EPSG:4326"
    return transform, crs


# ===========================================================================
# Resampling / alignment onto the reference grid
# ===========================================================================
def align_to_reference(arr, src_transform, src_crs, src_nodata, ref,
                       resampling="bilinear"):
    """Reproject/resample a source layer onto the reference GridSpec so the
    output matches EF5's model grid exactly (fast index-match read path).
    """
    _require_rasterio()
    methods = {
        "nearest": Resampling.nearest,
        "bilinear": Resampling.bilinear,
        "cubic": Resampling.cubic,
        "average": Resampling.average,
        "min": Resampling.min,
        "max": Resampling.max,
        "mode": Resampling.mode,
    }
    if resampling not in methods:
        raise ValueError("Unknown resampling %r; choose from %s"
                         % (resampling, ", ".join(methods)))

    dst = np.full((ref.height, ref.width), ref.nodata, dtype=np.float32)
    src_crs = src_crs if src_crs is not None else "EPSG:4326"
    dst_crs = ref.crs if ref.crs is not None else src_crs
    reproject(
        source=np.ascontiguousarray(arr, dtype=np.float32),
        destination=dst,
        src_transform=src_transform,
        src_crs=src_crs,
        src_nodata=src_nodata,
        dst_transform=ref.transform,
        dst_crs=dst_crs,
        dst_nodata=ref.nodata,
        resampling=methods[resampling],
    )
    return dst


# ===========================================================================
# High-level convenience API
# ===========================================================================
def _default_resampling(role, override):
    if override:
        return override
    if role and role.lower() in CATEGORICAL_ROLES:
        return "nearest"
    return "bilinear"


def convert(src, dst, ref=None, role=None, var=None, band=1, time=None,
            level=None, timestamp=None, scale=1.0, offset=0.0,
            nodata=DEFAULT_NODATA, resampling=None, big_endian=False,
            verbose=True):
    """Convert a single source raster to a single BIF file.

    If ``ref`` (a path or :class:`GridSpec`) is given, the output is resampled
    onto that grid -- pass your EF5 DEM so forcings align cell-for-cell.
    ``role`` (e.g. ``"ddm"``) selects sensible defaults (nearest-neighbour for
    categorical grids).
    """
    arr, src_t, src_crs, src_nd = load_raster(
        src, var=var, band=band, time=time, level=level, timestamp=timestamp,
        scale=scale, offset=offset)

    method = _default_resampling(role, resampling)

    if ref is not None:
        spec = ref if isinstance(ref, GridSpec) else read_reference_grid(ref)
        spec.nodata = nodata
        out = align_to_reference(arr, src_t, src_crs, src_nd, spec,
                                 resampling=method)
        xll, yll, cell = spec.xll, spec.yll, spec.cellsize
    else:
        _check_square(src_t, src)
        out = np.where(arr == src_nd, nodata, arr).astype(np.float32)
        cell = float(src_t.a)
        xll = float(src_t.c)
        yll = float(src_t.f) - out.shape[0] * cell  # f is top (north) edge
    write_bif(dst, out, xll, yll, cell, nodata=nodata, big_endian=big_endian)
    if verbose:
        finite = out[out != nodata]
        rng = (float(finite.min()), float(finite.max())) if finite.size else (None, None)
        print("[crest-preprocess] %s -> %s  (%dx%d cell=%.6g xll=%.6g yll=%.6g "
              "method=%s range=%s)" % (os.path.basename(src), dst, out.shape[1],
                                       out.shape[0], cell, xll, yll, method, rng))
    return dst


def convert_timeseries(src, out_dir, name_template, ref=None, role=None,
                       var=None, level=None, scale=1.0, offset=0.0,
                       nodata=DEFAULT_NODATA, resampling=None,
                       big_endian=False, verbose=True):
    """Explode a multi-timestep scientific file (netCDF/GRIB/Zarr) into one BIF
    per time step, named by the time coordinate.

    ``name_template`` is an strftime pattern, e.g. ``"precip.%Y%m%d%H%M.bif"``
    (match the DatedName pattern in your EF5 control file).
    """
    try:
        import xarray as xr
    except Exception as exc:  # pragma: no cover
        raise ImportError("convert_timeseries needs xarray: %s" % exc)

    ds = _open_xarray(xr, src)
    written = []
    try:
        da = _select_variable(ds, var)
        tdim = _dim_name(da, {"time", "valid_time", "t", "step", "forecast_time"})
        if tdim is None:
            raise ValueError("%s has no time dimension; use convert() instead" % src)
        spec = None
        if ref is not None:
            spec = ref if isinstance(ref, GridSpec) else read_reference_grid(ref)
            spec.nodata = nodata
        method = _default_resampling(role, resampling)
        os.makedirs(out_dir, exist_ok=True)

        import pandas as pd
        times = pd.to_datetime(da[tdim].values)
        for i, t in enumerate(times):
            layer = _select_dims(da, time=i, level=level)
            layer = _orient_yx(layer)
            arr = np.asarray(layer.values, dtype=np.float32)
            src_nd = _extract_nodata(layer)
            src_nd = DEFAULT_NODATA if src_nd is None else src_nd
            arr = np.where(np.isnan(arr), src_nd, arr).astype(np.float32)
            if scale != 1.0 or offset != 0.0:
                valid = arr != src_nd
                arr = arr.astype(np.float64)
                arr[valid] = arr[valid] * scale + offset
                arr = arr.astype(np.float32)
            transform, crs = _transform_from_coords(xr, layer)

            if spec is not None:
                out = align_to_reference(arr, transform, crs, src_nd, spec,
                                         resampling=method)
                xll, yll, cell = spec.xll, spec.yll, spec.cellsize
            else:
                out = np.where(arr == src_nd, nodata, arr).astype(np.float32)
                cell = float(transform.a)
                xll = float(transform.c)
                yll = float(transform.f) - out.shape[0] * cell

            fname = pd.Timestamp(t).strftime(name_template)
            path = os.path.join(out_dir, fname)
            write_bif(path, out, xll, yll, cell, nodata=nodata,
                      big_endian=big_endian)
            written.append(path)
            if verbose:
                print("[crest-preprocess] step %d/%d %s -> %s"
                      % (i + 1, len(times), pd.Timestamp(t).isoformat(), path))
    finally:
        ds.close()
    return written


# ===========================================================================
# Inspection helpers
# ===========================================================================
def list_contents(path):
    """Print the variables / subdatasets in a file to help pick ``--var``."""
    ext = os.path.splitext(path)[1].lower()
    if _is_scientific(path, None):
        try:
            import xarray as xr
            ds = _open_xarray(xr, path)
            print("Variables in %s:" % path)
            for v in ds.data_vars:
                print("  %-24s dims=%s shape=%s"
                      % (v, ds[v].dims, ds[v].shape))
            ds.close()
            return
        except Exception as exc:
            print("xarray could not open %s (%s); trying GDAL subdatasets..."
                  % (path, exc))
    _require_rasterio()
    with rasterio.open(path) as ds:
        if ds.subdatasets:
            print("Subdatasets in %s:" % path)
            for s in ds.subdatasets:
                print("  %s" % s)
        else:
            print("%s: %d band(s), %dx%d, crs=%s, dtype=%s"
                  % (path, ds.count, ds.width, ds.height, ds.crs, ds.dtypes[0]))


# ===========================================================================
# Command line interface
# ===========================================================================
def _build_parser():
    p = argparse.ArgumentParser(
        prog="crest_preprocess",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        description=__doc__,
        epilog="""
Examples
--------
# Inspect a netCDF/GRIB to find variable names:
  python crest_preprocess.py list forcing.nc

# Convert a GeoTIFF DEM to BIF (no resampling -- it IS the model grid):
  python crest_preprocess.py convert dem.tif dem.bif

# Convert an ESRI flow-direction grid (categorical -> nearest):
  python crest_preprocess.py convert ddm.tif ddm.bif --role ddm

# Convert one GRIB2 precip field, aligning to the EF5 DEM grid:
  python crest_preprocess.py convert qpe.grib2 precip.bif --ref dem.tif \\
      --var tp --role precip

# Convert a GLDAS netCDF rain rate (kg m-2 s-1 -> mm over a 3-h step):
  python crest_preprocess.py convert GLDAS.nc4 precip.bif --ref dem.bif \\
      --var Rainf_f_tavg --scale 10800

# Explode a multi-step netCDF into per-timestep BIF files for EF5:
  python crest_preprocess.py convert-ts era5.nc ./precip_bif \\
      --name "precip.%Y%m%d%H%M.bif" --ref dem.bif --var tp --scale 1000

# Verify a BIF round-trip:
  python crest_preprocess.py inspect precip.bif
""")
    sub = p.add_subparsers(dest="cmd", required=True)

    c = sub.add_parser("convert", help="convert one raster/slice to one BIF")
    c.add_argument("src")
    c.add_argument("dst")
    c.add_argument("--ref", help="reference grid (DEM .tif/.asc/.bif) to align to")
    c.add_argument("--role", help="grid role: precip|pet|temp|dem|ddm|fam "
                                   "(sets nearest resampling for ddm/fam)")
    c.add_argument("--var", help="variable / subdataset name (netCDF/GRIB/HDF)")
    c.add_argument("--band", type=int, default=1, help="1-based band index")
    c.add_argument("--time", type=int, help="0-based time index")
    c.add_argument("--timestamp", help="time label, e.g. 2020-06-01T00:00")
    c.add_argument("--level", type=int, help="0-based level/ensemble index")
    c.add_argument("--scale", type=float, default=1.0, help="value*scale+offset")
    c.add_argument("--offset", type=float, default=0.0)
    c.add_argument("--nodata", type=float, default=DEFAULT_NODATA)
    c.add_argument("--resampling", help="nearest|bilinear|cubic|average|min|max|mode")
    c.add_argument("--big-endian", action="store_true",
                   help="write big-endian (only if EF5 runs on a big-endian CPU)")

    t = sub.add_parser("convert-ts",
                        help="explode a multi-step file into one BIF per step")
    t.add_argument("src")
    t.add_argument("out_dir")
    t.add_argument("--name", required=True,
                   help="strftime template, e.g. precip.%%Y%%m%%d%%H%%M.bif")
    t.add_argument("--ref")
    t.add_argument("--role")
    t.add_argument("--var")
    t.add_argument("--level", type=int)
    t.add_argument("--scale", type=float, default=1.0)
    t.add_argument("--offset", type=float, default=0.0)
    t.add_argument("--nodata", type=float, default=DEFAULT_NODATA)
    t.add_argument("--resampling")
    t.add_argument("--big-endian", action="store_true")

    li = sub.add_parser("list", help="list variables/subdatasets in a file")
    li.add_argument("src")

    ins = sub.add_parser("inspect", help="print a BIF header + value stats")
    ins.add_argument("src")
    ins.add_argument("--big-endian", action="store_true")

    return p


def main(argv=None):
    args = _build_parser().parse_args(argv)

    if args.cmd == "convert":
        convert(args.src, args.dst, ref=args.ref, role=args.role, var=args.var,
                band=args.band, time=args.time, level=args.level,
                timestamp=args.timestamp, scale=args.scale, offset=args.offset,
                nodata=args.nodata, resampling=args.resampling,
                big_endian=args.big_endian)
    elif args.cmd == "convert-ts":
        convert_timeseries(args.src, args.out_dir, args.name, ref=args.ref,
                           role=args.role, var=args.var, level=args.level,
                           scale=args.scale, offset=args.offset,
                           nodata=args.nodata, resampling=args.resampling,
                           big_endian=args.big_endian)
    elif args.cmd == "list":
        list_contents(args.src)
    elif args.cmd == "inspect":
        data, meta = read_bif(args.src, big_endian=args.big_endian)
        finite = data[data != meta["nodata"]]
        print("BIF %s" % args.src)
        for k in ("ncols", "nrows", "xll", "yll", "cellsize", "nodata"):
            print("  %-9s %s" % (k, meta[k]))
        print("  top       %s" % (meta["yll"] + meta["nrows"] * meta["cellsize"]))
        if finite.size:
            print("  values    min=%.6g max=%.6g mean=%.6g valid=%d/%d"
                  % (finite.min(), finite.max(), finite.mean(),
                     finite.size, data.size))
        else:
            print("  values    (all nodata)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
