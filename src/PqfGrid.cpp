#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "PqfGrid.h"
#include "Messages.h"

#ifndef HAVE_PARQUET

// Built without Apache Arrow: PQF forcing is unavailable. Return NULL so the
// reader reports a missing file (zeros) rather than crashing.
FloatGrid *ReadFloatPqfGrid(char *file) {
  WARNING_LOGF("PQF/Parquet support not compiled in (configure --with-arrow); "
               "cannot read %s",
               file);
  return NULL;
}

#else

#include <arrow/api.h>
#include <arrow/io/file.h>
#include <parquet/arrow/reader.h>
#include <parquet/metadata.h>
#include <cstdlib>
#include <cstring>
#include <memory>

// Pull a key from the Parquet file-level key/value metadata; returns def if
// the key is absent.
static double KvGet(const std::shared_ptr<const arrow::KeyValueMetadata> &kv,
                    const char *key, double def) {
  if (!kv) {
    return def;
  }
  arrow::Result<std::string> r = kv->Get(key);
  if (!r.ok()) {
    return def;
  }
  return atof(r.ValueOrDie().c_str());
}

FloatGrid *ReadFloatPqfGrid(char *file) {
  // Open the file.
  arrow::Result<std::shared_ptr<arrow::io::ReadableFile>> infileR =
      arrow::io::ReadableFile::Open(file);
  if (!infileR.ok()) {
    return NULL; // missing/unreadable -> treated as zeros by the caller
  }
  std::shared_ptr<arrow::io::ReadableFile> infile = infileR.ValueOrDie();

  std::unique_ptr<parquet::arrow::FileReader> reader;
  arrow::Status st = parquet::arrow::OpenFile(
      infile, arrow::default_memory_pool(), &reader);
  if (!st.ok()) {
    WARNING_LOGF("PQF file %s could not be opened as Parquet", file);
    return NULL;
  }

  // Geometry from key/value metadata.
  std::shared_ptr<parquet::FileMetaData> md = reader->parquet_reader()->metadata();
  std::shared_ptr<const arrow::KeyValueMetadata> kv = md->key_value_metadata();
  long ncols = (long)KvGet(kv, "ncols", -1);
  long nrows = (long)KvGet(kv, "nrows", -1);
  if (ncols <= 0 || nrows <= 0) {
    WARNING_LOGF("PQF file %s missing ncols/nrows metadata", file);
    return NULL;
  }
  double xll = KvGet(kv, "xllcorner", 0.0);
  double yll = KvGet(kv, "yllcorner", 0.0);
  double cellsize = KvGet(kv, "cellsize", 0.0);
  float nodata = (float)KvGet(kv, "nodata", -9999.0);

  // Read the single data column.
  std::shared_ptr<arrow::Table> table;
  st = reader->ReadTable(&table);
  if (!st.ok() || table->num_columns() < 1) {
    WARNING_LOGF("PQF file %s: failed to read data column", file);
    return NULL;
  }
  std::shared_ptr<arrow::ChunkedArray> col = table->column(0);
  if ((long)col->length() != nrows * ncols) {
    WARNING_LOGF("PQF file %s: %li values != %li x %li grid", file,
                 (long)col->length(), nrows, ncols);
    return NULL;
  }

  FloatGrid *grid = new FloatGrid();
  grid->numCols = ncols;
  grid->numRows = nrows;
  grid->cellSize = cellsize;
  grid->extent.left = xll;
  grid->extent.bottom = yll;
  grid->extent.top = yll + nrows * cellsize;
  grid->extent.right = xll + ncols * cellsize;
  grid->noData = nodata;

  grid->data = new float *[nrows]();
  for (long i = 0; i < nrows; i++) {
    grid->data[i] = new float[ncols];
  }

  // Copy float values out chunk-by-chunk into row-major data[row][col].
  long flat = 0;
  for (int c = 0; c < col->num_chunks(); c++) {
    std::shared_ptr<arrow::Array> chunk = col->chunk(c);
    std::shared_ptr<arrow::FloatArray> fa =
        std::static_pointer_cast<arrow::FloatArray>(chunk);
    const float *vals = fa->raw_values();
    long n = fa->length();
    for (long k = 0; k < n; k++, flat++) {
      long row = flat / ncols;
      long cl = flat % ncols;
      // honor explicit nulls if present
      grid->data[row][cl] =
          (fa->IsNull(k)) ? nodata : vals[k];
    }
  }

  return grid;
}

#endif // HAVE_PARQUET
