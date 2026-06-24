# Inverse Kinematic Wave Routing

This script back-calculates Q (streamflow) values at all pixels given the Q at the basin outlet.
It builds the routing network from DEM, Flow Direction, and Flow Accumulation data, and processes
multiple timesteps with runoff data.

## Overview

The inverse routing works by:
1. **Building network from DEM data**: Reads DEM, Flow Direction (DDM), and Flow Accumulation (FAM) GeoTIFF files
2. **Creating node structure**: Builds routing network with upstream/downstream connections
3. **Traversing from outlet upstream**: Uses BFS to process nodes from outlet to headwaters
4. **Solving inverse kinematic wave equation**: Calculates incoming Q from upstream at each node
5. **Accounting for local runoff**: Includes local rainfall/runoff contribution
6. **Outputting GeoTIFF files**: Saves one GeoTIFF per timestep

## Requirements

```bash
pip install numpy rasterio
```

## Usage

Two options for providing configuration:

### Option 1: CSV Config File (Recommended - Easy to Edit)

```bash
python scripts/inverse_kw_routing.py --config <config.csv> <outlet_q_file> <runoff_dir> <output_dir>
```

**Example:**
```bash
python scripts/inverse_kw_routing.py --config config.csv outlet_q.csv runoff/ output/
```

**Config File Location:**
- The config file can be **anywhere** - provide the full path or relative path
- No specific naming requirement - any `.csv` filename works (e.g., `config.csv`, `routing_config.csv`, `my_config.csv`)
- File paths **inside** the config file (like `dem_file`, `flow_dir_file`) are relative to your **current working directory** when you run the script, not relative to the config file location

**Example with different paths:**
```bash
# Config file in same directory
python scripts/inverse_kw_routing.py --config config.csv outlet_q.csv runoff/ output/

# Config file in different directory
python scripts/inverse_kw_routing.py --config ../configs/my_config.csv outlet_q.csv runoff/ output/

# Config file with absolute path
python scripts/inverse_kw_routing.py --config /path/to/config.csv outlet_q.csv runoff/ output/
```

### Option 2: Command-Line Arguments (Quick Testing)

```bash
python scripts/inverse_kw_routing.py <dem_file> <flow_dir_file> <flow_accum_file> <outlet_x> <outlet_y> <outlet_q_file> <runoff_dir> <output_dir> [alpha0] [alpha] [beta] [stepHours] [channel_threshold]
```

**Example (with lat/lon):**
```bash
python scripts/inverse_kw_routing.py data/dem.tif data/flow_dir.tif data/flow_accum.tif -98.123 39.654 outlet_q.csv runoff/ output/ 1.5 2.0 0.6 1.0 100.0
```

**Example (with grid coordinates - backward compatibility):**
```bash
python scripts/inverse_kw_routing.py data/dem.tif data/flow_dir.tif data/flow_accum.tif 150 200 outlet_q.csv runoff/ output/ 1.5 2.0 0.6 1.0 100.0
```

### Arguments

**For CSV config:**
- `--config` or `-c`: CSV configuration file
- `outlet_q_file`: CSV file with outlet Q values (timestep, Q_m3s)
- `runoff_dir`: Directory containing runoff GeoTIFF files
- `output_dir`: Output directory for Q GeoTIFF files

**For command-line:**
- `dem_file`: Path to DEM GeoTIFF
- `flow_dir_file`: Path to Flow Direction GeoTIFF
- `flow_accum_file`: Path to Flow Accumulation GeoTIFF
- `outlet_lon`, `outlet_lat`: Outlet coordinates (longitude, latitude in decimal degrees) - **Preferred**
- OR `outlet_x`, `outlet_y`: Outlet grid coordinates (pixel coordinates) - for backward compatibility
- `outlet_q_file`: CSV file with outlet Q values
- `runoff_dir`: Directory containing runoff GeoTIFF files
- `output_dir`: Output directory for Q GeoTIFF files
- `[alpha0]`: Overland routing coefficient (optional, default: 1.5)
- `[alpha]`: Channel routing coefficient (optional, default: 2.0)
- `[beta]`: Channel routing exponent (optional, default: 0.6)
- `[stepHours]`: Timestep in hours (optional, default: 1.0)
- `[channel_threshold]`: Flow accumulation threshold (optional, default: 100.0)

## Configuration File Format (CSV)

The CSV configuration file is easy to edit in Excel or any spreadsheet program. It should have two columns: `parameter` and `value`.

**Example `config.csv`:**

```csv
parameter,value
dem_file,data/dem.tif
flow_dir_file,data/flow_direction.tif
flow_accum_file,data/flow_accumulation.tif
outlet_x,150
outlet_y,200
alpha0,1.5
alpha,2.0
beta,0.6
stepHours,1.0
channel_threshold,100.0
```

### Configuration Parameters

**Required:**
- `dem_file`: Path to DEM GeoTIFF file
- `flow_dir_file`: Path to Flow Direction GeoTIFF (ESRI encoding: 1=E, 2=NE, 3=N, 4=NW, 5=W, 6=SW, 7=S, 8=SE)
- `flow_accum_file`: Path to Flow Accumulation GeoTIFF
- `outlet_lon`: Outlet longitude (decimal degrees) - **Preferred**
- `outlet_lat`: Outlet latitude (decimal degrees) - **Preferred**

**Note on Outlet Location:**
When using lat/lon coordinates, the script automatically searches nearby pixels (up to ~20km or 50 pixels) to find the location with the **highest flow accumulation**. This handles projection displacement issues where the exact lat/lon might not align perfectly with the grid cell center. The search uses a spiral pattern checking 8 directions, similar to the GaugeMap implementation in EF5.

**Alternative (backward compatibility):**
- `outlet_x`: Grid X coordinate of outlet (pixel coordinate) - if lat/lon not provided
- `outlet_y`: Grid Y coordinate of outlet (pixel coordinate) - if lat/lon not provided

**Optional (with defaults):**
- `alpha0`: Overland routing coefficient (default: 1.5)
- `alpha`: Channel routing coefficient (default: 2.0)
- `beta`: Channel routing exponent (default: 0.6)
- `stepHours`: Outlet Q timestep in hours (default: 1.0)
- `channel_threshold`: Flow accumulation threshold for channel cells (default: 100.0)
- `runoff_timestep_hours`: Runoff data timestep in hours (default: auto-detect from files)

**Note:** 
- You can easily edit this CSV file in Excel, LibreOffice Calc, or any text editor
- Keep the header row (`parameter,value`) and use commas to separate columns
- File paths in the config (like `dem_file`, `flow_dir_file`) should be relative to where you run the script, or use absolute paths

## Outlet Q File Format

CSV file with outlet Q values for each timestep:

```csv
timestep,Q_m3s
20230101_0000,100.5
20230101_0100,105.2
20230101_0200,98.3
...
```

Or alternative format:

```csv
time,Q
20230101_0000,100.5
20230101_0100,105.2
...
```

## Runoff Data Format

Runoff files should be GeoTIFF files named `runoff_<timestep>.tif` in the runoff directory.
Values should be in mm/timestep. The script will automatically match timesteps from the outlet Q file.

Example:
- `runoff/runoff_20230101_0000.tif`
- `runoff/runoff_20230101_0100.tif`
- etc.

If runoff files are not found, the script will proceed without local runoff (assumes all Q comes from upstream routing).

## Algorithm Details

### Inverse Kinematic Wave Equation

The forward kinematic wave equation is:
```
Q = α * q^β
```

Where:
- Q = discharge (m³/s)
- q = flow per unit width (cms/m for overland, cms for channel)
- α = routing coefficient
- β = routing exponent

For **overland flow**:
- Q = q * horLen
- Inverse: q = Q / horLen

For **channel flow**:
- Q is directly in cms
- Inverse: q = (Q / α)^(1/β)

### Back-Calculation Process

1. **Start at outlet**: Set Q[outlet] = outlet_q
2. **For each processed node**:
   - Find all upstream nodes
   - Calculate incoming Q from upstream (inverse routing)
   - Distribute Q to upstream nodes (by area or equally)
3. **Traverse upstream**: Use BFS to process all nodes

### Distribution Methods

- **By Contributing Area**: Q distributed proportionally to upstream contributing areas
- **Equal Distribution**: If no area info, Q divided equally among upstream nodes

## Flow Direction Encoding

The script expects ESRI flow direction encoding (matching EF5):
- 1 = East
- 2 = Northeast
- 3 = North
- 4 = Northwest
- 5 = West
- 6 = Southwest
- 7 = South
- 8 = Southeast

## Network Building

The script automatically:
1. Reads DEM, Flow Direction, and Flow Accumulation GeoTIFF files
2. Creates nodes for all valid grid cells
3. Builds upstream/downstream connections based on flow direction
4. Calculates contributing areas by accumulating upstream areas
5. Identifies channel cells based on flow accumulation threshold

## Temporal Resolution Handling

The script handles different temporal resolutions between outlet Q and runoff data:

### Automatic Timestep Matching

1. **Exact Match**: First tries to find runoff file with exact timestep name
2. **Nearest Match**: If exact match not found, finds nearest available runoff timestep
3. **Auto-Detection**: Automatically detects runoff timestep resolution from available files

### Example: Hourly Runoff, 15-minute Outlet Q

If your outlet Q is 15-minute (0.25 hours) but runoff is hourly (1.0 hours):

1. Set `stepHours=0.25` in config (outlet Q timestep)
2. Set `runoff_timestep_hours=1.0` in config (optional - will auto-detect)
3. The script will:
   - Process each 15-minute outlet Q timestep
   - Use the nearest hourly runoff file for each timestep
   - Multiple 15-minute timesteps will use the same hourly runoff value

**Example:**
- Outlet Q: `20230101_0000`, `20230101_0015`, `20230101_0030`, `20230101_0045`
- Runoff files: `runoff_20230101_0000.tif`, `runoff_20230101_0100.tif`
- Result: All 15-min timesteps in 00:00-00:59 use `runoff_20230101_0000.tif`

### Runoff File Naming

Runoff files should be named: `runoff_<timestep>.tif`

The timestep format should match your outlet Q file format (e.g., `YYYYMMDD_HHMM`).

## Limitations

1. **Simplified Inverse**: The current implementation uses simplified inverse equations. For more accuracy, you may need to solve the full implicit equation backwards.

2. **Geographic Projection**: The horizontal length calculation is simplified. For accurate results, the script should account for actual latitude in geographic projections.

3. **Steady State Assumption**: Assumes steady-state conditions within each timestep (no temporal variation during the timestep).

4. **Runoff Interpolation**: Currently uses nearest-neighbor matching. For better accuracy with mismatched timesteps, temporal interpolation could be added.

## Output

The script generates one GeoTIFF file per timestep in the output directory:
- `output/Q_20230101_0000.tif`
- `output/Q_20230101_0100.tif`
- etc.

Each GeoTIFF contains Q values (m³/s) at each grid cell, with nodata values for cells not in the routing network.

## Future Improvements

1. **Full Inverse Solution**: Implement complete inverse of implicit finite difference scheme
2. **Better Geographic Projection**: Account for latitude in horizontal length calculations
3. **State Persistence**: Track routing states across timesteps
4. **Validation Tools**: Compare back-calculated Q with forward-calculated Q
5. **Parallel Processing**: Process multiple timesteps in parallel

