#include "BasinConfigSection.h"
#include "BasicGrids.h"
#include "Config.h"
#include "GaugeConfigSection.h"
#include "LakeConfigSection.h"
#include "LakeModel.h"
#include "Messages.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/stat.h>

std::map<std::string, BasinConfigSection *> g_basinConfigs;

// Helper function to read lakes from CSV file
static bool ReadLakesFromCSV(const std::string& filename, std::vector<LakeInfo>& lakes) {
  // Check if directory exists
  std::string dirPath = filename;
  size_t lastSlash = dirPath.find_last_of("/\\");
  if (lastSlash != std::string::npos) {
    dirPath = dirPath.substr(0, lastSlash);
    if (!dirPath.empty()) {
      struct stat path_stat;
      if (stat(dirPath.c_str(), &path_stat) != 0) {
        ERROR_LOGF("Directory does not exist for lakes CSV file: %s", dirPath.c_str());
        return false;
      }
      if (!S_ISDIR(path_stat.st_mode)) {
        ERROR_LOGF("Path exists but is not a directory for lakes CSV file: %s", dirPath.c_str());
        return false;
      }
      INFO_LOGF("Directory validated for lakes CSV file: %s", dirPath.c_str());
    }
  }
  
  std::ifstream file(filename.c_str());
  if (!file.is_open()) {
    ERROR_LOGF("Failed to open lakes CSV file: %s", filename.c_str());
    return false;
  }

  std::string line;
  // Skip header line
  if (std::getline(file, line)) {
    // Check if header is valid
    if (line.find("name") == std::string::npos || 
        line.find("lat") == std::string::npos || 
        line.find("lon") == std::string::npos) {
      ERROR_LOGF("Invalid lakes CSV header in file: %s", filename.c_str());
      return false;
    }
  }

  // Read data lines
  while (std::getline(file, line)) {
    std::stringstream ss(line);
    std::string token;
    LakeInfo lake;

    // Parse name
    if (!std::getline(ss, token, ',')) continue;
    lake.name = token;

    // Parse lat
    if (!std::getline(ss, token, ',')) continue;
    lake.lat = atof(token.c_str());

    // Parse lon
    if (!std::getline(ss, token, ',')) continue;
    lake.lon = atof(token.c_str());

    // Parse th_volume
    if (!std::getline(ss, token, ',')) continue;
    lake.th_volume = atof(token.c_str());

    // Parse area
    if (!std::getline(ss, token, ',')) continue;
    lake.area = atof(token.c_str());

    // Parse klake (optional)
    if (std::getline(ss, token, ',')) {
      lake.retention_constant = atof(token.c_str());
    } else {
      lake.retention_constant = 24.0; // Default value
    }

    lakes.push_back(lake);
  }

  file.close();
  INFO_LOGF("Successfully loaded %lu lakes from %s", (unsigned long)lakes.size(), filename.c_str());
  return true;
}

// Helper function to read engineered discharge from CSV file
static bool ReadEngineeredDischargeFromCSV(const std::string& filename, std::map<std::string, double>& engineeredDischarge) {
  // Check if directory exists
  std::string dirPath = filename;
  size_t lastSlash = dirPath.find_last_of("/\\");
  if (lastSlash != std::string::npos) {
    dirPath = dirPath.substr(0, lastSlash);
    if (!dirPath.empty()) {
      struct stat path_stat;
      if (stat(dirPath.c_str(), &path_stat) != 0) {
        ERROR_LOGF("Directory does not exist for engineered discharge CSV file: %s", dirPath.c_str());
        return false;
      }
      if (!S_ISDIR(path_stat.st_mode)) {
        ERROR_LOGF("Path exists but is not a directory for engineered discharge CSV file: %s", dirPath.c_str());
        return false;
      }
      INFO_LOGF("Directory validated for engineered discharge CSV file: %s", dirPath.c_str());
    }
  }
  
  std::ifstream file(filename.c_str());
  if (!file.is_open()) {
    ERROR_LOGF("Failed to open engineered discharge CSV file: %s", filename.c_str());
    return false;
  }

  std::string line;
  std::vector<std::string> lakeNames;
  
  // Read header line to get lake names
  if (std::getline(file, line)) {
    std::stringstream ss(line);
    std::string token;
    
    // Skip timestamp column
    if (!std::getline(ss, token, ',')) return false;
    
    // Read lake names from header
    while (std::getline(ss, token, ',')) {
      lakeNames.push_back(token);
    }
  }

  // Read first data line (we'll use the first timestamp for now)
  if (std::getline(file, line)) {
    std::stringstream ss(line);
    std::string token;

    // Skip timestamp
    if (!std::getline(ss, token, ',')) return false;

    // Parse discharge values for each lake
    int lakeIndex = 0;
    while (std::getline(ss, token, ',') && lakeIndex < (int)lakeNames.size()) {
      double discharge = atof(token.c_str());
      if (discharge != 0.0 || token == "0" || token == "0.0") {
        engineeredDischarge[lakeNames[lakeIndex]] = discharge;
      } else {
        WARNING_LOGF("Failed to parse discharge value '%s' for lake '%s'", token.c_str(), lakeNames[lakeIndex].c_str());
      }
      lakeIndex++;
    }
  }

  file.close();
  INFO_LOGF("Successfully loaded %lu engineered discharge values from %s", (unsigned long)engineeredDischarge.size(), filename.c_str());
  return true;
}

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
  } else if (strcasecmp(name, "lakes_csv") == 0 || strcasecmp(name, "lakelistfile") == 0) {
    // Read lakes from CSV file
    if (!ReadLakesFromCSV(value, lakes)) {
      ERROR_LOGF("Failed to read lakes from CSV file: %s", value);
      return INVALID_RESULT;
    }
  } else if (strcasecmp(name, "engineered_discharge_csv") == 0 || strcasecmp(name, "lakeoutflowfile") == 0 || strcasecmp(name, "damq") == 0) {
    // Read engineered discharge from CSV file
    if (!ReadEngineeredDischargeFromCSV(value, engineeredDischarge)) {
      ERROR_LOGF("Failed to read engineered discharge from CSV file: %s", value);
      return INVALID_RESULT;
    }
  } else if (strcasecmp(name, "lake") == 0) {
    // Reference a lake defined in a LakeConfigSection
    TOLOWER(value);
    std::map<std::string, LakeConfigSection *>::iterator itr = g_lakeConfigs.find(value);
    if (itr == g_lakeConfigs.end()) {
      ERROR_LOGF("Unknown lake \"%s\" in basin!", value);
      return INVALID_RESULT;
    }
    
    // Convert LakeConfigSection to LakeInfo and add to lakes vector
    LakeConfigSection *lakeSec = itr->second;
    LakeInfo lakeInfo;
    lakeInfo.name = lakeSec->GetName();
    lakeInfo.lat = static_cast<double>(lakeSec->GetLat());
    lakeInfo.lon = static_cast<double>(lakeSec->GetLon());
    lakeInfo.th_volume = static_cast<double>(lakeSec->GetThVolume());
    lakeInfo.area = static_cast<double>(lakeSec->GetArea());
    lakeInfo.retention_constant = static_cast<double>(lakeSec->GetRetentionConstant());
    lakeInfo.obsFlowAccum = static_cast<double>(lakeSec->GetObsFlowAccum());
    lakeInfo.obsFlowAccumSet = lakeSec->HasObsFlowAccum();
    
    lakes.push_back(lakeInfo);
    INFO_LOGF("Added lake %s from LakeConfigSection to basin", lakeInfo.name.c_str());
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
