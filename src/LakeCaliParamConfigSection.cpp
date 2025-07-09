#include "LakeCaliParamConfigSection.h"
#include "Messages.h"
#include "Model.h"
#include <cstring>
#include <cstdlib>

std::map<std::string, LakeCaliParamConfigSection *> g_lakeCaliParamConfigs;

LakeCaliParamConfigSection::LakeCaliParamConfigSection(const char *nameVal)
    : CaliParamConfigSection((char*)nameVal, MODEL_LAKE) {
  // Initialize with lake parameter definitions from Model.h
  int numLakeParam = numLakeParams[0]; // Assuming lake parameters are the same for all lake types
  float *paramMins = GetParamMins();
  float *paramMaxs = GetParamMaxs();

  // Set default parameter ranges for lake parameters
  // These should match the definitions in Models.tbl
  // Note: Lake parameters don't need initial values as they come from lakes.csv
  for (int i = 0; i < numLakeParam; i++) {
    paramMins[i] = 0.0f;   // Default minimum
    paramMaxs[i] = 10.0f;  // Default maximum
  }
}

LakeCaliParamConfigSection::~LakeCaliParamConfigSection() {
  // Memory cleanup is handled by the parent class
}

CONFIG_SEC_RET LakeCaliParamConfigSection::ProcessKeyValue(char *name, char *value) {
  // Handle lake name specification
  if (!strcasecmp(name, "lake_name") || !strcasecmp(name, "lakename") || !strcasecmp(name, "LakeName")) {
    calibratedLakeName = value;
    return VALID_RESULT;
  }
  
  // Handle lake-specific calibration parameter settings
  if (!strcasecmp(name, "klake")) {
    // Parse klake=<min>,<max> format
    char* comma = strchr(value, ',');
    if (comma != NULL) {
      *comma = '\0'; // Split the string
      float *paramMins = GetParamMins();
      float *paramMaxs = GetParamMaxs();
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
  int numLakeParam = numLakeParams[0];
  if (numLakeParam <= 0) {
    ERROR_LOG("Lake calibration section must have at least one parameter!");
    return INVALID_RESULT;
  }

  float *paramMins = GetParamMins();
  float *paramMaxs = GetParamMaxs();
  for (int i = 0; i < numLakeParam; i++) {
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