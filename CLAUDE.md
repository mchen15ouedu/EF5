# EF5 — project guide for Claude

EF5 (Ensemble Framework For Flash Flood Forecasting): distributed hydrologic model in C++,
autotools build, binary `ef5`. This fork adds a **gridded lake/reservoir module** and a
**fast input-I/O** path (BIF + a Python preprocessor).

## 👉 Active work — READ THIS FIRST
**[CLAUDE_HANDOFF.md](CLAUDE_HANDOFF.md)** is a self-contained handoff memo describing
exactly what was changed, what was tested, and what remains (lake calibration wiring,
engineered-discharge feature, etc.). It was written on a machine **without a compiler**, so
the C++ edits there are **unbuilt** — building and testing on this machine is step one.

## Build
Autotools. Deps: zlib, libtiff, libgeotiff, OpenMP.
```bash
autoreconf -fi && ./configure && make -j
```
If headers/libs aren't found, pass `CPPFLAGS=-I.../include LDFLAGS=-L.../lib` to `./configure`
(see CLAUDE_HANDOFF.md §4 for module/cluster notes).

## Layout
- `src/` — model source. Lake module: `LakeModel.*`, `LakeMap.*`, `LakeConfigSection.*`,
  `BasinConfigSection.cpp` (lake CSV reader), wired in `Simulator.cpp`.
- Grid I/O: `BifGrid.cpp` (fast binary, **preferred**), `TifGrid.cpp` (GeoTIFF, slow),
  `AscGrid.cpp`, `BasicGrids.cpp` (loads DEM/DDM/FAM).
- `scripts/crest_preprocess/` — standalone Python tool: any raster (netCDF/GRIB/GeoTIFF/
  HDF/BIL/…) → EF5 BIF. Not part of the C++ build. See its README.
- `sample_control_*.txt` — runnable example control files (incl. lake examples).

## Conventions
- Grids are row-major `data[row][col]`, **row 0 = north**, square cells, single `cellSize`.
- The fast forcing format is **BIF**; convert inputs with `scripts/crest_preprocess/`.
- Commit/push only when the user asks.
