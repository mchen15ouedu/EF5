#ifndef CONFIG_INLET_SECTION_H
#define CONFIG_INLET_SECTION_H

#include "ConfigSection.h"
#include "Defines.h"
#include "Grid.h"
#include "TimeSeries.h"
#include "TimeVar.h"
#include <map>
#include <string>

class InletConfigSection : public ConfigSection {

public:
  InletConfigSection(char *nameVal);
  ~InletConfigSection();

  char *GetName();
  float GetLat() { return lat; }
  float GetLon() { return lon; }
  long GetGridNodeIndex() { return gridNodeIndex; }
  char *GetLakeName() { return lakeName; }
  char *GetInletQFile() { return inletQFile; }
  GridLoc *GetGridLoc() { return &gridLoc; }
  float GetObserved(TimeVar *currentTime);
  float GetObserved(TimeVar *currentTime, float diff);
  void SetObservedValue(char *timeBuffer, float dataValue);
  void LoadTS();
  void SetGridNodeIndex(long newVal) { gridNodeIndex = newVal; }
  void SetLat(float newVal) { lat = newVal; }
  void SetLon(float newVal) { lon = newVal; }
  void SetCellX(long newX) { gridLoc.x = newX; }
  void SetCellY(long newY) { gridLoc.y = newY; }
  bool NeedsProjecting() { return latSet; }
  CONFIG_SEC_RET ProcessKeyValue(char *name, char *value);
  CONFIG_SEC_RET ValidateSection();

  static bool IsDuplicate(char *name);

private:
  bool latSet, lonSet, xSet, ySet, lakeNameSet, inletQSet;
  char lakeName[CONFIG_MAX_LEN];
  char inletQFile[CONFIG_MAX_LEN];
  char name[CONFIG_MAX_LEN];
  float lat;
  float lon;
  TimeSeries obs;

  // These are for basin carving procedures!
  long gridNodeIndex;
  GridLoc gridLoc;
};

extern std::map<std::string, InletConfigSection *> g_inletConfigs;

#endif 