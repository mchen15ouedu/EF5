#ifndef CONFIG_LAKE_SECTION_H
#define CONFIG_LAKE_SECTION_H

#include "ConfigSection.h"
#include <string>
#include <vector>

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
    float GetVolumeMax() const { return volumeMax; }
    float GetVolumeInitial() const { return volumeInitial; }
    void SetGridNodeIndex(long idx) { gridNodeIndex = idx; }
    long GetGridNodeIndex() const { return gridNodeIndex; }
    void SetOutflow(float val) { outflow = val; }
    float GetOutflow() const { return outflow; }
    void SetInflow(float val) { inflow = val; }
    float GetInflow() const { return inflow; }
    void AddInlet(float lat, float lon, long gridIdx) { inlets.push_back({lat, lon, gridIdx}); }
    const std::vector<LakeInlet>& GetInlets() const { return inlets; }
    float GetHMax() const { return (area > 0.0f) ? (volumeMax / area) : 0.0f; }
    void EnforceStorageLimits(float dt) {
        if (volumeMax > 0.0f && storage > volumeMax) {
            float excess = storage - volumeMax;
            storage = volumeMax;
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
    float GetStorageKm3() const { return storage / 1e9f; }

    CONFIG_SEC_RET ProcessKeyValue(char *name, char *value) override {
        if (strcmp(name, "Area") == 0) { area = static_cast<float>(atof(value)) * 1e6f; return VALID_RESULT; } // km2 to m2
        if (strcmp(name, "VolumeMax") == 0) { volumeMax = static_cast<float>(atof(value)) * 1e9f; return VALID_RESULT; } // km3 to m3
        if (strcmp(name, "VolumeInitial") == 0) { volumeInitial = static_cast<float>(atof(value)) * 1e9f; storage = volumeInitial; return VALID_RESULT; } // km3 to m3
        if (strcmp(name, "OutflowFile") == 0) { outflowFile = value; return VALID_RESULT; }
        if (strcmp(name, "GridNodeIndex") == 0) { gridNodeIndex = static_cast<long>(atof(value)); return VALID_RESULT; }
        if (strcmp(name, "Outflow") == 0) { outflow = static_cast<float>(atof(value)); return VALID_RESULT; }
        if (strcmp(name, "Inflow") == 0) { inflow = static_cast<float>(atof(value)); return VALID_RESULT; }
        if (strcmp(name, "Precipitation") == 0) { precipitation = static_cast<float>(atof(value)); return VALID_RESULT; }
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
    float volumeMax = 0.0f;
    float volumeInitial = 0.0f;
    std::string outflowFile;
    long gridNodeIndex = -1;
    float outflow = 0.0f;
    float inflow = 0.0f;
    float storage = 0.0f;
    float precipitation = 0.0f;
    std::vector<LakeInlet> inlets;
};

#endif // CONFIG_LAKE_SECTION_H 