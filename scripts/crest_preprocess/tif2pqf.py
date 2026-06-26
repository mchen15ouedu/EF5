#!/usr/bin/env python3
"""Convert a GeoTIFF (or any rasterio raster) to EF5 PQF (Parquet forcing).
Single column 'v' float32, dense row-major (row 0 = north), grid geometry in
the Parquet file key/value metadata. Matches src/PqfGrid.cpp."""
import sys, numpy as np, rasterio, pyarrow as pa, pyarrow.parquet as pq

def convert(src_path, dst_path, compression='zstd'):
    with rasterio.open(src_path) as s:
        a = s.read(1).astype('float32'); tr = s.transform
        nodata = s.nodata if s.nodata is not None else -9999.0
    nr, nc = a.shape
    xll = tr.c; yll = tr.f + nr * tr.e; cell = tr.a   # tr.e < 0
    meta = {b'ncols': str(nc).encode(), b'nrows': str(nr).encode(),
            b'xllcorner': repr(float(xll)).encode(), b'yllcorner': repr(float(yll)).encode(),
            b'cellsize': repr(float(cell)).encode(), b'nodata': repr(float(nodata)).encode()}
    schema = pa.schema([pa.field('v', pa.float32())]).with_metadata(meta)
    table = pa.table({'v': a.reshape(-1)}, schema=schema)
    pq.write_table(table, dst_path, compression=compression)

if __name__ == '__main__':
    convert(sys.argv[1], sys.argv[2], sys.argv[3] if len(sys.argv) > 3 else 'zstd')
    print('wrote', sys.argv[2])
