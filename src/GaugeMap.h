#ifndef GAUGE_MAP_H
#define GAUGE_MAP_H

#include "GaugeConfigSection.h"
#include "GridNode.h"
#include "TimeVar.h"
#include <map>
#include <vector>

class GaugeMap {
public:
  void Initialize(std::vector<GaugeConfigSection *> *newGauges);
  void AddUpstreamGauge(GaugeConfigSection *downStream,
                        GaugeConfigSection *upStream);
  void GaugeAverage(std::vector<GridNode> *nodes,
                    std::vector<float> *currentValue,
                    std::vector<float> *gaugeAvg);
  void GetGaugeArea(std::vector<GridNode> *nodes,
                    std::vector<float> *gaugeArea);
  
  // New methods for saving/loading gauge relationships
  void SaveGaugeRelationships(TimeVar *currentTime, char *statePath);
  bool LoadGaugeRelationships(TimeVar *beginTime, char *statePath);

private:
  std::vector<GaugeConfigSection *> gauges;
  std::vector<std::vector<GaugeConfigSection *> > gaugeTree;
  std::map<GaugeConfigSection *, size_t> gaugeMap;
  std::vector<float> partialVal, partialArea;
};

#endif
