#include "InletConfigSection.h"
#include "Messages.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

std::map<std::string, InletConfigSection *> g_inletConfigs;

InletConfigSection::InletConfigSection(char *nameVal) {
  latSet = false;
  lonSet = false;
  xSet = false;
  ySet = false;
  lakeNameSet = false;
  inletQSet = false;
  strcpy(name, nameVal);
  lakeName[0] = 0;
  inletQFile[0] = 0;
}

InletConfigSection::~InletConfigSection() {}

char *InletConfigSection::GetName() { return name; }

void InletConfigSection::LoadTS() {
  if (inletQFile[0]) {
    obs.LoadTimeSeries(inletQFile);
  }
}

float InletConfigSection::GetObserved(TimeVar *currentTime) {
  if (!obs.GetNumberOfObs()) {
    return std::numeric_limits<float>::quiet_NaN();
  }
  return obs.GetValueAtTime(currentTime);
}

float InletConfigSection::GetObserved(TimeVar *currentTime, float diff) {
  if (!obs.GetNumberOfObs()) {
    return std::numeric_limits<float>::quiet_NaN();
  }
  return obs.GetValueNearTime(currentTime, diff);
}

void InletConfigSection::SetObservedValue(char *timeBuffer, float dataValue) {
  obs.PutValueAtTime(timeBuffer, dataValue);
}

CONFIG_SEC_RET InletConfigSection::ProcessKeyValue(char *name, char *value) {

  if (!strcasecmp(name, "lat")) {
    lat = strtod(value, NULL);
    latSet = true;
  } else if (!strcasecmp(name, "lon")) {
    lon = strtod(value, NULL);
    lonSet = true;
  } else if (!strcasecmp(name, "cellx")) {
    SetCellX(atoi(value));
    xSet = true;
  } else if (!strcasecmp(name, "celly")) {
    SetCellY(atoi(value));
    ySet = true;
  } else if (!strcasecmp(name, "lakename")) {
    strcpy(lakeName, value);
    lakeNameSet = true;
  } else if (!strcasecmp(name, "inletq")) {
    strcpy(inletQFile, value);
    inletQSet = true;
  } else {
    ERROR_LOGF("Unknown key value \"%s=%s\" in inlet %s!", name, value,
               this->name);
    return INVALID_RESULT;
  }
  return VALID_RESULT;
}

CONFIG_SEC_RET InletConfigSection::ValidateSection() {
  if (!latSet && !ySet) {
    ERROR_LOG("The latitude was not specified");
    return INVALID_RESULT;
  } else if (!lonSet && !xSet) {
    ERROR_LOG("The longitude was not specified");
    return INVALID_RESULT;
  } else if (!lakeNameSet) {
    ERROR_LOG("The lake name was not specified");
    return INVALID_RESULT;
  } else if (!inletQSet) {
    ERROR_LOG("The inletQ file was not specified");
    return INVALID_RESULT;
  }

  return VALID_RESULT;
}

bool InletConfigSection::IsDuplicate(char *name) {
  std::map<std::string, InletConfigSection *>::iterator itr =
      g_inletConfigs.find(name);
  if (itr == g_inletConfigs.end()) {
    return false;
  } else {
    return true;
  }
} 