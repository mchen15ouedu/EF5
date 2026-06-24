# EF5 Lake Module + Fast I/O — Handoff Memo

**Date:** 2026-06-24 · **Branch:** `master` · **Remote:** `https://github.com/mchen15ouedu/EF5.git`
**Author of these changes:** a Claude Code session running on the user's **Windows** box
(no C++ compiler there). This memo exists so a fresh Claude on the **HPC** can pick up
the work with full context — the prior session's chat history and machine-local memory
do **not** travel; everything you need is in this file + the repo.

> **First thing to do on HPC: build the project** (Section 4). The C++ edits below were
> written but **never compiled** on Windows. Expect to fix a small compile error or two,
> then run a sample (Section 5) before moving on to the remaining work (Section 3).

---

## 0. What this project is

EF5 (Ensemble Framework For Flash Flood Forecasting), a distributed hydrologic model in
C++ (autotools build, binary `ef5`). The user added a **gridded** lake/reservoir module
(concept modeled on **mizuRoute**, which is vector/reach-based — in both, the user tags a
single outlet location as the lake; we do **not** delineate the lake body). Two workstreams:

1. **Lake module bug review** — done; fixes applied (Section 1), three items deferred (Section 3).
2. **Faster input I/O** — convert real-world geospatial/weather rasters into EF5's fastest
   native format (**BIF**) and let EF5 read BIF for every grid role (Section 2).

---

## 1. Lake fixes APPLIED (uncompiled — verify after building)

Linear-reservoir outflow used everywhere: `O = (1/(a·3600)) · S · (S/Vth)^b`, where
`a` = `param_a` = `klake` (retention time, hours), `b` = `param_b`, `S` = storage,
`Vth` = `th_volume` (threshold/“full pool” volume).

| # | Fix | Where |
|---|-----|-------|
| **#1** | `param_a` was never set from the `klake` column → `param_a=0` → `CalculateLinearReservoirOutflow` returned 0 → **dry-season outflow was always zero**. Now `param_a = retention_constant` in the **active** CSV reader and the `[LAKE]`-section path. | [BasinConfigSection.cpp:165](src/BasinConfigSection.cpp), [:316](src/BasinConfigSection.cpp); also legacy [LakeModel.cpp `ReadLakeInfoCSV`](src/LakeModel.cpp) |
| **#2** | Overflow branch subtracted the spill **twice** (set `storage=th_volume` *and* then `storage -= outflow·dt`) → water destroyed every spill step. Restructured so the trailing subtraction only runs in the dry/engineered branches. | `LakeModel.cpp`: `ApplyHorizontalBalance`, `LakeModelImpl::Step`, `LegacyLakeModel::Step` |
| **#5** | `CalculateInflow` **averaged** upstream-neighbor discharge; should **sum** (mass conservation). Now sums. | [LakeMap.cpp `CalculateInflow`](src/LakeMap.cpp) |
| **#7** | If `dt > klake` (timestep > residence time) the explicit outflow can overshoot. Per user decision: **warn once, do NOT cap** (capping breaks mass balance). | `LakeModel.cpp ApplyHorizontalBalance` (uses `warnedDtResidence`) |
| **#9** | Cold-start storage was inconsistent (constructor `th_volume` vs `InitializeStates` `0.8·th_volume`). Unified to `th_volume` (lake at outlet level). mizuRoute likewise prefers restart files and cold-starts near operating level, not empty. | `LakeModel.cpp InitializeStates` |
| **#10** | Each lake allocated a `lakeNodes` vector **sized to the whole grid** → `O(N_lakes × N_cells)` memory (fatal for global runs). Now a single `LakeGridNode lakeNode;`. | `LakeModel.h` + all `lakeNodes[...]` sites in `LakeModel.cpp` |
| **#11** | `CalculateInflow` did `O(N_cells)` linear scans per neighbor per lake per timestep. Added a `(y<<32\|x)→node-index` map built once. | `LakeMap.h` (member `nodeIndexByLoc`) + `LakeMap.cpp CalculateInflow` |

**Riskiest edits to re-check after it compiles:** #10 (the vector→single-struct refactor
touched ~11 sites — make sure no `lakeNodes` references remain and state save/load still
writes the right cell) and #11 (the node-index map). Brace counts were verified equal but
that is not a compile.

**Behavioral expectations once built & run** (good regression checks):
- A lake with `storage ≤ th_volume` now produces **non-zero** outflow that scales with `klake`.
- On a spill step, post-step `storage == th_volume` exactly (no extra loss).
- Lake volume/outflow respond to `klake` changes (previously inert).

---

## 2. Faster I/O — BIF everywhere + a preprocessor

**Why BIF:** `src/BifGrid.cpp` reads a 50-byte header + raw row-major `float32` with one
`fread` per row — no parsing, no decompression. It is the fastest reader in EF5. GeoTIFF
(libTIFF, scan-line) is the slow path the user wanted to avoid.

### 2a. C++ patch (applied) — BIF for every grid role
Upstream EF5 already read BIF for **precip** & **PET**. Added:
- [BifGrid.cpp](src/BifGrid.cpp) — **bug fix:** reader never set `grid->noData` from the header; now it does.
- [TempType.h](src/TempType.h)/[TempType.cpp](src/TempType.cpp)/[TempReader.cpp](src/TempReader.cpp) — temperature reads `.bif` (`TEMP_BIF`).
- [BasicGrids.cpp](src/BasicGrids.cpp) — DEM/DDM/FAM extension dispatch accepts `.bif`.

Usage in a control file: set `TYPE=BIF` for forcings and give DEM/DDM/FAM `.bif` filenames.

### 2b. The preprocessor — `scripts/crest_preprocess/`
`crest_preprocess.py` (+ `README.md`, `requirements.txt`). Pure-Python, **standalone**,
not part of the EF5 build. Converts almost any geospatial/weather raster → EF5 BIF:
- Reads via **GDAL/rasterio** (GeoTIFF/COG, GRIB1/2, netCDF, HDF4/5/EOS, Zarr, ENVI
  BIL/BSQ/BIP, Arc ASCII/Binary, ERDAS .img, JPEG2000, SRTM .hgt, DTED, USGS DEM, …)
  and **xarray** (+cfgrib/netCDF4/zarr) for multi-dim files (variable + time/level select).
- Always writes **north-up** (row 0 = north), square cells, little-endian (matches EF5);
  aligns/resamples to a reference DEM grid (`--ref dem.bif`) so forcings hit EF5's fast
  index-match path; `--role ddm/fam` forces nearest-neighbor (never interpolate flow codes).
- CLI: `convert`, `convert-ts` (explode a time series), `list`, `inspect`. See its README.
- **Already tested on Windows** (BIF round-trip vs the C++ struct layout, GeoTIFF
  orientation incl. south-up flip, alignment, and a netCDF variable+time path) — those
  tests passed. The BIF format itself is verified byte-for-byte against `src/BifGrid.h`.

Env on the user's Windows box: conda envs `rasters` (rasterio+rioxarray+xarray) and
`netcdf` (rasterio+xarray+netCDF4) work. On HPC, `conda create -c conda-forge rasterio
rioxarray xarray netcdf4 cfgrib zarr pandas` (or load a GDAL module).

---

## 3. REMAINING WORK (the point of moving to HPC)

### #3 — Lake calibration never runs the lake physics  *(highest value)*
`Simulator::SimulateForCali` (around [Simulator.cpp:2485](src/Simulator.cpp)) runs only
`runModel->WaterBalance(...)` + `runRoutingModel->Route(...)`. It does **not** call
`ApplyVerticalBalance`/`ApplyHorizontalBalance` and does **not** push the calibrated `klake`
into a lake model. `caliLModels` are allocated ([Simulator.cpp:717](src/Simulator.cpp)) but
never stepped, and `CarveLakeParameters` ([BasicGrids.cpp:1119](src/BasicGrids.cpp)) is a
no-op. Net effect: DREAM optimizes `klake` against a simulation the lake never influenced.
**To do:** mirror the main loop's lake stepping inside the calibration loop, and map the
DREAM parameter vector → `param_a` (klake) of the cali lake model(s). Test with
`sample_control_with_lake_calibration.txt`.

### #4 — Engineered (dam) discharge is an unfinished feature
`ReadEngineeredDischargeFromCSV` ([BasinConfigSection.cpp:195](src/BasinConfigSection.cpp))
keys the map by **lake name** and reads only the **first** CSV data row; but
`LakeCalculations::GetEngineeredDischarge` looks up by **timestamp**, and every lake is
constructed with `wm_flag=false`. So engineered discharge is 100% dead. **To do:** decide
the real data model (recommended: `map<lakeName, map<normalizedTimestamp, q>>`), read all
rows, normalize timestamps to match `TimeVarToTimestamp` ("YYYYMMDD_HHUU"), give each
`LakeModelImpl` its own series + `wm_flag=true`, and stop treating `0.0` as "no value"
(use presence in the map). Touches `BasinConfigSection`, `Simulator`, `LakeModel`.

### #8 — One-timestep downstream lag  *(document; optional)*
Lake outflow is injected into `currentQ` **after** routing, so downstream cells see the
regulated flow one step late. Intrinsic to the operator split; small at high temporal
resolution. A true fix needs lake-aware routing — treat as a design note unless required.

### #6 — Single-cell lake — **accepted, no change.** Matches mizuRoute (user tags one
outlet/reach as the lake). DEM-based delineation was judged to add more error than it removes.

---

## 4. Build on HPC

Autotools project; deps: **zlib (`-lz`), libtiff (`-ltiff`), libgeotiff (`-lgeotiff`),
OpenMP**. (`configure.ac` also looks for `xtiffio.h` / `geotiff/xtiffio.h`.)

```bash
# load toolchain + libs (names vary per cluster; check `module avail`)
module load gcc            # a recent g++
module load libtiff libgeotiff zlib   # or: module load gdal   (often pulls tiff/geotiff)

cd EF5_v4.3
autoreconf -fi             # regenerate configure/Makefile (or ./autogen.sh if present)
./configure                # add CPPFLAGS=-I.../include LDFLAGS=-L.../lib if headers/libs not found
make -j                    # produces the `ef5` binary
```
If `configure` can't find `xtiffio.h` or `-lgeotiff`, point it at the module's prefix:
`./configure CPPFLAGS="-I$GEOTIFF_ROOT/include" LDFLAGS="-L$GEOTIFF_ROOT/lib"`.

## 5. Smoke test after building

Sample control files live in the repo root (`sample_control_*.txt`). Lake-relevant ones:
`sample_control_with_lakes_csv.txt`, `sample_control_comprehensive_lake.txt`,
`sample_control_with_lake_calibration.txt`. Run e.g. `./bin/ef5 sample_control_with_lakes_csv.txt`
(adjust the path to wherever `make` put the binary). Confirm:
1. It builds and runs without crashing (validates the #10/#11 refactors).
2. With a lake whose storage ≤ threshold, the lake's outflow column is **non-zero** and
   varies with `klake` (validates #1).
3. Optionally convert an input to BIF with `scripts/crest_preprocess/` and confirm EF5
   reads it (validates the BIF C++ patch).

## 6. Suggested order for the HPC Claude

1. Build (Section 4). Fix any compile errors from the uncompiled edits — most likely in the
   `lakeNodes`→`lakeNode` refactor (#10) or the `LakeMap` node-index map (#11).
2. Run a lake sample (Section 5); confirm the #1/#2 behavioral expectations.
3. Implement **#3** (calibration runs lake physics) — highest scientific value.
4. Implement **#4** (engineered discharge) if the user needs dam control.
5. Leave **#8**/**#6** as documented design notes unless asked.

Pre-existing uncommitted changes were in the working tree before this work began
(`Simulator.cpp`, `LakeCaliParamConfigSection.*`, `Models.tbl`, `TempConfigSection.*`,
`docs/manual.html`, the `temperature_elevation` samples). They are the user's; this memo
only covers the lake-fix + BIF-I/O changes listed above.
