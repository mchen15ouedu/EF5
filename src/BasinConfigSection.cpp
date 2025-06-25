#include "BasinConfigSection.h"
#include "Messages.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include "BasicGrids.h"
#include <cmath>

std::map<std::string, BasinConfigSection *> g_basinConfigs;

BasinConfigSection::BasinConfigSection(char *newName) { strcpy(name, newName); }

BasinConfigSection::~BasinConfigSection() {}

CONFIG_SEC_RET BasinConfigSection::ProcessKeyValue(char *name, char *value) {
  if (strcasecmp(name, "gauge") == 0) {
    TOLOWER(value);
    std::map<std::string, GaugeConfigSection *>::iterator itr =
        g_gaugeConfigs.find(value);
    if (itr == g_gaugeConfigs.end()) {
      ERROR_LOGF("Unknown gauge \"%s\" in basin!", value);
      return INVALID_RESULT;
    }
    if (IsDuplicateGauge(itr->second)) {
      ERROR_LOGF("Duplicate gauge \"%s\" in basin!", value);
      return INVALID_RESULT;
    }
    gauges.push_back(itr->second);
  } else if (strcmp(name, "LakeListFile") == 0) {
    lakeListFile = value;
    LoadLakesFromCSV(lakeListFile);
    return VALID_RESULT;
  } else if (strcmp(name, "DamQ") == 0) {
    engineeredDischargeFile = value;
    LoadEngineeredDischargeCSV(engineeredDischargeFile);
    return VALID_RESULT;
  } else {
    ERROR_LOGF("Unknown key value \"%s=%s\" in basin %s!", name, value,
               this->name);
    return INVALID_RESULT;
  }

  return VALID_RESULT;
}

CONFIG_SEC_RET BasinConfigSection::ValidateSection() {

  if (gauges.size() == 0) {
    ERROR_LOG("A basin was defined which contains no gauges!");
    return INVALID_RESULT;
  }

  return VALID_RESULT;
}

bool BasinConfigSection::IsDuplicateGauge(GaugeConfigSection *gauge) {

  // Scan the vector for duplicates
  for (std::vector<GaugeConfigSection *>::iterator itr = gauges.begin();
       itr != gauges.end(); itr++) {
    if (gauge == (*itr)) {
      return true;
    }
  }

  // No duplicates found!
  return false;
}

bool BasinConfigSection::IsDuplicate(char *name) {
  std::map<std::string, BasinConfigSection *>::iterator itr =
      g_basinConfigs.find(std::string(name));
  if (itr == g_basinConfigs.end()) {
    return false;
  } else {
    return true;
  }
}

void BasinConfigSection::LoadLakesFromCSV(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) return;
    std::string line;
    // Skip header
    std::getline(file, line);
    while (std::getline(file, line)) {
        std::istringstream ss(line);
        std::string name, lat, lon, thVol, area, klake;
        std::getline(ss, name, ',');
        std::getline(ss, lat, ',');
        std::getline(ss, lon, ',');
        std::getline(ss, thVol, ',');
        std::getline(ss, area, ',');
        std::getline(ss, klake, ',');
        LakeConfigSection* lake = new LakeConfigSection(name.c_str());
        lake->ProcessKeyValue((char*)"Lat", (char*)lat.c_str());
        lake->ProcessKeyValue((char*)"Lon", (char*)lon.c_str());
        lake->ProcessKeyValue((char*)"ThVolume", (char*)thVol.c_str());
        lake->ProcessKeyValue((char*)"Area", (char*)area.c_str());
        if (!klake.empty()) {
            lake->ProcessKeyValue((char*)"Klake", (char*)klake.c_str());
        }
        lakes.push_back(lake);
    }
}

void BasinConfigSection::AssignLakesToGridNodes(const std::vector<GridNode>& gridNodes) {
    for (auto* lake : lakes) {
        GridLoc pt;
        if (!g_FAM->GetGridLoc(lake->GetLon(), lake->GetLat(), &pt)) continue;
        float maxFAM = -1e30f;
        long bestX = pt.x, bestY = pt.y;
        // Search 3x3 window around (pt.x, pt.y)
        for (long dy = -1; dy <= 1; ++dy) {
            for (long dx = -1; dx <= 1; ++dx) {
                long x = pt.x + dx;
                long y = pt.y + dy;
                if (x < 0 || y < 0 || x >= g_FAM->numCols || y >= g_FAM->numRows) continue;
                float famVal = g_FAM->data[y][x];
                if (famVal != g_FAM->noData && famVal > maxFAM) {
                    maxFAM = famVal;
                    bestX = x;
                    bestY = y;
                }
            }
        }
        // Find the grid node with (bestX, bestY)
        long foundIdx = -1;
        for (size_t i = 0; i < gridNodes.size(); ++i) {
            if (gridNodes[i].x == bestX && gridNodes[i].y == bestY) {
                foundIdx = static_cast<long>(i);
                break;
            }
        }
        lake->SetGridNodeIndex(foundIdx);
    }
}

void BasinConfigSection::LoadEngineeredDischargeCSV(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) return;
    std::string line;
    // Read header
    if (!std::getline(file, line)) return;
    std::vector<std::string> lakeNames;
    std::istringstream header(line);
    std::string col;
    // First column is time
    std::getline(header, col, ',');
    while (std::getline(header, col, ',')) {
        lakeNames.push_back(col);
    }
    // Warn if any lake in the LakeListFile is missing from DamQ file
    for (auto* lake : lakes) {
        bool found = false;
        for (const auto& name : lakeNames) {
            if (lake->GetName() == name) {
                found = true;
                break;
            }
        }
        if (!found) {
            WARNING_LOGF("Lake '%s' in LakeListFile is missing from DamQ file header.", lake->GetName());
        }
    }
    // Read each row
    while (std::getline(file, line)) {
        std::istringstream ss(line);
        std::string timestamp;
        std::getline(ss, timestamp, ',');
        for (size_t i = 0; i < lakeNames.size(); ++i) {
            std::string val;
            if (!std::getline(ss, val, ',')) break;
            double discharge = atof(val.c_str());
            lakeDischargeTS[lakeNames[i]][timestamp] = discharge;
        }
    }
}

void BasinConfigSection::ProcessLakeInletSection(const std::string& lakeName, float lat, float lon, const std::vector<GridNode>& gridNodes) {
    // Find the lake by name
    for (auto* lake : lakes) {
        if (lake->GetName() == lakeName) {
            // Find nearest channel (highest FAM in a small window around lat/lon)
            GridLoc pt;
            if (!g_FAM->GetGridLoc(lon, lat, &pt)) return;
            float maxFAM = -1e30f;
            long bestIdx = -1;
            for (long dy = -2; dy <= 2; ++dy) {
                for (long dx = -2; dx <= 2; ++dx) {
                    long x = pt.x + dx;
                    long y = pt.y + dy;
                    if (x < 0 || y < 0 || x >= g_FAM->numCols || y >= g_FAM->numRows) continue;
                    float famVal = g_FAM->data[y][x];
                    if (famVal != g_FAM->noData && famVal > maxFAM) {
                        // Find grid node index
                        for (size_t i = 0; i < gridNodes.size(); ++i) {
                            if (gridNodes[i].x == x && gridNodes[i].y == y) {
                                maxFAM = famVal;
                                bestIdx = (long)i;
                            }
                        }
                    }
                }
            }
            if (bestIdx >= 0) {
                lake->AddInlet(lat, lon, bestIdx);
            }
            break;
        }
    }
}

double BasinConfigSection::GetEngineeredDischarge(const std::string& lakeName, const std::string& timestamp) const {
    auto lakeIt = lakeDischargeTS.find(lakeName);
    if (lakeIt == lakeDischargeTS.end()) {
        return -1.0; // Lake not found
    }
    
    auto timeIt = lakeIt->second.find(timestamp);
    if (timeIt == lakeIt->second.end()) {
        return -1.0; // Timestamp not found
    }
    
    return timeIt->second;
}
