#include "LakeCaliParamConfigSection.h"
#include "Messages.h"
#include <cstring>

std::map<std::string, LakeCaliParamConfigSection *> g_lakeCaliParamConfigs;

LakeCaliParamConfigSection::LakeCaliParamConfigSection(const char *nameVal)
    : CaliParamConfigSection(nameVal) {
  // Initialize with lake parameter definitions from Model.h
  numParams = numLakeParams[0]; // Assuming lake parameters are the same for all lake types
  paramMins = new float[numParams];
  paramMaxs = new float[numParams];
  paramInits = new float[numParams];

  // Set default parameter ranges for lake parameters
  // These should match the definitions in Models.tbl
  for (int i = 0; i < numParams; i++) {
    paramMins[i] = 0.0f;   // Default minimum
    paramMaxs[i] = 10.0f;  // Default maximum
    paramInits[i] = 1.0f;  // Default initial value (will be overridden by lakes.csv)
  }
}

LakeCaliParamConfigSection::~LakeCaliParamConfigSection() {
  // Memory cleanup is handled by the parent class
}

CONFIG_SEC_RET LakeCaliParamConfigSection::ProcessKeyValue(char *name, char *value) {
  // Handle lake name specification
  if (!strcasecmp(name, "lake_name")) {
    calibratedLakeName = value;
    return VALID_RESULT;
  }
  
  // Handle lake-specific calibration parameter settings
  if (!strcasecmp(name, "klake")) {
    // Parse klake=<min>,<max> format
    char* comma = strchr(value, ',');
    if (comma != NULL) {
      *comma = '\0'; // Split the string
      paramMins[0] = (float)atof(value);
      paramMaxs[0] = (float)atof(comma + 1);
      return VALID_RESULT;
    } else {
      ERROR_LOG("klake parameter must be in format: klake=<min>,<max>");
      return INVALID_RESULT;
    }
  }

  // Let the parent class handle other parameters
  return CaliParamConfigSection::ProcessKeyValue(name, value);
}

CONFIG_SEC_RET LakeCaliParamConfigSection::ValidateSection() {
  // Validate that a lake name is specified
  if (calibratedLakeName.empty()) {
    ERROR_LOG("Lake calibration section must specify a lake_name!");
    return INVALID_RESULT;
  }

  // Validate lake calibration parameters
  if (numParams <= 0) {
    ERROR_LOG("Lake calibration section must have at least one parameter!");
    return INVALID_RESULT;
  }

  for (int i = 0; i < numParams; i++) {
    if (paramMins[i] >= paramMaxs[i]) {
      ERROR_LOGF("Lake parameter %d: minimum value must be less than maximum value!", i);
      return INVALID_RESULT;
    }
  }

  return VALID_RESULT;
}

bool LakeCaliParamConfigSection::IsDuplicate(char *name) {
  std::map<std::string, LakeCaliParamConfigSection *>::iterator itr =
      g_lakeCaliParamConfigs.find(name);
  return (itr != g_lakeCaliParamConfigs.end());
} 