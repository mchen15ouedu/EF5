# Lake Module Integration for EF5 v3.3b

This document describes the lake module integration that has been added to EF5 v3.3b with surgical precision, maintaining full backward compatibility.

## Overview

The lake module provides two approaches to specify lakes in EF5:

1. **CSV File Reading**: Recommended approach for reading multiple lakes from CSV files
2. **Individual Lake Sections**: Recommended approach for single lakes using [LAKE] sections

## Lake CSV File Format

### Lakes CSV (`lakes.csv`)
The lakes CSV file should have the following format:

```csv
name,lat,lon,th_volume,area,klake
Lake1,45.123,-122.456,1.2,50.0,24.0
Lake2,45.234,-122.567,0.8,30.0,48.0
Reservoir1,45.345,-122.678,2.5,100.0,12.0
```

**Columns:**
- `name`: Lake name (string)
- `lat`: Latitude (decimal degrees)
- `lon`: Longitude (decimal degrees)
- `th_volume`: Theoretical volume (cubic meters)
- `area`: Surface area (square meters)
- `klake`: Retention constant (hours, optional, default: 24.0)

### Engineered Discharge CSV (`engineered_discharge.csv`)
The engineered discharge CSV file should have the following format:

```csv
timestamp,Lake1,Lake2,Reservoir1
20240101-000000,50.0,30.0,100.0
20240101-010000,45.0,35.0,95.0
```

**Columns:**
- `timestamp`: Time stamp (format: YYYYMMDD-HHMMSS)
- `Lake1`, `Lake2`, etc.: Discharge values for each lake (cubic meters per second)

## Configuration

### Basin Section Configuration

#### Method 1: CSV File Reading

Add the following lines to your basin section to enable CSV reading:

```ini
[basin basin1]
gauge: gauge1
gauge: gauge2
lakes_csv: lakes.csv
engineered_discharge_csv: engineered_discharge.csv
```

**Parameters:**
- `lakes_csv`: Path to the lakes CSV file
- `engineered_discharge_csv`: Path to the engineered discharge CSV file (optional)

#### Method 2: Individual Lake Sections

Define lakes individually and reference them in the basin:

```ini
[LAKE Reservoir1]
Lat = 35.5
Lon = -97.8
Area = 0.1         ; km² (converted to m² internally)
ThVolume = 0.5    ; km³ (converted to m³ internally)
Klake = 24.0      ; hours

[basin basin1]
gauge: gauge1
lake: Reservoir1
```

**Lake Section Parameters:**
- `Lat`: Latitude (decimal degrees)
- `Lon`: Longitude (decimal degrees)
- `Area`: Surface area (km², converted to m² internally)
- `ThVolume`: Theoretical volume (km³, converted to m³ internally)
- `Klake`: Retention constant (hours)
- `obsFam`: Observed flow accumulation (optional)

### Task Configuration

For lake model simulation, use:

```ini
[task task1]
style: simu
model: lake
basin: basin1
precip: precip1
pet: pet1
output: output
time_begin: 2024-01-01 00:00:00
time_end: 2024-01-31 23:59:59
timestep: 1H
```

## Usage Examples

### Example 1: Multiple Lakes from CSV
```ini
[basin basin1]
gauge: gauge1
lakes_csv: lakes.csv
engineered_discharge_csv: engineered_discharge.csv

[task task1]
style: simu
model: lake
basin: basin1
precip: precip1
pet: pet1
output: output
time_begin: 2024-01-01 00:00:00
time_end: 2024-01-31 23:59:59
timestep: 1H
```

### Example 2: Single Lake using Lake Section
```ini
[LAKE Reservoir1]
Lat = 35.5
Lon = -97.8
Area = 0.1
ThVolume = 0.5
Klake = 24.0

[basin basin1]
gauge: gauge1
lake: Reservoir1

[task task1]
style: simu
model: lake
basin: basin1
precip: precip1
pet: pet1
output: output
time_begin: 2024-01-01 00:00:00
time_end: 2024-01-31 23:59:59
timestep: 1H
```

## Implementation Details

### Key Files Modified
- `src/Models.tbl`: Added lake model definition
- `src/Model.h`: Added lake parameter enums and arrays
- `src/Model.cpp`: Added lake parameter strings and counts
- `src/TaskConfigSection.h/.cpp`: Added lake parameter getters and processing
- `src/Simulator.h/.cpp`: Added lake model integration
- `src/BasicGrids.h/.cpp`: Added lake parameter handling in basin carving
- `src/Config.cpp`: Added lake configuration support
- `src/BasinConfigSection.h/.cpp`: Added CSV reading functionality

### Lake Model Selection
When `model: lake` is specified:
1. If lakes are defined in the basin CSV, the first lake is used
2. If no lakes are defined, a default lake model is created
3. Engineered discharge data is passed to the lake model if available

### Backward Compatibility
- All existing EF5 v3.3b functionality is preserved
- Lake module is optional and only activated when `model: lake` is specified
- Lake parameters are now defined in CSV files instead of parameter sets

## Notes

- The current implementation uses the first lake from the CSV file
- Future enhancements could support multiple lakes simultaneously
- Engineered discharge data is read from the first timestamp in the CSV
- All lake parameters are defined in the CSV file
- The lake model integrates seamlessly with existing routing and snow modules 