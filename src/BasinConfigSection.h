#ifndef CONFIG_BASIN_SECTION_H
#define CONFIG_BASIN_SECTION_H

#include "ConfigSection.h"
#include "Defines.h"
#include "GaugeConfigSection.h"
#include <map>
#include <string>
#include <vector>

// Forward declaration
struct LakeInfo;

class BasinConfigSection : public ConfigSection {

public:
  BasinConfigSection(char *newName);
  ~BasinConfigSection();

  char *GetName() { return name; }
  std::vector<GaugeConfigSection *> *GetGauges() { return &gauges; }
  std::vector<LakeInfo> *GetLakes() { return &lakes; }
  std::map<std::string, double> *GetEngineeredDischarge() { return &engineeredDischarge; }
  CONFIG_SEC_RET ProcessKeyValue(char *name, char *value);
  CONFIG_SEC_RET ValidateSection();

  static bool IsDuplicate(char *name);

private:
  bool IsDuplicateGauge(GaugeConfigSection *gauge);
  char name[CONFIG_MAX_LEN];
  std::vector<GaugeConfigSection *> gauges;
  std::vector<LakeInfo> lakes;
  std::map<std::string, double> engineeredDischarge;
};

extern std::map<std::string, BasinConfigSection *> g_basinConfigs;

#endif
