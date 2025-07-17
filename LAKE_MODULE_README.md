# EF5 Lake Module - Version 4.2.0

The EF5 Lake Module provides comprehensive lake and reservoir modeling capabilities integrated with the EF5 hydrological framework. This module supports multiple lakes, engineered discharge control, calibration, and state management.

## Features

### 1. Multiple Lake Support
- Configure multiple lakes in a single simulation
- CSV-based configuration for easy management
- Automatic unit conversion (km³/km² to m³/m²)

### 2. Engineered Discharge
- Dam-controlled outflow specification
- Time-varying discharge control via CSV files
- Integration with natural outflow modeling

### 3. Lake Calibration
- DREAM algorithm integration
- Calibrate lake parameters (klake)
- Support for observed flow accumulation

### 4. State Management
- Automatic state saving/loading
- Grid-based state files
- Restart capability from any saved state

### 5. Model Integration
- Works with all EF5 water balance models (CREST, SAC, HP, HyMOD, LAKE)
- Not limited to standalone lake model
- Backward compatibility maintained

## Configuration

### Lake CSV File Format

The `lakes.csv` file contains lake parameters with the following columns:

```csv
name,lat,lon,th_volume,area,klake,obsFam,outputts
Lake1,45.123,-122.456,1.2,50.0,24.0,5000.0,true
Lake2,45.234,-122.567,0.8,30.0,48.0,,false
Reservoir1,45.345,-122.678,2.5,100.0,12.0,15000.0,y
Lake3,45.456,-122.789,0.5,20.0,36.0,,TRUE
Lake4,45.567,-122.890,1.0,40.0,24.0,8000.0,1
```

**Column Descriptions:**
- `name`: Lake identifier (string)
- `lat`: Latitude (decimal degrees)
- `lon`: Longitude (decimal degrees)
- `th_volume`: Threshold volume (km³) - automatically converted to m³
- `area`: Surface area (km²) - automatically converted to m²
- `klake`: Retention constant (hours) - for linear reservoir outflow
- `obsFam`: Observed flow accumulation (optional, for calibration)
- `outputts`: Output time series flag (optional) - controls whether lake volume appears in gauge output files
  - Values: `true`, `y`, `1`, `TRUE`, `Y` (case insensitive)
  - Empty values (missing column or empty field) are treated as `false`
  - Default: `false` (if not specified or empty)
  - Only lakes with `outputts=true` will have their volume included in the `Lake_Vol(m^3)` column

### Engineered Discharge CSV Format

The engineered discharge file specifies dam-controlled outflow:

```csv
timestamp,Lake1,Lake2,Reservoir1
2000-01-01 00:00:00,10.5,5.2,25.0
2000-01-01 01:00:00,12.3,6.1,28.5
```

**Format:**
- First column: timestamp (YYYY-MM-DD HH:MM:SS)
- Subsequent columns: discharge values for each lake (m³/s)

## Control File Configuration

### Basic Lake Module Setup

```ini
[BASIN MyBasin]
Gauge = MyGauge
lakes_csv = lakes.csv
engineered_discharge_csv = engineered_discharge.csv

[TASK MyTask]
Model = CREST
Basin = MyBasin
LakeModule = true
```

### Lake Calibration Setup

```ini
[lake_cali_param lake_calibration]
lake_name = Lake1
klake = 1.0,100.0  # min,max values

[TASK CalibrationTask]
Model = CREST
Basin = MyBasin
Style = CALI_DREAM
LakeModule = true
lake_cali_param = lake_calibration
```

### State Saving Configuration

```ini
[TASK MyTask]
Model = CREST
Basin = MyBasin
LakeModule = true
State = ./states
State_Time = 20240115_12
Save_States = true
```

## Lake Model Physics

### Natural Outflow
- **Storage-based overflow**: When storage > threshold volume
- **Linear reservoir decay**: During dry season (storage ≤ threshold)
- **Exponential decay**: Applied to previous outflow values

### Engineered Discharge
- **Dam-controlled outflow**: Specified via CSV file
- **Time-varying control**: Different discharge rates at different times
- **Override natural outflow**: When engineered discharge is specified

### Water Balance
- **Inflow**: From upstream catchment
- **Precipitation**: Direct precipitation on lake surface
- **Evaporation**: Direct evaporation from lake surface
- **Outflow**: Natural or engineered discharge

## Output Files

### Gauge Output
When `LakeModule = true` and at least one lake has `outputts=true`, gauge output includes lake volume:
```csv
Time,Discharge(m^3 s^-1),Observed(m^3 s^-1),...,Lake_Vol(m^3)
```

**Note:** The `Lake_Vol(m^3)` column only appears if:
1. `LakeModule = true` is set in the control file
2. At least one lake in the lakes.csv file has `outputts=true` (or `y`, `1`, `TRUE`, `Y`)
3. The gauge is located at a grid cell where a lake with `outputts=true` is present

If no lakes have `outputts=true`, the `Lake_Vol` column will not appear in the gauge output files.

### State Files
- `lake_storage_YYYYMMDD_HHUU.tif`: Lake storage at specified time
- `lake_outflow_YYYYMMDD_HHUU.tif`: Lake outflow at specified time

### Gridded Output
- Lake storage and outflow maps
- Integration with existing EF5 gridded output system

## Examples

### Simple Lake Simulation
See `sample_control_with_lakes_csv.txt` for basic lake simulation.

### Lake Calibration
See `sample_control_with_lake_calibration.txt` for calibration example.

### Comprehensive Example
See `sample_control_comprehensive_lake.txt` for all features.

## File Structure

```
EF5_v4.2/
├── lakes.csv                          # Main lake configuration
├── sample_lakes_with_obsFam.csv       # Example with calibration data
├── sample_inletA_lake1.csv            # Example engineered discharge
├── sample_control_with_lakes_csv.txt  # Basic lake simulation
├── sample_control_with_lake_calibration.txt  # Calibration example
└── sample_control_comprehensive_lake.txt     # All features
```

## Version History

### Version 4.2.0
- Enhanced CSV configuration system
- Improved engineered discharge support
- Better state management
- Comprehensive documentation
- Integration with all EF5 models

### Previous Versions
- Version 1.3.3: Initial lake module implementation
- Version 1.2.3: Basic lake functionality

## Troubleshooting

### Common Issues

1. **Lake outside grid domain**: Check lat/lon coordinates in lakes.csv
2. **Missing engineered discharge**: Ensure CSV format matches expected structure
3. **Calibration not working**: Verify lake_name matches lakes.csv
4. **State loading failed**: Check state file paths and timestamps

### Error Messages

- `Lake X is outside the basic grid domain`: Adjust lat/lon coordinates
- `Failed to open lakes CSV file`: Check file path and permissions
- `Invalid lakes CSV header`: Ensure column names match expected format

## Support

For issues and questions:
1. Check the example files in this directory
2. Review the EF5 documentation
3. Check error messages in the console output
4. Verify CSV file formats match examples 