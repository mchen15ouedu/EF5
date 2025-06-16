#include "LakeModel.h"
#include <fstream>
#include <sstream>
#include <iostream>

LakeModel::LakeModel(const LakeInfo& info, bool wm_flag, std::map<std::string, double>* engineeredDischarge)
    : storage(info.initial_volume), area(0.0), outflow(0.0), inflow(0.0), precipitation(0.0), evaporation(0.0), dt(0.0), max_volume(info.max_volume), wm_flag(wm_flag), engineeredDischarge(engineeredDischarge) {}

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
        if (storage > max_volume) {
            outflow = (storage - max_volume) / dt;
            storage = max_volume;
        } else {
            outflow = 0.0;
        }
    }
    storage -= outflow * dt;
    if (storage < 0) storage = 0;
    this->outflow = outflow;
}

double LakeModel::GetOutflow() const { return outflow; }
double LakeModel::GetStorage() const { return storage; }
double LakeModel::GetMaxVolume() const { return max_volume; }

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

void ReadLakeInfoCSV(const std::string& filename, std::map<int, LakeInfo>& lakeInfoMap) {
    std::ifstream file(filename);
    std::string line;
    getline(file, line); // skip header
    while (getline(file, line)) {
        std::istringstream ss(line);
        std::string idStr, latStr, lonStr, maxVolStr, initVolStr;
        getline(ss, idStr, ',');
        getline(ss, latStr, ',');
        getline(ss, lonStr, ',');
        getline(ss, maxVolStr, ',');
        getline(ss, initVolStr, ',');
        if (!idStr.empty() && !latStr.empty() && !lonStr.empty() && !maxVolStr.empty() && !initVolStr.empty()) {
            LakeInfo info;
            info.id = std::stoi(idStr);
            info.lat = std::stod(latStr);
            info.lon = std::stod(lonStr);
            info.max_volume = std::stod(maxVolStr);
            info.initial_volume = std::stod(initVolStr);
            lakeInfoMap[info.id] = info;
        }
    }
} 