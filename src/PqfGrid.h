#ifndef PQF_GRID_H
#define PQF_GRID_H

#include "Grid.h"

/* Parquet forcing grid ("PQF").
 *
 * A single-column Parquet file holding one dense, row-major float32 grid.
 *   - column 0, name "v", type FLOAT, length = nrows*ncols, row 0 = NORTH
 *     (same orientation as BIF/GeoTIFF readers in EF5).
 *   - grid geometry carried in the file-level key/value metadata:
 *       ncols, nrows, xllcorner, yllcorner, cellsize, nodata
 *     (xll/yll = lower-left corner, matching the BIF header convention).
 *
 * Written by scripts/crest_preprocess (tif/any-raster -> .pqf). Compression
 * (snappy/zstd) is handled transparently by Parquet. When EF5 is built without
 * Apache Arrow (--with-arrow not given) this reader is a stub returning NULL.
 */
FloatGrid *ReadFloatPqfGrid(char *file);

#endif
