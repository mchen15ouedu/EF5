# CREST input data pre-process

Convert (almost) any geospatial / weather raster into EF5's **fastest native
format, BIF**, so the model reads grids with a raw `fread` per row instead of
the slow libTIFF scan-line path used for GeoTIFF.

This addresses two problems with the current EF5 input layer:

1. Several formats EF5 carries are obsolete/rare (TRMM-RT, TRMM-V6 HDF4,
   bespoke MRMS binaries).
2. The one common, still-used format — GeoTIFF — is read slowly by EF5
   (libTIFF, scan-line at a time).

The fix is to standardise on **BIF** (`src/BifGrid.cpp`): a 50-byte header plus
row-major `float32`, no parsing, no decompression — the fastest reader in EF5.
This tool converts your real-world inputs into BIF; the companion C++ patch lets
EF5 read BIF for **every** grid role.

---

## 1. Why BIF, and the EF5 C++ patch

Upstream EF5 already reads BIF for **precipitation** and **PET**. A small patch
(included in this branch) extends BIF to the remaining roles so the whole input
layer can be fast:

| File | Change |
|------|--------|
| `src/BifGrid.cpp` | **Bug fix:** the reader never set `grid->noData` from the header — now it does, so missing cells are detected for BIF grids. |
| `src/TempType.h`, `src/TempType.cpp` | Add the `TEMP_BIF` type / `"bif"` string. |
| `src/TempReader.cpp` | Read `.bif` temperature grids. |
| `src/BasicGrids.cpp` | DEM / DDM / FAM extension dispatch now accepts `.bif`. |

After rebuilding EF5, set the format keys in your control file to `bif` and
point the paths at the converted files, e.g.:

```
[PrecipForcing myprecip]
TYPE=BIF
...
[Basic]
DEM=dem.bif
DDM=ddm.bif
FAM=fam.bif
```

(DEM/DDM/FAM are chosen by file extension, so just give them a `.bif` name.)

---

## 2. Install

Core (BIF read/write + all GDAL raster formats):

```bash
pip install numpy rasterio
```

Add the scientific stack for netCDF / GRIB / HDF / Zarr with named variables:

```bash
pip install xarray rioxarray netCDF4 h5netcdf cfgrib zarr pandas
```

The least painful route (GDAL + ecCodes prebuilt) is conda-forge:

```bash
conda create -n crest_pre -c conda-forge rasterio rioxarray xarray netcdf4 cfgrib zarr pandas
conda activate crest_pre
```

> On Windows/conda you may need `set GDAL_DATA=%CONDA_PREFIX%\Library\share\gdal`.

## 3. Supported inputs

Anything GDAL can open (155+ drivers) — opened via `rasterio`:

> GeoTIFF / COG, GRIB1 & GRIB2, netCDF / netCDF4, HDF4 / HDF5 / HDF-EOS, Zarr,
> ENVI **BIL/BSQ/BIP** (`.bil`), Arc/Info ASCII (`.asc`) & Binary Grid (`.adf`),
> ERDAS IMAGINE (`.img`), JPEG2000, SRTM `.hgt`, DTED, USGS DEM, GTopo30, VRT,
> PCRaster, …

Multi-dimensional scientific files (a **variable** + time/level/ensemble dims)
are opened via `xarray` so you can select the variable and timestep:
netCDF (`netCDF4`/`h5netcdf`), GRIB (`cfgrib`), Zarr (`zarr`).

**Output:** EF5 BIF — `float32`, row-major, **row 0 = north**, square cells,
little-endian by default (`--big-endian` for big-endian CPUs).

## 4. Command-line usage

```bash
# Discover variable names inside a netCDF / GRIB / HDF file
python crest_preprocess.py list forcing.nc

# DEM GeoTIFF -> BIF (it defines the grid; no resampling)
python crest_preprocess.py convert dem.tif dem.bif

# Flow-direction grid: categorical -> nearest-neighbour (IMPORTANT)
python crest_preprocess.py convert ddm.tif ddm.bif --role ddm
python crest_preprocess.py convert fam.tif fam.bif --role fam

# One GRIB2 precip field, aligned onto the EF5 DEM grid
python crest_preprocess.py convert qpe.grib2 precip.bif --ref dem.bif --var tp --role precip

# GLDAS rain rate (kg m-2 s-1) -> mm over a 3-hour step, aligned to DEM
python crest_preprocess.py convert GLDAS.nc4 precip.bif --ref dem.bif \
    --var Rainf_f_tavg --scale 10800

# Explode a multi-step netCDF/GRIB into one BIF per timestep for EF5
python crest_preprocess.py convert-ts era5_tp.nc ./precip_bif \
    --name "precip.%Y%m%d%H%M.bif" --ref dem.bif --var tp --scale 1000

# Verify a BIF (prints header + value stats)
python crest_preprocess.py inspect precip.bif
```

### Key options

| Option | Meaning |
|--------|---------|
| `--ref <DEM>` | Resample/reproject the output onto this grid (`.tif`/`.asc`/`.bif`). **Pass your EF5 DEM** so forcings land cell-for-cell on the model grid → EF5's fast index-match read path. |
| `--role` | `precip`/`pet`/`temp`/`dem`/`ddm`/`fam`. `ddm`/`fam` default to **nearest** resampling (never interpolate flow codes). |
| `--var` | Variable / subdataset for netCDF/GRIB/HDF. |
| `--time` / `--timestamp` | Pick a timestep by index or label. |
| `--scale` / `--offset` | `value*scale + offset` unit conversion. |
| `--resampling` | `nearest｜bilinear｜cubic｜average｜min｜max｜mode`. |
| `--nodata` | Output no-data sentinel (default `-9999`). |
| `--big-endian` | Write big-endian (only if EF5 runs on a big-endian CPU). |

## 5. Use as a library

```python
import crest_preprocess as cp

# Align a forcing onto the EF5 DEM grid and write BIF
cp.convert("ldas.nc4", "precip.bif", ref="dem.bif",
           var="Rainf_f_tavg", role="precip", scale=10800)

# Per-timestep export
cp.convert_timeseries("era5.nc", "out_dir", "precip.%Y%m%d%H%M.bif",
                      ref="dem.bif", var="tp", scale=1000)

# Low level
spec = cp.read_reference_grid("dem.bif")              # GridSpec
arr, t, crs, nd = cp.load_raster("x.tif")             # north-up array + georef
out = cp.align_to_reference(arr, t, crs, nd, spec)
cp.write_bif("x.bif", out, spec.xll, spec.yll, spec.cellsize)
data, meta = cp.read_bif("x.bif")                     # round-trip check
```

## 6. Notes & gotchas

- **Row order is mandatory.** EF5 indexes `data[y][x]` with `y` increasing
  southward; the tool always writes north-up (row 0 = north) and auto-flips
  south-up GeoTIFFs and ascending-latitude netCDFs. Writing south-up would
  silently flip your data.
- **Align to the DEM for speed.** EF5 only takes the fast direct-index path when
  the forcing grid has the *same* `ncols × nrows` as the DEM
  (`Grid::IsSpatialMatch` compares dimensions only). Otherwise it falls back to a
  per-cell lat/lon lookup — still correct, just slower. Use `--ref dem.bif`.
- **Categorical grids:** always convert DDM (and usually FAM) with
  `--role ddm` / `--role fam` (nearest). Bilinear would invent fractional flow
  codes. Note `float32` represents integers exactly only up to 2^24 (~16.7M), so
  extremely large FAM counts lose precision — same limitation as EF5's own
  float FAM grids.
- **Endianness:** BIF is raw binary read with `fread`, so bytes are interpreted
  in the EF5 host's native order. Default little-endian covers Windows/Linux
  x86-64; use `--big-endian` only for a big-endian target.

## 7. BIF on-disk layout (matches `src/BifGrid.h`)

`#pragma pack(1)` header, 50 bytes, then `nrows*ncols` `float32` north-up:

```
int32   ncols
int32   nrows
float32 xllcorner   # lower-left corner (west edge)
float32 yllcorner   # lower-left corner (south edge)
float32 cellsize    # square cells
float32 nodata
char[26] padding
```
