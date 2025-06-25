#include "LakeModel.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <cmath>

LakeModel::LakeModel(const LakeInfo& info, bool wm_flag, std::map<std::string, double>* engineeredDischarge)
    : lakeName(info.name), storage(info.th_volume), area(info.area), outflow(0.0), inflow(0.0), precipitation(0.0), evaporation(0.0), dt(0.0), th_volume(info.th_volume), wm_flag(wm_flag), engineeredDischarge(engineeredDischarge), retentionConstant(info.retention_constant) {}

void LakeModel::Step(const std::string& timestamp, double inflow, double precipitation, double evaporation, double dt) {
    this->inflow = inflow;
    this->precipitation = precipitation;
    this->evaporation = evaporation;
    this->dt = dt;
    double outflow = 0.0;
    double precip_vol = precipitation * 1e-3 * area; // mm to m^3
    double evap_vol = evaporation * 1e-3 * area;
    storage += (inflow * dt) + precip_vol - evap_vol;
    if (wm_flag && engineeredDischarge) {
        auto it = engineeredDischarge->find(timestamp);
        if (it != engineeredDischarge->end()) {
            outflow = it->second;
        } else {
            outflow = 0.0;
        }
    } else {
        if (storage > th_volume) {
            // Overflow condition: storage-based overflow
            outflow = (storage - th_volume) / dt;
            storage = th_volume;
        } else {
            // Dry season condition: linear reservoir outflow
            if (storage <= 0.0 || retentionConstant <= 0.0) {
                outflow = 0.0;
            } else {
                // Linear reservoir equation: O = S/K (convert K from hours to seconds)
                double linearOutflow = storage / (retentionConstant * 3600.0);
                
                // Apply exponential decay if we have a previous outflow value and we're in dry season
                if (this->outflow > 0.0 && storage <= th_volume) {
                    // Exponential decay: O_new = O_prev * exp(-dt/K)
                    double decayedOutflow = this->outflow * exp(-dt / (retentionConstant * 3600.0));
                    linearOutflow = decayedOutflow;
                }
                
                outflow = linearOutflow;
            }
        }
    }
    storage -= outflow * dt;
    if (storage < 0) storage = 0;
    this->outflow = outflow;
}

double LakeModel::GetOutflow() const { return outflow; }
double LakeModel::GetStorage() const { return storage; }
double LakeModel::GetThVolume() const { return th_volume; }
std::string LakeModel::GetLakeName() const { return lakeName; }

// CSV utility for reading engineered discharge
template<typename T>
void ReadEngineeredDischargeCSV(const std::string& filename, std::map<std::string, T>& dischargeMap) {
    std::ifstream file(filename);
    std::string line;
    getline(file, line); // skip header
    while (getline(file, line)) {
        std::istringstream ss(line);
        std::string date, dischargeStr;
        getline(ss, date, ',');
        getline(ss, dischargeStr, ',');
        if (!date.empty() && !dischargeStr.empty()) {
            T discharge = static_cast<T>(std::stod(dischargeStr));
            dischargeMap[date] = discharge;
        }
    }
}

// Explicit template instantiation for double
template void ReadEngineeredDischargeCSV<double>(const std::string&, std::map<std::string, double>&);

void ReadLakeInfoCSV(const std::string& filename, std::map<std::string, LakeInfo>& lakeInfoMap) {
    std::ifstream file(filename);
    std::string line;
    getline(file, line); // skip header
    while (getline(file, line)) {
        std::istringstream ss(line);
        std::string nameStr, latStr, lonStr, thVolStr, areaStr, klakeStr;
        getline(ss, nameStr, ',');
        getline(ss, latStr, ',');
        getline(ss, lonStr, ',');
        getline(ss, thVolStr, ',');
        getline(ss, areaStr, ',');
        getline(ss, klakeStr, ',');
        if (!nameStr.empty() && !latStr.empty() && !lonStr.empty() && 
            !thVolStr.empty() && !areaStr.empty()) {
            LakeInfo info;
            info.name = nameStr;
            info.lat = std::stod(latStr);
            info.lon = std::stod(lonStr);
            // Convert km³ to m³ (multiply by 1e9)
            info.th_volume = std::stod(thVolStr) * 1e9;
            // Convert km² to m² (multiply by 1e6)
            info.area = std::stod(areaStr) * 1e6;
            // Set retention constant (default to 24 hours if not provided)
            info.retention_constant = klakeStr.empty() ? 24.0 : std::stod(klakeStr);
            lakeInfoMap[info.name] = info;
        }
    }
} 