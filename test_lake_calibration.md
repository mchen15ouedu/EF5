# Lake Calibration Testing Guide

## Overview
This document describes how to test the new per-lake calibration system in EF5.

## Test Setup

### 1. Create Test Data Files

**lakes.csv:**
```csv
Name,Latitude,Longitude,ThresholdVolume_km3,Area_km2,Klake_hours
Lake1,45.5,-122.5,1.2,50.0,24.0
Lake2,45.3,-122.3,0.8,30.0,48.0
Lake3,45.1,-122.1,0.5,20.0,72.0
```

**engineered_discharge.csv (optional):**
```csv
Time,Lake1,Lake2,Lake3
2010-01-01 00:00:00,100.5,50.2,25.1
2010-01-01 01:00:00,98.3,48.9,24.8
```

### 2. Test Control Files

**Test 1: Calibrate Lake1**
```ini
[BASIN TestBasin]
Gauge=test_gauge
LakeListFile=lakes.csv

[TASK Lake1Calibration]
Model=crest
Routing=linear
Snow=snow17
Style=cali_dream
StartDate=2010-01-01 00:00:00
EndDate=2010-12-31 23:59:59
TimeStep=1h
WarmupDays=30
Basin=TestBasin
Gauge=test_gauge
ObjectiveFunction=nse
ObjectiveGoal=minimize

[LAKECALIPARAMS lake_calibration]
LakeName=Lake1
klake=0.1,100.0

[EXECUTE]
Task=Lake1Calibration
```

**Test 2: Calibrate Lake2**
```ini
[BASIN TestBasin]
Gauge=test_gauge
LakeListFile=lakes.csv

[TASK Lake2Calibration]
Model=crest
Routing=linear
Snow=snow17
Style=cali_dream
StartDate=2010-01-01 00:00:00
EndDate=2010-12-31 23:59:59
TimeStep=1h
WarmupDays=30
Basin=TestBasin
Gauge=test_gauge
ObjectiveFunction=nse
ObjectiveGoal=minimize

[LAKECALIPARAMS lake_calibration]
LakeName=Lake2
klake=0.1,100.0

[EXECUTE]
Task=Lake2Calibration
```

## Expected Behavior

### 1. Lake Initialization
- All lakes should be initialized with their lakes.csv values
- Lake1: klake = 24.0 hours (from lakes.csv)
- Lake2: klake = 48.0 hours (from lakes.csv)
- Lake3: klake = 72.0 hours (from lakes.csv)
- Calibration initial values are taken from lakes.csv for the specified lake

### 2. Calibration Process
- **Test 1**: Only Lake1's klake parameter should be calibrated
- **Test 2**: Only Lake2's klake parameter should be calibrated
- Other lakes should retain their original values

### 3. Output Verification
- Calibration results should specify which lake was calibrated
- Output format should include lake name and parameter information
- Original lakes.csv values should be preserved

## Test Scenarios

### Scenario 1: Single Lake Calibration
1. Run Test 1 (Lake1 calibration)
2. Verify only Lake1's klake is modified during calibration
3. Check output file contains "Calibrated Lake: Lake1"

### Scenario 2: Sequential Calibration
1. Run Test 1 and note optimized Lake1 klake value
2. Update lakes.csv with Lake1's optimized value
3. Run Test 2 (Lake2 calibration)
4. Verify Lake1 retains optimized value, Lake2 gets calibrated

### Scenario 3: Invalid Lake Name
1. Use non-existent lake name in LakeCaliparams
2. Verify error message: "Lake calibration specified lake 'InvalidLake' not found"

### Scenario 4: Missing Lake Name
1. Remove LakeName from LakeCaliparams section
2. Verify error message: "Lake calibration section must specify a lake_name!"

## Validation Checklist

- [ ] Lakes initialize with correct lakes.csv values
- [ ] Only specified lake's klake parameter is calibrated
- [ ] Other lakes retain original values during calibration
- [ ] Calibration output includes lake name information
- [ ] Error handling works for invalid lake names
- [ ] Error handling works for missing lake names
- [ ] Sequential calibration preserves previous results

## Troubleshooting

### Common Issues
1. **Lake not found**: Check lake name spelling in control file
2. **No calibration**: Ensure LakeCaliparams section is properly configured
3. **Wrong lake calibrated**: Verify LakeName parameter is correct

### Debug Output
Enable debug logging to see:
- Which lake is being calibrated
- Parameter values during calibration
- Lake model initialization details 