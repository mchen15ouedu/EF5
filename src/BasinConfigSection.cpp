#include "BasinConfigSection.h"
#include "BasicGrids.h"
#include "Config.h"
#include "GaugeConfigSection.h"
#include "LakeConfigSection.h"
#include "LakeModel.h"
#include "Messages.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
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
  
  // Read header line first to determine column structure
  if (!std::getline(file, line)) {
    ERROR_LOGF("Failed to read header from CSV file: %s", filename.c_str());
    return false;
  }
  
  // Remove UTF-8 BOM if present
  if (line.length() >= 3 && 
      (unsigned char)line[0] == 0xEF && 
      (unsigned char)line[1] == 0xBB && 
      (unsigned char)line[2] == 0xBF) {
    line = line.substr(3);
  }
  
  // Parse header to determine column positions (case-insensitive)
  std::stringstream headerSS(line);
  std::string token;
  std::vector<std::string> headers;
  
  while (std::getline(headerSS, token, ',')) {
    // Trim whitespace from header
    token.erase(0, token.find_first_not_of(" \t\r\n"));
    token.erase(token.find_last_not_of(" \t\r\n") + 1);
    headers.push_back(token);
  }
  
  // Determine column positions (case-insensitive)
  int nameCol = -1, latCol = -1, lonCol = -1, thVolCol = -1, areaCol = -1, klakeCol = -1, obsFamCol = -1, outputtsCol = -1;
  
  for (size_t i = 0; i < headers.size(); i++) {
    std::string header = headers[i];
    std::transform(header.begin(), header.end(), header.begin(), ::tolower);
    
    if (header == "name" || header == "id") {
      nameCol = i;
    }
    else if (header == "lat" || header == "latitude") {
      latCol = i;
    }
    else if (header == "lon" || header == "longitude") {
      lonCol = i;
    }
    else if (header == "th_volume" || header == "volume" || header == "thvolume") {
      thVolCol = i;
    }
    else if (header == "area") {
      areaCol = i;
    }
    else if (header == "klake" || header == "retention_constant") {
      klakeCol = i;
    }
    else if (header == "obsfam" || header == "obs_fam" || header == "obsflowaccum") {
      obsFamCol = i;
    }
    else if (header == "outputts" || header == "output_ts" || header == "output_timeseries") {
      outputtsCol = i;
    }
  }
  
  // Validate required columns
  if (nameCol == -1 || latCol == -1 || lonCol == -1) {
    ERROR_LOGF("Invalid lakes CSV header in file: %s - missing required columns (name, lat, lon)", filename.c_str());
    return false;
  }

  // Read data lines
  while (std::getline(file, line)) {
    std::stringstream ss(line);
    std::string token;
    LakeInfo lake;
    
    // Initialize with default values
    lake.obsFlowAccum = 0.0;
    lake.obsFlowAccumSet = false;
    lake.outputts = false;
    lake.retention_constant = 24.0; // Default value
    
    // Parse all columns into a vector
    std::vector<std::string> values;
    while (std::getline(ss, token, ',')) {
      values.push_back(token);
    }
    
    // Extract values based on column positions
    if (nameCol >= 0 && nameCol < (int)values.size()) {
      lake.name = values[nameCol];
    } else {
      printf("DEBUG: Skipping line - no name column found\n");
      continue;
    }
    
    if (latCol >= 0 && latCol < (int)values.size()) {
      lake.lat = atof(values[latCol].c_str());
    }
    
    if (lonCol >= 0 && lonCol < (int)values.size()) {
      lake.lon = atof(values[lonCol].c_str());
    }
    
    if (thVolCol >= 0 && thVolCol < (int)values.size()) {
      // Convert km³ to m³ (multiply by 1e9)
      lake.th_volume = atof(values[thVolCol].c_str()) * 1e9;
    }
    
    if (areaCol >= 0 && areaCol < (int)values.size()) {
      // Convert km² to m² (multiply by 1e6)
      lake.area = atof(values[areaCol].c_str()) * 1e6;
    }
    
    if (klakeCol >= 0 && klakeCol < (int)values.size()) {
      lake.retention_constant = atof(values[klakeCol].c_str());
    }
    
    if (obsFamCol >= 0 && obsFamCol < (int)values.size()) {
      std::string obsFamStr = values[obsFamCol];
      if (!obsFamStr.empty() && obsFamStr.find_first_not_of(" \t\r\n") != std::string::npos) {
        lake.obsFlowAccum = atof(obsFamStr.c_str());
        lake.obsFlowAccumSet = true;
      }
    }
    
    if (outputtsCol >= 0 && outputtsCol < (int)values.size()) {
      std::string outputtsStr = values[outputtsCol];
      // Trim whitespace
      outputtsStr.erase(0, outputtsStr.find_first_not_of(" \t\r\n"));
      outputtsStr.erase(outputtsStr.find_last_not_of(" \t\r\n") + 1);
      
      // Check for Y, y, TRUE, true, 1 (case-insensitive)
      std::string valStr = outputtsStr;
      std::transform(valStr.begin(), valStr.end(), valStr.begin(), ::tolower);
      lake.outputts = (valStr == "y" || valStr == "yes" || valStr == "true" || valStr == "1");
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
    lakeInfo.outputts = lakeSec->GetOutputTS();
    
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
