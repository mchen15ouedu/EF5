# Lake Outflow Integration with KW Routing in EF5

This document explains how lake outflow is integrated with the Kinematic Wave (KW) routing system in EF5.

## Overview

The lake outflow integration allows EF5 to model lakes and reservoirs that affect the downstream flow routing. Lakes can have either:
1. **Engineered outflow** - controlled discharge from damQ data
2. **Natural outflow** - calculated based on storage overflow (storage > th_volume) + linear reservoir outflow (storage ≤ th_volume)

## Architecture

### Components

1. **LakeModel** - Handles individual lake water balance and outflow calculation
2. **LakeConfigSection** - Configuration for lake parameters
3. **Simulator** - Integrates lake outflow with KW routing
4. **KWRoute** - Modified to handle lake outflow as additional input

### Integration Flow

```
Water Balance → Lake Models → Update Outflows → Add to Routing → KW Routing → Discharge
```

## Configuration

### 1. Control File Settings

Add these lines to your EF5 control file:

```
# Enable lake module
LAKEMODULE true

# Optional: Specify engineered discharge file
LAKEOUTFLOWFILE "engineered_discharge.csv"
```

### 2. Lakes Configuration File (lakes.csv)

Create a `lakes.csv` file in your project directory:

```csv
name,lat,lon,th_volume,area,klake
Lake1,45.123,-122.456,1.0,0.5,24.0
Lake2,45.234,-122.567,2.0,0.75,48.0
Reservoir1,45.345,-122.678,3.0,1.0,12.0
```

**Columns:**
- `name`: Unique lake name (used in engineered discharge file)
- `lat`, `lon`: Lake coordinates
- `th_volume`: Threshold storage volume (km³) - converted to m³ internally
- `area`: Lake surface area (km²) - converted to m² internally
- `klake`: Linear reservoir retention constant K (hours) - controls dry season outflow decay

**Note:** 
- Maximum depth can be calculated as `max_depth = th_volume / area`
- Initial storage is set to `th_volume` (lakes start full)
- Units are automatically converted: km³ → m³ (×1e9), km² → m² (×1e6)
- This makes input much more user-friendly than using large m³/m² values
- Lake names must match the column headers in the engineered discharge file

### 3. Engineered Discharge File (Optional)

If using engineered outflow, create `engineered_discharge.csv`:

```csv
timestamp,Lake1,Lake2,Reservoir1
20200101_00,50.0,30.0,100.0
20200101_01,45.0,35.0,95.0
20200101_02,40.0,40.0,90.0
```

**Format:**
- First column: `timestamp` (time identifier)
- Subsequent columns: Lake names (must match names in lakes.csv)
- Values: Discharge rates in m³/s
- Missing values or missing lakes: Will use natural outflow calculation

## How It Works

### 1. Lake Initialization

During simulator initialization:
- Lakes are loaded from `lakes.csv`
- Each lake is mapped to the nearest grid node
- Lake models are created with initial storage set to `th_volume`

### 2. Water Balance Integration

For each timestep:
- Lake inflow is estimated from upstream flow components (FF + SF + BF)
- Precipitation and evaporation are applied to the lake
- Water balance is updated: `Storage += Inflow + Precip - Evap - Outflow`

### 3. Outflow Calculation

**Engineered Outflow (if damQ data available):**
- Outflow is directly set from the engineered discharge file
- Water balance is updated accordingly

**Natural Outflow (if no damQ data):**

**Wet Season (storage > th_volume):**
- Storage-based overflow: `if (storage > th_volume)`
- Outflow = `(storage - th_volume) / timestep`
- Storage is capped at `th_volume`

**Dry Season (storage ≤ th_volume):**
- Linear reservoir outflow using exponential decay
- Outflow = `S/K * exp(-dt/K)`
- Where:
  - `S` = current storage (m³)
  - `K` = retention constant (hours, converted to seconds)
  - `dt` = timestep (seconds)
- When storage = 0, outflow = 0 (naturally)

### 4. Linear Reservoir Theory

The linear reservoir approach models outflow as proportional to storage:

**Basic Equation:**
```
O(t) = S(t) / K
```

**Exponential Decay (when inflow = 0):**
```
S(t) = S₀ * exp(-t/K)
O(t) = (S₀/K) * exp(-t/K)
```

**Discrete Time Implementation:**
```
O_{t+1} = O_t * exp(-dt/K)  (only when storage ≤ th_volume)
```

This ensures:
- **Smooth outflow decay** during dry periods (storage ≤ th_volume)
- **Natural termination** when storage reaches zero
- **Physical realism** - lakes don't instantly stop releasing water
- **Memory effect** - previous outflow affects current outflow (only in dry season)
- **No interference** - exponential decay doesn't affect overflow conditions

### 5. KW Routing Integration

Lake outflow is added to the KW routing system:
- Outflow is converted from m³/s to mm/h for routing compatibility
- Added to the fast flow component at the lake's grid node
- KW routing processes the combined flow through the channel network

## KW Routing Details

### Current KW Architecture

The KW routing system handles three flow components:
1. **Fast Flow (Overland)** - Surface runoff
2. **Interflow** - Subsurface flow
3. **Baseflow** - Groundwater flow

### Lake Integration Points

1. **Input Integration**: Lake outflow is added to fast flow before routing
2. **Channel Routing**: Combined flow is routed through the channel network
3. **Downstream Propagation**: Lake outflow affects downstream discharge

### Routing Equations

The KW routing uses kinematic wave equations:
- **Overland**: `∂q/∂t + αβq^(β-1) ∂q/∂x = r`
- **Channel**: `∂Q/∂t + αβQ^(β-1) ∂Q/∂x = q`

Where:
- `q` = overland flow per unit width
- `Q` = channel discharge
- `α`, `β` = routing parameters
- `r` = lateral inflow (including lake outflow)

## Example Usage

### 1. Basic Setup

```bash
# Create lakes.csv with user-friendly units (km³, km²) and linear reservoir parameters
echo "name,lat,lon,th_volume,area,klake" > lakes.csv
echo "Lake1,45.123,-122.456,1.0,0.5,24.0" >> lakes.csv
echo "Lake2,45.234,-122.567,2.0,0.75,48.0" >> lakes.csv
echo "Reservoir1,45.345,-122.678,3.0,1.0,12.0" >> lakes.csv

# Run EF5 with lake module enabled
./ef5 sample_control_with_lakes.txt
```

### 2. With Engineered Discharge

```bash
# Create engineered discharge file
echo "timestamp,Lake1,Lake2,Reservoir1" > engineered_discharge.csv
echo "20200101_00,50.0,30.0,100.0" >> engineered_discharge.csv
echo "20200101_01,45.0,35.0,95.0" >> engineered_discharge.csv
echo "20200101_02,40.0,40.0,90.0" >> engineered_discharge.csv

# Run EF5
./ef5 sample_control_with_lakes.txt
```

## Output

The lake integration affects:
- **Discharge output**: Downstream flow includes lake outflow
- **Water balance**: Lake storage and outflow are tracked
- **Routing**: Modified flow patterns due to lake regulation

## Calibration

### Lake Parameters

1. **Th Volume**: Controls when overflow occurs and initial storage
   - Set based on observed lake capacity
   - Affects flood control and storage behavior
   - Maximum depth = th_volume / area
   - Initial storage = th_volume (lakes start full)

2. **Lake Area**: Used for precipitation/evaporation calculations
   - Set based on lake surface area
   - Affects water balance accuracy

3. **Retention Constant (K)**: Controls dry season outflow decay
   - **Small K (6-12 hours)**: Fast decay, quick response to dry conditions
   - **Medium K (24-48 hours)**: Moderate decay, typical for many lakes
   - **Large K (72+ hours)**: Slow decay, lakes maintain outflow longer
   - **Calibration**: Match observed outflow decay during dry periods

### Lake Calibration with DREAM

The `klake` parameter can be automatically calibrated using the DREAM algorithm:

#### 1. Lake Calibration Parameter File

Create a lake calibration parameter file (e.g., `lake_cali_param.txt`):

```
LAKE_CALI_PARAM "lake_cali_example"

# Gauge for calibration
gauge "sample_gauge"

# Lake parameters for calibration
# Format: parameter_name=min_value,max_value,initial_value
klake=6.0,72.0,24.0
```

#### 2. Control File Configuration

Add lake calibration to your control file:

```
# Calibration parameter sets
CALI_PARAM "sample_cali_params"
ROUTING_CALI_PARAM "sample_routing_cali_params"
LAKE_CALI_PARAM "lake_cali_example"

# Enable lake module
LAKEMODULE true
```

#### 3. Calibration Process

The DREAM algorithm will:
- Sample `klake` values within the specified range (6.0-72.0 hours)
- Run simulations with different `klake` values
- Evaluate model performance against observed discharge
- Find optimal `klake` value that minimizes objective function

#### 4. Calibration Output

The calibration output file will include:
- Optimized `klake` value
- Parameter sensitivity analysis
- Convergence statistics

### KW Routing Parameters

Lake outflow may require adjustment of KW parameters:
- **ALPHA**: Wave speed parameter
- **BETA**: Flow-depth relationship
- **TH**: Channel threshold

## Advantages of Combined Approach

1. **Realistic Wet Season**: Storage-based overflow for flood conditions
2. **Realistic Dry Season**: Linear reservoir for natural outflow decay
3. **Smooth Transitions**: No sudden jumps between wet/dry modes
4. **Natural Termination**: Outflow naturally reaches zero when storage is empty
5. **Physical Basis**: Both approaches have hydrological foundations
6. **Easy Calibration**: Clear parameters for different conditions

## Limitations

1. **Single Lake per Node**: Only one lake can be assigned to each grid node
2. **Simplified Geometry**: Lakes are treated as point sources
3. **No Backwater Effects**: Upstream effects of lakes are not modeled
4. **Parameter Sensitivity**: Linear reservoir parameters need careful calibration

## Future Enhancements

1. **Multiple Lakes per Node**: Support for multiple lakes at same location
2. **Complex Geometry**: Full lake geometry and bathymetry
3. **Backwater Effects**: Upstream influence of lake levels
4. **Lake-lake Routing**: Routing between connected lakes
5. **Seasonal Parameters**: Different K values for wet/dry seasons

## Troubleshooting

### Common Issues

1. **No Lakes Found**: Check `lakes.csv` file exists and format is correct
2. **Lake Not at Expected Location**: Verify lat/lon coordinates match grid
3. **Unrealistic Outflow**: Check th_volume and retention constant values
4. **Routing Instability**: Check KW parameters and timestep size
5. **Too Much Dry Season Flow**: Increase retention constant K
6. **Too Little Dry Season Flow**: Decrease retention constant K

### Debug Output

Enable debug logging to see:
- Lake initialization messages
- Outflow calculations (wet vs dry season)
- Linear reservoir decay calculations
- Integration with routing
- Water balance updates 

## Lake Module Integration with EF5

## Overview
The EF5 hydrological model now includes a comprehensive lake module that simulates lake storage, inflow, outflow, and water balance. The module supports both simulation and calibration modes, with the ability to calibrate individual lakes separately.

## Lake Configuration

### lakes.csv Format
The lake configuration file (`lakes.csv`) contains lake-specific parameters:

```csv
Name,Latitude,Longitude,ThresholdVolume_km3,Area_km2,Klake_hours
Lake1,45.5,-122.5,1.2,50.0,24.0
Lake2,45.3,-122.3,0.8,30.0,48.0
Lake3,45.1,-122.1,0.5,20.0,72.0
```

**Parameters:**
- `Name`: Lake identifier
- `Latitude, Longitude`: Lake coordinates
- `ThresholdVolume_km3`: Storage threshold for overflow (km³)
- `Area_km2`: Lake surface area (km²)
- `Klake_hours`: Linear reservoir retention constant (hours)

### Control File Configuration
```ini
[BASIN TestBasin]
Gauge=test_gauge
LakeListFile=lakes.csv
DamQ=engineered_discharge.csv  # Optional: engineered discharge time series
```

## Lake Simulation

### Water Balance Components
1. **Inflow**: From upstream grid cells (FAM-1 logic or custom inlets)
2. **Precipitation**: Direct precipitation on lake surface
3. **Evaporation**: Estimated from PET at lake location
4. **Outflow**: Calculated based on storage conditions

### Outflow Logic
- **Overflow**: When storage > threshold volume
  - Outflow = (storage - threshold) / timestep
- **Linear Reservoir**: When storage ≤ threshold volume
  - Outflow = storage / (klake × 3600) [m³/s]
  - Exponential decay: O_new = O_prev × exp(-dt/(klake × 3600))
- **Engineered Discharge**: If provided in DamQ file, overrides natural outflow

## Lake Calibration

### Per-Lake Calibration System
The lake module supports **individual lake calibration**, allowing users to calibrate one lake at a time from upstream to downstream.

### Calibration Configuration
```ini
[LAKECALIPARAMS lake_calibration]
LakeName=Lake1              # Specify which lake to calibrate
klake=0.1,100.0             # Min and max klake values (hours)
```

**Note:** The initial value for calibration is taken from the lakes.csv file for the specified lake.

### Calibration Workflow
1. **Start with upstream lakes**: Calibrate the most upstream lake first
2. **Sequential calibration**: Move downstream, calibrating one lake at a time
3. **Parameter preservation**: Original lakes.csv values are preserved; calibration only affects the specified lake during the calibration run
4. **Output results**: Calibrated parameters are output to the calibration results file

### Calibration Output
The calibration process outputs optimized parameters in the format:
```
[Lake]
# Calibrated Lake: Lake1
# Parameter	Best_Value	Min	Max	Initial
klake=32.5
```

**Note:** The Initial value shown is the lakes.csv value for the calibrated lake.

### Manual Calibration Process
1. **First run**: Calibrate Lake1 (upstream)
   - Use `LakeName=Lake1` in control file
   - Run calibration
   - Note optimized klake value
2. **Second run**: Calibrate Lake2 (downstream)
   - Use `LakeName=Lake2` in control file
   - Update lakes.csv with Lake1's optimized klake value
   - Run calibration
3. **Continue**: Repeat for all lakes in downstream order

## Engineered Discharge (Optional)

### DamQ File Format
```csv
Time,Lake1,Lake2,Lake3
2010-01-01 00:00:00,100.5,50.2,25.1
2010-01-01 01:00:00,98.3,48.9,24.8
...
```

When DamQ is provided, the lake module uses engineered discharge values instead of calculating natural outflow.

## Output Files

### Lake Time Series
For each lake, a time series file is generated: `ts.{LakeName}.lake.csv`
```csv
time,storage,inflow,outflow,precipitation,evaporation
2010-01-01 00:00:00,1.200000,50.5,25.2,0.0,0.1
2010-01-01 01:00:00,1.225300,48.3,24.8,0.0,0.1
...
```

**Units:**
- Storage: km³
- Flows: m³/s
- Precipitation/Evaporation: mm

## Integration with EF5 Workflow

### Simulation Mode
- Lakes are initialized with parameters from lakes.csv
- Water balance is calculated at each timestep
- Lake outflow is added to downstream routing

### Calibration Mode
- Only the specified lake's klake parameter is calibrated
- Other lakes retain their lakes.csv values
- Calibration results include lake-specific information

## Best Practices

1. **Upstream to downstream**: Always calibrate lakes in upstream order
2. **Parameter ranges**: Set reasonable min/max values for klake (typically 0.1-100 hours)
3. **Initial values**: Use physically reasonable initial values
4. **Validation**: Verify calibrated parameters make physical sense
5. **Documentation**: Keep track of calibrated values for each lake

## Example Files

- `sample_control_with_lakes.txt`: Basic lake simulation
- `sample_control_with_lake_calibration.txt`: Lake calibration example
- `lakes.csv`: Sample lake configuration
- `engineered_discharge.csv`: Sample engineered discharge data 