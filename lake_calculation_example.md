# Lake Parameter Calculation Example

## From lakes.csv to Lake Properties

Given the lakes.csv data (with user-friendly km³/km² units and linear reservoir parameters):
```csv
name,lat,lon,th_volume,area,klake
Lake1,45.123,-122.456,1.0,0.5,24.0
Lake2,45.234,-122.567,2.0,0.75,48.0
```

## Internal Unit Conversion

The code automatically converts units:
- **km³ → m³**: multiply by 1e9
- **km² → m²**: multiply by 1e6
- **klake**: kept in hours (converted to seconds internally)

## Calculations

### Lake 1
- **Input Th Volume**: 1.0 km³
- **Internal Th Volume**: 1.0 × 1e9 = **1,000,000,000 m³**
- **Input Area**: 0.5 km²
- **Internal Area**: 0.5 × 1e6 = **500,000 m²**
- **Max Depth**: 1,000,000,000 ÷ 500,000 = **2,000 m**
- **Initial Storage**: 1,000,000,000 m³ (starts at th_volume)
- **Klake**: 24.0 hours = **86,400 seconds**

### Lake 2
- **Input Th Volume**: 2.0 km³
- **Internal Th Volume**: 2.0 × 1e9 = **2,000,000,000 m³**
- **Input Area**: 0.75 km²
- **Internal Area**: 0.75 × 1e6 = **750,000 m²**
- **Max Depth**: 2,000,000,000 ÷ 750,000 = **2,667 m**
- **Initial Storage**: 2,000,000,000 m³ (starts at th_volume)
- **Klake**: 48.0 hours = **172,800 seconds**

## Outflow Behavior

### Wet Season (storage > th_volume)
- **Lake 1**: Outflow starts when depth > 2,000 m
- **Lake 2**: Outflow starts when depth > 2,667 m
- **Formula**: `outflow = (storage - th_volume) / timestep`

### Dry Season (storage ≤ th_volume)
- **Linear Reservoir Outflow**: `outflow = S/K * exp(-dt/K)`
- **Lake 1**: 
  - Initial linear outflow = 1,000,000,000 ÷ 86,400 = **11,574 m³/s**
  - Decays exponentially with K = 24 hours
  - When storage = 0, outflow = 0 (naturally)
- **Lake 2**:
  - Initial linear outflow = 2,000,000,000 ÷ 172,800 = **11,574 m³/s**
  - Decays exponentially with K = 48 hours (slower decay)
  - When storage = 0, outflow = 0 (naturally)

## Exponential Decay Example

For Lake 1 with K = 24 hours:
- **After 24 hours**: outflow = 11,574 × exp(-24/24) = **4,260 m³/s**
- **After 48 hours**: outflow = 11,574 × exp(-48/24) = **1,568 m³/s**
- **After 72 hours**: outflow = 11,574 × exp(-72/24) = **577 m³/s**
- **When storage = 0**: outflow = 0 (naturally)

## Advantages of Combined Approach

1. **User-Friendly**: Easy to input 1.0 instead of 1,000,000,000
2. **Realistic Wet Season**: Storage-based overflow for flood conditions
3. **Realistic Dry Season**: Linear reservoir for natural outflow decay
4. **Smooth Transitions**: No sudden jumps between wet/dry modes
5. **Natural Termination**: Outflow naturally reaches zero when storage is empty
6. **Automatic Conversion**: Code handles all unit conversions internally
7. **Simplified Setup**: Only need th_volume, area, and klake 