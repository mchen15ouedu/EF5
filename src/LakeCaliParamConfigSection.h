#ifndef CONFIG_LAKE_CALI_PARAM_SECTION_H
#define CONFIG_LAKE_CALI_PARAM_SECTION_H

#include "CaliParamConfigSection.h"
#include "Model.h"
#include <string>

class LakeCaliParamConfigSection : public CaliParamConfigSection {
public:
  LakeCaliParamConfigSection(const char *nameVal);
  ~LakeCaliParamConfigSection();

  CONFIG_SEC_RET ProcessKeyValue(char *name, char *value);
  CONFIG_SEC_RET ValidateSection();

  static bool IsDuplicate(char *name);

  // Get the lake name being calibrated
  const std::string& GetCalibratedLakeName() const { return calibratedLakeName; }
  
  // Get the number of lake parameters
  int GetNumParams() const { return numLakeParams[0]; }
  
  // Get parameter name string
  const char* GetParamString(int index) const { 
    if (index == 0) return "klake"; 
    return "unknown"; 
  }

private:
  std::string calibratedLakeName; // Name of the lake being calibrated
};

extern std::map<std::string, LakeCaliParamConfigSection *> g_lakeCaliParamConfigs;

#endif 