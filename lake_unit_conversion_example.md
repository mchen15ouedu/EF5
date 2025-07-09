# Lake Unit Conversion Example

This document demonstrates how EF5 automatically handles unit conversion for lake parameters, making input much more user-friendly.

## Unit Conversion Overview

EF5 automatically converts user-friendly units to internal units:

| Parameter | User Input | Internal Storage | Conversion Factor |
|-----------|------------|------------------|-------------------|
| Area | km² | m² | ×1,000,000 |
| ThVolume | km³ | m³ | ×1,000,000,000 |
| Klake | hours | hours | ×1 (no conversion) |

## Example 1: Using lakes.csv

**lakes.csv:**
```csv
name,lat,lon,th_volume,area,klake
Lake1,45.123,-122.456,1.0,0.5,24.0
Reservoir1,45.345,-122.678,3.0,1.0,12.0
```

**What EF5 does internally:**
- Lake1: Area = 0.5 km² → 500,000 m², ThVolume = 1.0 km³ → 1,000,000,000 m³
- Reservoir1: Area = 1.0 km² → 1,000,000 m², ThVolume = 3.0 km³ → 3,000,000,000 m³

## Example 2: Using [LAKE] Sections

**Control file:**
```ini
[LAKE SmallPond]
Lat = 35.5
Lon = -97.8
Area = 0.001        ; 0.001 km² = 1,000 m²
ThVolume = 0.0001   ; 0.0001 km³ = 100,000 m³
Klake = 6.0         ; 6 hours

[LAKE LargeReservoir]
Lat = 35.6
Lon = -97.9
Area = 10.0         ; 10 km² = 10,000,000 m²
ThVolume = 50.0     ; 50 km³ = 50,000,000,000 m³
Klake = 48.0        ; 48 hours
```

**What EF5 does internally:**
- SmallPond: Area = 0.001 km² → 1,000 m², ThVolume = 0.0001 km³ → 100,000 m³
- LargeReservoir: Area = 10.0 km² → 10,000,000 m², ThVolume = 50.0 km³ → 50,000,000,000 m³

## Benefits

### 1. User-Friendly Input
Instead of typing:
```ini
Area = 1000000      ; 1 km² in m²
ThVolume = 1000000000  ; 1 km³ in m³
```

Users can simply write:
```ini
Area = 1.0          ; 1 km²
ThVolume = 1.0      ; 1 km³
```

### 2. Reduced Errors
- No need to remember conversion factors
- No risk of typing too many zeros
- Consistent with lakes.csv format

### 3. Intuitive Values
- Lake areas in km² (typical range: 0.001 to 100 km²)
- Lake volumes in km³ (typical range: 0.0001 to 100 km³)
- Retention constants in hours (typical range: 1 to 100 hours)

## Implementation Details

The unit conversion is handled in `LakeConfigSection.cpp`:

```cpp
} else if (strcmp(key, "Area") == 0) {
    area = static_cast<float>(atof(value)) * 1e6f; // km² to m²
    return VALID_RESULT;
} else if (strcmp(key, "ThVolume") == 0) {
    thVolume = static_cast<float>(atof(value)) * 1e9f; // km³ to m³
    return VALID_RESULT;
}
```

And in `LakeModel.cpp` for lakes.csv:

```cpp
// Convert km³ to m³ (multiply by 1e9)
info.th_volume = std::stod(thVolStr) * 1e9;
// Convert km² to m² (multiply by 1e6)
info.area = std::stod(areaStr) * 1e6;
```

## Verification

To verify the conversion is working correctly, check the lake output files:
- Storage values should be in m³ (large numbers)
- Area calculations should use the converted m² values
- Outflow calculations should use the converted m³ values

The conversion is transparent to the user - they only need to specify values in the intuitive km² and km³ units! 