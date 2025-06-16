#ifndef CONFIG_BASIN_SECTION_H
#define CONFIG_BASIN_SECTION_H

#include "ConfigSection.h"
#include "Defines.h"
#include "GaugeConfigSection.h"
#include "LakeConfigSection.h"
#include <map>
#include <string>
#include <vector>

class BasinConfigSection : public ConfigSection {

public:
  BasinConfigSection(char *newName);
  ~BasinConfigSection();

  char *GetName() { return name; }
  std::vector<GaugeConfigSection *> *GetGauges() { return &gauges; }
  CONFIG_SEC_RET ProcessKeyValue(char *name, char *value);
  CONFIG_SEC_RET ValidateSection();

  static bool IsDuplicate(char *name);

  void AssignLakesToGridNodes(const std::vector<GridNode>& gridNodes);

  void ProcessLakeInletSection(const std::string& lakeName, float lat, float lon, const std::vector<GridNode>& gridNodes);
  bool customLakeInlets = false;

private:
  bool IsDuplicateGauge(GaugeConfigSection *gauge);
  char name[CONFIG_MAX_LEN];
  std::vector<GaugeConfigSection *> gauges;
  std::string lakeListFile;
  std::string engineeredDischargeFile;
  std::vector<LakeConfigSection*> lakes;
  std::map<std::string, std::map<std::string, double>> lakeDischargeTS; // lakeName -> (timestamp -> discharge)
  void LoadLakesFromCSV(const std::string& filename);
  void LoadEngineeredDischargeCSV(const std::string& filename);
};

extern std::map<std::string, BasinConfigSection *> g_basinConfigs;

#endif
