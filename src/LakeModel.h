#pragma once
#include <string>
#include <map>

struct LakeInfo {
    int id;
    double lat;
    double lon;
    double max_volume;
    double initial_volume;
};

class LakeModel {
public:
    LakeModel(const LakeInfo& info, bool wm_flag = false, std::map<std::string, double>* engineeredDischarge = nullptr);

    // Simulate one timestep
    void Step(const std::string& timestamp, double inflow, double precipitation, double evaporation, double dt);

    double GetOutflow() const;
    double GetStorage() const;
    double GetMaxVolume() const;

    // Members
    double storage;      // Current storage (m^3)
    double area;         // Surface area (m^2)
    double outflow;      // Outflow (m^3/s)
    double inflow;       // Inflow (m^3/s)
    double precipitation;// Precipitation (mm or m)
    double evaporation;  // Evaporation (mm or m)
    double dt;           // Timestep (s)
    double max_volume;   // Maximum volume (m^3)
    bool wm_flag;        // Use engineered discharge if true
    std::map<std::string, double>* engineeredDischarge; // Pointer to engineered discharge time series
};

// CSV utility for reading engineered discharge
template<typename T>
void ReadEngineeredDischargeCSV(const std::string& filename, std::map<std::string, T>& dischargeMap);

void ReadLakeInfoCSV(const std::string& filename, std::map<int, LakeInfo>& lakeInfoMap); 