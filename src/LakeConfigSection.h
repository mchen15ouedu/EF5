#ifndef CONFIG_LAKE_SECTION_H
#define CONFIG_LAKE_SECTION_H

#include "ConfigSection.h"
#include <string>
#include <vector>
#include <cmath>

struct LakeInlet {
    float lat;
    float lon;
    long gridNodeIndex;
};

class LakeConfigSection : public ConfigSection {
public:
    LakeConfigSection(const char *nameVal);
    ~LakeConfigSection();

    const char *GetName() const { return name.c_str(); }
    float GetLat() const { return lat; }
    float GetLon() const { return lon; }
    float GetArea() const { return area; }
    void SetArea(float val) { area = val; }
    float GetMaxDepth() const { return maxDepth; }
    float GetInitialLevel() const { return initialLevel; }
    const char *GetOutflowFile() const { return outflowFile.c_str(); }
    float GetThVolume() const { return thVolume; }
    void SetGridNodeIndex(long idx) { gridNodeIndex = idx; }
    long GetGridNodeIndex() const { return gridNodeIndex; }
    void SetOutflow(float val) { outflow = val; }
    float GetOutflow() const { return outflow; }
    void SetInflow(float val) { inflow = val; }
    float GetInflow() const { return inflow; }
    void AddInlet(float lat, float lon, long gridIdx) { inlets.push_back({lat, lon, gridIdx}); }
    const std::vector<LakeInlet>& GetInlets() const { return inlets; }
    float GetHMax() const { return (area > 0.0f) ? (thVolume / area) : 0.0f; }
    void EnforceStorageLimits(float dt) {
        if (thVolume > 0.0f && storage > thVolume) {
            float excess = storage - thVolume;
            storage = thVolume;
            outflow += excess / dt;
        }
        if (area > 0.0f) {
            float h = storage / area;
            float hmax = GetHMax();
            if (h > hmax) h = hmax;
        }
    }
    void SetPrecipitation(float val) { precipitation = val; }
    float GetPrecipitation() const { return precipitation; }
    void SetEvaporation(float val) { evaporation = val; }
    float GetEvaporation() const { return evaporation; }
    float GetStorageKm3() const { return storage / 1e9f; }
    
    // Update storage based on water balance equation
    // Water Balance: Storage(t+1) = Storage(t) + Inflow*dt + Precip*Area*dt - ET*Area*dt - Outflow*dt
    // Units: storage (m³), inflow/outflow (m³/s), precipitation/evaporation (mm/hr), area (m²), dt (s)
    void UpdateWaterBalance(float dt, float evaporation = 0.0f) {
        float precip_vol = precipitation * 1e-3f * area * dt; // mm to m³
        float evap_vol = evaporation * 1e-3f * area * dt; // mm to m³
        storage += (inflow * dt) + precip_vol - evap_vol - (outflow * dt);
        if (storage < 0.0f) storage = 0.0f;
    }
    
    // Calculate outflow based on storage limits (storage-based overflow)
    float CalculateOutflowFromStorage(float dt) {
        if (thVolume > 0.0f && storage > thVolume) {
            return (storage - thVolume) / dt;
        }
        return 0.0f;
    }
    
    // Update storage and calculate outflow using storage-based approach
    void UpdateWaterBalanceAndCalculateOutflow(float dt, float evaporation = 0.0f) {
        // First update storage assuming no outflow
        float precip_vol = precipitation * 1e-3f * area * dt; // mm to m³
        float evap_vol = evaporation * 1e-3f * area * dt; // mm to m³
        storage += (inflow * dt) + precip_vol - evap_vol;
        if (storage < 0.0f) storage = 0.0f;
        
        // Then calculate outflow using combined approach (overflow + linear reservoir)
        outflow = CalculateCombinedOutflow(dt);
        
        // Finally adjust storage for the calculated outflow
        storage -= outflow * dt;
        if (storage < 0.0f) storage = 0.0f;
    }
    
    // Getter for storage (needed for water balance update)
    float GetStorage() const { return storage; }
    void SetStorage(float val) { storage = val; }

    // Linear reservoir parameters for dry season outflow
    void SetRetentionConstant(float K) { retentionConstant = K; }
    float GetRetentionConstant() const { return retentionConstant; }
    
    // Calculate linear reservoir outflow for dry season (when storage <= th_volume)
    float CalculateLinearReservoirOutflow(float dt) {
        if (storage <= 0.0f || retentionConstant <= 0.0f) {
            return 0.0f; // Return 0 if no storage or invalid K
        }
        
        // Linear reservoir equation: O = S/K
        float linearOutflow = storage / retentionConstant;
        
        // Apply exponential decay if we have a previous outflow value and we're in dry season
        if (outflow > 0.0f && storage <= thVolume) {
            // Exponential decay: O_new = O_prev * exp(-dt/K)
            float decayedOutflow = outflow * exp(-dt / retentionConstant);
            linearOutflow = decayedOutflow;
        }
        
        return linearOutflow;
    }
    
    // Combined outflow calculation: overflow + linear reservoir
    float CalculateCombinedOutflow(float dt) {
        if (thVolume > 0.0f && storage > thVolume) {
            // Overflow condition: storage-based overflow
            return (storage - thVolume) / dt;
        } else {
            // Dry season condition: linear reservoir outflow
            return CalculateLinearReservoirOutflow(dt);
        }
    }

    CONFIG_SEC_RET ProcessKeyValue(char *name, char *value) override {
        if (strcmp(name, "Area") == 0) { area = static_cast<float>(atof(value)) * 1e6f; return VALID_RESULT; } // km2 to m2
        if (strcmp(name, "ThVolume") == 0) { thVolume = static_cast<float>(atof(value)) * 1e9f; return VALID_RESULT; } // km3 to m3
        if (strcmp(name, "OutflowFile") == 0) { outflowFile = value; return VALID_RESULT; }
        if (strcmp(name, "GridNodeIndex") == 0) { gridNodeIndex = static_cast<long>(atof(value)); return VALID_RESULT; }
        if (strcmp(name, "Outflow") == 0) { outflow = static_cast<float>(atof(value)); return VALID_RESULT; }
        if (strcmp(name, "Inflow") == 0) { inflow = static_cast<float>(atof(value)); return VALID_RESULT; }
        if (strcmp(name, "Precipitation") == 0) { precipitation = static_cast<float>(atof(value)); return VALID_RESULT; }
        if (strcmp(name, "Klake") == 0) { retentionConstant = static_cast<float>(atof(value)); return VALID_RESULT; } // hours
        return ConfigSection::ProcessKeyValue(name, value);
    }
    CONFIG_SEC_RET ValidateSection() override;

private:
    std::string name;
    float lat = 0.0f;
    float lon = 0.0f;
    float area = 0.0f;
    float maxDepth = 0.0f;
    float initialLevel = 0.0f;
    float thVolume = 0.0f;
    std::string outflowFile;
    long gridNodeIndex = -1;
    float outflow = 0.0f;
    float inflow = 0.0f;
    float storage = 0.0f;
    float precipitation = 0.0f;
    float evaporation = 0.0f;
    float retentionConstant = 0.0f;
    std::vector<LakeInlet> inlets;
};

#endif // CONFIG_LAKE_SECTION_H 