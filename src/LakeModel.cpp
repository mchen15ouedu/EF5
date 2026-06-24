#include "LakeModel.h"
#include "LakeMap.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cctype>
#include <algorithm>
#include "Messages.h"
#include "DatedName.h"
#include "BasicGrids.h"

// Helper functions for lake calculations
namespace LakeCalculations {
    double CalculateLinearReservoirOutflow(double storage, double th_volume, double param_a, double param_b) {
        if (storage <= 0.0 || param_a <= 0.0) {
            return 0.0;
        }
        double storageRatio = storage / th_volume;
        double a_seconds = param_a * 3600.0; // Convert retention time from hours to seconds
        return (1.0 / a_seconds) * storage * pow(storageRatio, param_b);
    }
    
    double GetEngineeredDischarge(const std::string& timestamp, bool wm_flag, 
                                   std::map<std::string, double>* engineeredDischarge) {
        if (wm_flag && engineeredDischarge) {
            std::map<std::string, double>::iterator it = engineeredDischarge->find(timestamp);
            if (it != engineeredDischarge->end()) {
                return it->second;
            }
        }
        return 0.0;
    }
    
    std::string TimeVarToTimestamp(TimeVar* currentTime) {
        DatedName timeStr;
        timeStr.SetNameStr("YYYYMMDD_HHUU");
        timeStr.ProcessNameLoose(NULL);
        timeStr.UpdateName(currentTime->GetTM());
        return timeStr.GetName();
    }
}

// Legacy LakeModel implementation (for backward compatibility)
LegacyLakeModel::LegacyLakeModel(const LakeInfo& info, bool wm_flag, std::map<std::string, double>* engineeredDischarge)
    : lakeName(info.name), storage(info.th_volume), area(info.area), outflow(0.0), inflow(0.0), precipitation(0.0), evaporation(0.0), dt(0.0), th_volume(info.th_volume), wm_flag(wm_flag), engineeredDischarge(engineeredDischarge), retentionConstant(info.retention_constant) {}

void LegacyLakeModel::Step(const std::string& timestamp, double inflow, double precipitation, double evaporation, double dt) {
    this->inflow = inflow;
    this->precipitation = precipitation;
    this->evaporation = evaporation;
    this->dt = dt;
    double outflow = 0.0;
    double precip_vol = precipitation * 1e-3 * area; // mm to m^3
    double evap_vol = evaporation * 1e-3 * area;
    storage += (inflow * dt) + precip_vol - evap_vol;
    
    // Calculate outflow
    outflow = LakeCalculations::GetEngineeredDischarge(timestamp, wm_flag, engineeredDischarge);
    if (outflow == 0.0) {
        if (storage > th_volume) {
            // Overflow: spill the excess; storage capped at threshold. No second
            // subtraction (the old code removed the excess twice).
            outflow = (storage - th_volume) / dt;
            storage = th_volume;
        } else {
            // Dry season: linear-reservoir outflow, then remove it from storage.
            // Use retentionConstant as param_a and default b=1.5 for LegacyLakeModel
            outflow = LakeCalculations::CalculateLinearReservoirOutflow(storage, th_volume, retentionConstant, 1.5);
            storage -= outflow * dt;
            if (storage < 0) storage = 0;
        }
    } else {
        // Engineered (dam-controlled) release.
        storage -= outflow * dt;
        if (storage < 0) storage = 0;
    }
    this->outflow = outflow;
}

double LegacyLakeModel::GetOutflow() const { return outflow; }
double LegacyLakeModel::GetStorage() const { return storage; }
double LegacyLakeModel::GetThVolume() const { return th_volume; }
std::string LegacyLakeModel::GetLakeName() const { return lakeName; }

// LakeModelImpl implementation (new state-saving version)
LakeModelImpl::LakeModelImpl(const LakeInfo& info, bool wm_flag, std::map<std::string, double>* engineeredDischarge)
    : lakeName(info.name), storage(info.th_volume), area(info.area), outflow(0.0), inflow(0.0), precipitation(0.0), evaporation(0.0), dt(0.0), th_volume(info.th_volume), wm_flag(wm_flag), engineeredDischarge(engineeredDischarge), retentionConstant(info.retention_constant), param_a(info.param_a), param_b(info.param_b), nodes(NULL), lakeNodeIndex(-1), warnedDtResidence(false), lat(info.lat), lon(info.lon), obsFlowAccum(info.obsFlowAccum), obsFlowAccumSet(info.obsFlowAccumSet) {
    // Initialize grid location
    gridLoc.x = -1;
    gridLoc.y = -1;
}

LakeModelImpl::~LakeModelImpl() {
    // Cleanup handled by parent class
}

bool LakeModelImpl::InitializeModel(std::vector<GridNode> *newNodes,
                                   std::map<GaugeConfigSection *, float *> *paramSettings,
                                   std::vector<FloatGrid *> *paramGrids) {
    nodes = newNodes;
    if (!nodes) {
        ERROR_LOG("LakeModelImpl::InitializeModel: nodes pointer is NULL");
        return false;
    }
    
    // Find the grid node where this lake is located
    lakeNodeIndex = -1;
    
    // Use the grid location that was set by LakeMap::FindLakeLocations()
    // Lake initialization without logging
    
    if (gridLoc.x >= 0 && gridLoc.y >= 0) {
        // Find the node that matches the grid coordinates
        for (size_t i = 0; i < nodes->size(); i++) {
            GridNode& node = nodes->at(i);
            if (node.x == gridLoc.x && node.y == gridLoc.y) {
                lakeNodeIndex = i;
                // Found matching node without logging
                break;
            }
        }
    }
    

    
    // Fallback: if no location found, use first node (for backward compatibility)
    if (lakeNodeIndex == -1) {
        lakeNodeIndex = 0;
        // Using fallback node without logging
    }
    
    if (lakeNodeIndex == -1) {
        printf("Error: LakeModelImpl::InitializeModel: Could not find grid node for lake %s\n", lakeName.c_str());
        return false;
    }
    
    // Initialize the lake node (single struct -- see header note)
    lakeNode.lakeName = lakeName;
    lakeNode.storage = storage;
    lakeNode.area = area;
    lakeNode.outflow = outflow;
    lakeNode.inflow = inflow;
    lakeNode.precipitation = precipitation;
    lakeNode.evaporation = evaporation;
    lakeNode.th_volume = th_volume;
    lakeNode.wm_flag = wm_flag;
    
    // Lake initialized without logging
    
    // Use the retention constant from LakeInfo (no need to carve to grid)
    lakeNode.retentionConstant = retentionConstant;
    lakeNode.param_a = param_a;
    lakeNode.param_b = param_b;
    // Using retention constant without logging
    
    // Initialize state arrays
    lakeNode.states[STATE_LAKE_STORAGE] = static_cast<float>(storage);
    lakeNode.states[STATE_LAKE_OUTFLOW] = static_cast<float>(outflow);
    

    
    return true;
}

void LakeModelImpl::InitializeStates(TimeVar *beginTime, char *statePath) {
    if (!statePath || !nodes) return;
    
    DatedName timeStr;
    timeStr.SetNameStr("YYYYMMDD_HHUU");
    timeStr.ProcessNameLoose(NULL);
    timeStr.UpdateName(beginTime->GetTM());
    
    // Track if we found state files
    bool foundStorageState = false;
    bool foundOutflowState = false;
    
    char buffer[300];
    for (int p = 0; p < STATE_LAKE_QTY; p++) {
        sprintf(buffer, "%s/lake_%s_%s.tif", statePath, 
                (p == STATE_LAKE_STORAGE) ? "storage" : "outflow",
                timeStr.GetName());
        
        FloatGrid *sGrid = ReadFloatTifGrid(buffer);
        if (sGrid) {
            // Loading state without logging
            
            if (lakeNodeIndex >= 0 && lakeNodeIndex < (int)nodes->size()) {
                GridNode *node = &nodes->at(lakeNodeIndex);
                LakeGridNode *cNode = &lakeNode;
                
                if (sGrid->IsSpatialMatch(g_DEM)) {
                    if (sGrid->data[node->y][node->x] != sGrid->noData) {
                        cNode->states[p] = sGrid->data[node->y][node->x];
                        
                        // Update lake state variables
                        if (p == STATE_LAKE_STORAGE) {
                            storage = static_cast<double>(cNode->states[p]);
                            cNode->storage = storage;
                            foundStorageState = true;
                            // Loaded storage state without logging
                        } else if (p == STATE_LAKE_OUTFLOW) {
                            outflow = static_cast<double>(cNode->states[p]);
                            cNode->outflow = outflow;
                            foundOutflowState = true;
                            // Loaded outflow state without logging
                        }
                    }
                }
            }
            delete sGrid;
        } else {
            // State file not found without logging
        }
    }
    
    // If state files were not found, set default values
    if (!foundStorageState && lakeNodeIndex >= 0 && lakeNodeIndex < (int)nodes->size()) {
        // Cold-start initial storage = threshold volume (the lake sits at its
        // outlet/spillway level). This matches the constructor's storage(th_volume)
        // so the model has ONE consistent initial condition regardless of whether
        // a state file exists. (Was 0.8*th_volume here, conflicting with the
        // constructor.) Change this fraction if you prefer a spin-up buffer.
        storage = th_volume;
        LakeGridNode *cNode = &lakeNode;
        cNode->storage = storage;
        cNode->states[STATE_LAKE_STORAGE] = static_cast<float>(storage);
    }
    
    if (!foundOutflowState && lakeNodeIndex >= 0 && lakeNodeIndex < (int)nodes->size()) {
        // Set outflow to 0.0 (no initial outflow)
        outflow = 0.0;
        LakeGridNode *cNode = &lakeNode;
        cNode->outflow = outflow;
        cNode->states[STATE_LAKE_OUTFLOW] = static_cast<float>(outflow);
        // Using default outflow without logging
    }
}

void LakeModelImpl::SaveStates(TimeVar *currentTime, char *statePath,
                               GridWriterFull *gridWriter) {
    if (!statePath || !nodes || !gridWriter) return;
    
    DatedName timeStr;
    timeStr.SetNameStr("YYYYMMDD_HHUU");
    timeStr.ProcessNameLoose(NULL);
    timeStr.UpdateName(currentTime->GetTM());
    
    std::vector<float> dataVals;
    dataVals.resize(nodes->size());
    
    // Initialize all nodes with 0
    for (size_t i = 0; i < nodes->size(); i++) {
        dataVals[i] = 0.0f;
    }
    
    char buffer[300];
    for (int p = 0; p < STATE_LAKE_QTY; p++) {
        sprintf(buffer, "%s/lake_%s_%s.tif", statePath, 
                (p == STATE_LAKE_STORAGE) ? "storage" : "outflow",
                timeStr.GetName());
        
        // Update the lake node's state
        if (lakeNodeIndex >= 0 && lakeNodeIndex < (int)nodes->size()) {
            LakeGridNode *cNode = &lakeNode;

            // Update state from current lake values
            if (p == STATE_LAKE_STORAGE) {
                cNode->states[p] = static_cast<float>(storage);
            } else if (p == STATE_LAKE_OUTFLOW) {
                cNode->states[p] = static_cast<float>(outflow);
            }
            
            dataVals[lakeNodeIndex] = cNode->states[p];
        }
        
        gridWriter->WriteGrid(nodes, &dataVals, buffer, false);
        
        // State saving without logging
    }
}



void LakeModelImpl::Step(const std::string& timestamp, double inflow, double precipitation, double evaporation, double dt) {
    // Legacy method for backward compatibility
    this->inflow = inflow;
    this->precipitation = precipitation;
    this->evaporation = evaporation;
    this->dt = dt;
    double outflow = 0.0;
    double precip_vol = precipitation * 1e-3 * area; // mm to m^3
    double evap_vol = evaporation * 1e-3 * area;
    storage += (inflow * dt) + precip_vol - evap_vol;
    
    // Calculate outflow
    outflow = LakeCalculations::GetEngineeredDischarge(timestamp, wm_flag, engineeredDischarge);
    if (outflow == 0.0) {
        if (storage > th_volume) {
            // Overflow: spill the excess. Storage is capped at the threshold and
            // the excess leaves as outflow -- do NOT subtract outflow*dt again
            // afterwards (that was the old double-removal mass-balance bug).
            outflow = (storage - th_volume) / dt;
            storage = th_volume;
        } else {
            // Dry season: linear-reservoir outflow, then remove it from storage.
            outflow = LakeCalculations::CalculateLinearReservoirOutflow(storage, th_volume, param_a, param_b);
            storage -= outflow * dt;
            if (storage < 0) storage = 0;
        }
    } else {
        // Engineered (dam-controlled) release.
        storage -= outflow * dt;
        if (storage < 0) storage = 0;
    }
    this->outflow = outflow;

    // Update lake node state if available
    if (lakeNodeIndex >= 0) {
        lakeNode.storage = storage;
        lakeNode.outflow = this->outflow;
        lakeNode.inflow = this->inflow;
        lakeNode.precipitation = this->precipitation;
        lakeNode.evaporation = this->evaporation;
        lakeNode.states[STATE_LAKE_STORAGE] = static_cast<float>(storage);
        lakeNode.states[STATE_LAKE_OUTFLOW] = static_cast<float>(this->outflow);
    }
}

// Two-part lake balance approach implementation

void LakeModelImpl::ApplyVerticalBalance(float stepHours, std::vector<float>* precip, std::vector<float>* pet) {
    // Part 1: Vertical balance - S = P - E (precipitation minus evaporation)
    // This handles the vertical water exchange with the atmosphere
    
    if (!precip || !pet || lakeNodeIndex < 0 || lakeNodeIndex >= (int)precip->size()) {
        // Vertical balance skipped without logging
        return;
    }
    
    // Get precipitation and PET values at the lake location
    this->precipitation = static_cast<double>((*precip)[lakeNodeIndex]);
    this->evaporation = static_cast<double>((*pet)[lakeNodeIndex]);
    

    this->dt = static_cast<double>(stepHours * 3600.0); // Convert hours to seconds
    
    // Convert precipitation and evaporation from mm to m³
    double precip_vol = this->precipitation * 1e-3 * area; // mm to m^3
    double evap_vol = this->evaporation * 1e-3 * area;     // mm to m^3
    
    // Apply vertical balance: storage += P - E
    storage += precip_vol - evap_vol;
    
    // Ensure storage doesn't go negative
    if (storage < 0) storage = 0;
    

    
    // Update lake node state
    if (lakeNodeIndex >= 0) {
        lakeNode.storage = storage;
        lakeNode.precipitation = this->precipitation;
        lakeNode.evaporation = this->evaporation;
        lakeNode.states[STATE_LAKE_STORAGE] = static_cast<float>(storage);
    }
}

void LakeModelImpl::ApplyHorizontalBalance(float stepHours, std::vector<float>* currentQ, std::vector<GridNode>* nodes, TimeVar* currentTime, LakeMap* lakeMap) {
    // Part 2: Horizontal balance - S = inflow - outflow
    // This handles the horizontal water exchange with the river network
    
    if (!currentQ || !nodes || !currentTime || !lakeMap || lakeNodeIndex < 0) {
        // Horizontal balance skipped without logging
        return;
    }
    
    // Calculate inflow from routed Q using LakeMap
    float inflow = lakeMap->CalculateInflow(this, currentQ, nodes, currentTime);
    
    this->inflow = static_cast<double>(inflow);
    this->dt = static_cast<double>(stepHours * 3600.0); // Convert hours to seconds

    // Warn (once) if the timestep is larger than the lake's residence time.
    // We deliberately do NOT cap the outflow -- capping would break the water
    // mass balance. Instead we tell the user their setup may be unstable.
    if (!warnedDtResidence && param_a > 0.0 && (double)stepHours > param_a) {
        WARNING_LOGF("Lake %s: timestep (%.4g h) exceeds the lake residence time "
                     "klake=%.4g h. Outflow may overshoot available storage and "
                     "degrade the simulation; consider a smaller timestep.",
                     lakeName.c_str(), (double)stepHours, param_a);
        warnedDtResidence = true;
    }

    // Add inflow to storage
    storage += this->inflow * this->dt;

    // Calculate outflow based on storage conditions
    std::string timestamp = LakeCalculations::TimeVarToTimestamp(currentTime);
    double outflowValue = LakeCalculations::GetEngineeredDischarge(timestamp, wm_flag, engineeredDischarge);

    if (outflowValue == 0.0) {
        if (storage > th_volume) {
            // Overflow: spill the excess. Storage capped at the threshold; the
            // excess leaves as outflow. Do NOT subtract outflow*dt again below
            // (that was the old double-removal mass-balance bug).
            outflowValue = (storage - th_volume) / this->dt;
            storage = th_volume;
        } else {
            // Dry season: linear-reservoir outflow, then remove it from storage.
            outflowValue = LakeCalculations::CalculateLinearReservoirOutflow(storage, th_volume, param_a, param_b);
            storage -= outflowValue * this->dt;
            if (storage < 0) storage = 0;
        }
    } else {
        // Engineered (dam-controlled) release.
        storage -= outflowValue * this->dt;
        if (storage < 0) storage = 0;
    }
    this->outflow = outflowValue;

    // Update lake node state
    if (lakeNodeIndex >= 0) {
        lakeNode.storage = storage;
        lakeNode.outflow = this->outflow;
        lakeNode.inflow = this->inflow;
        lakeNode.states[STATE_LAKE_STORAGE] = static_cast<float>(storage);
        lakeNode.states[STATE_LAKE_OUTFLOW] = static_cast<float>(this->outflow);
    }
    
    // Replace Q value at lake grid cell with lake outflow
    if (lakeNodeIndex >= 0 && lakeNodeIndex < (int)currentQ->size()) {
        (*currentQ)[lakeNodeIndex] = static_cast<float>(this->outflow);
    }
}

bool LakeModelImpl::WaterBalance(float stepHours,
                                 std::vector<float> *precip,
                                 std::vector<float> *pet,
                                 std::vector<float> *fastFlow,
                                 std::vector<float> *interFlow,
                                 std::vector<float> *baseFlow,
                                 std::vector<float> *soilMoisture,
                                 std::vector<float> *groundwater) {
    // For lake model, we need to handle the water balance differently
    // since lakes don't generate runoff components like other models
    
    if (!precip || !pet || !fastFlow || !interFlow || !baseFlow || 
        !soilMoisture || !groundwater || lakeNodeIndex < 0) {
        return false;
    }
    
    // Apply vertical balance (P-E) for the lake
    ApplyVerticalBalance(stepHours, precip, pet);
    
    // For lake model, we don't generate traditional runoff components
    // Instead, we set them to zero and let the horizontal balance handle outflow
    for (size_t i = 0; i < fastFlow->size(); i++) {
        (*fastFlow)[i] = 0.0f;
        (*interFlow)[i] = 0.0f;
        (*baseFlow)[i] = 0.0f;
    }
    
    // Set soil moisture and groundwater to zero for lake cells
    // (lakes don't have soil moisture or groundwater in the traditional sense)
    for (size_t i = 0; i < soilMoisture->size(); i++) {
        (*soilMoisture)[i] = 0.0f;
        (*groundwater)[i] = 0.0f;
    }
    
    return true;
}

// CSV utility for reading engineered discharge (simple version for double)
void ReadEngineeredDischargeCSV(const std::string& filename, std::map<std::string, double>& dischargeMap) {
    std::ifstream file(filename.c_str());
    if (!file.is_open()) return;
    
    std::string line;
    getline(file, line); // skip header
    while (getline(file, line)) {
        std::istringstream ss(line);
        std::string date, dischargeStr;
        getline(ss, date, ',');
        getline(ss, dischargeStr, ',');
        if (!date.empty() && !dischargeStr.empty()) {
            double discharge = atof(dischargeStr.c_str());
            dischargeMap[date] = discharge;
        }
    }
}

// Specialized version for nested map type
void ReadEngineeredDischargeCSV(const std::string& filename, std::map<std::string, std::map<std::string, double> >& dischargeMap) {
    std::ifstream file(filename.c_str());
    if (!file.is_open()) return;
    
    std::string line;
    // Read header
    if (!getline(file, line)) return;
    
    std::vector<std::string> lakeNames;
    std::istringstream header(line);
    std::string col;
    // First column is time
    getline(header, col, ',');
    while (getline(header, col, ',')) {
        lakeNames.push_back(col);
    }
    // Read each row
    while (getline(file, line)) {
        std::istringstream ss(line);
        std::string timestamp;
        getline(ss, timestamp, ',');
        for (size_t i = 0; i < lakeNames.size(); i++) {
            std::string val;
            if (!getline(ss, val, ',')) break;
            double discharge = atof(val.c_str());
            dischargeMap[lakeNames[i]][timestamp] = discharge;
        }
    }
}

void ReadLakeInfoCSV(const std::string& filename, std::map<std::string, LakeInfo>& lakeInfoMap) {
    std::ifstream file(filename.c_str());
    if (!file.is_open()) return;
    
    std::string line;
    getline(file, line); // skip header
    while (getline(file, line)) {
        std::istringstream ss(line);
        std::string nameStr, latStr, lonStr, thVolStr, areaStr, klakeStr, obsFamStr, outputtsStr;
        getline(ss, nameStr, ',');
        getline(ss, latStr, ',');
        getline(ss, lonStr, ',');
        getline(ss, thVolStr, ',');
        getline(ss, areaStr, ',');
        getline(ss, klakeStr, ',');
        getline(ss, obsFamStr, ','); // Optional obsFam column
        getline(ss, outputtsStr, ','); // Optional outputts column
        if (!nameStr.empty() && !latStr.empty() && !lonStr.empty() && 
            !thVolStr.empty() && !areaStr.empty()) {
            LakeInfo info;
            info.name = nameStr;
            info.lat = atof(latStr.c_str());
            info.lon = atof(lonStr.c_str());
            // Get th_volume in km³ for b calculation
            double th_volume_km3 = atof(thVolStr.c_str());
            // Convert km³ to m³ (multiply by 1e9)
            info.th_volume = th_volume_km3 * 1e9;
            // Convert km² to m² (multiply by 1e6)
            info.area = atof(areaStr.c_str()) * 1e6;
            // Set retention constant (default to 24 hours if not provided)
            info.retention_constant = klakeStr.empty() ? 24.0 : atof(klakeStr.c_str());
            // klake IS the linear-reservoir retention time 'a'; without this
            // param_a stayed 0 and dry-season outflow was always zero.
            info.param_a = info.retention_constant;
            // Calculate parameter b based on th_volume (input value in km³)
            info.param_b = LakeInfo::CalculateParamB(th_volume_km3);
            // Set observed flow accumulation (optional)
            if (!obsFamStr.empty()) {
                info.obsFlowAccum = atof(obsFamStr.c_str());
                info.obsFlowAccumSet = true;
            } else {
                info.obsFlowAccum = 0.0;
                info.obsFlowAccumSet = false;
            }
            // Set outputts flag (optional, default to false)
            if (!outputtsStr.empty()) {
                // Trim whitespace from outputtsStr
                std::string trimmedOutputts = outputtsStr;
                // Remove leading whitespace
                trimmedOutputts.erase(0, trimmedOutputts.find_first_not_of(" \t\r\n"));
                // Remove trailing whitespace
                trimmedOutputts.erase(trimmedOutputts.find_last_not_of(" \t\r\n") + 1);
                
                // Check for Y, y, TRUE, true, 1
                std::string outputtsLower = trimmedOutputts;
                std::transform(outputtsLower.begin(), outputtsLower.end(), outputtsLower.begin(), ::tolower);
                info.outputts = (outputtsLower == "y" || outputtsLower == "true" || outputtsLower == "1");
            } else {
                info.outputts = false;
            }
            lakeInfoMap[info.name] = info;
        }
    }
} 