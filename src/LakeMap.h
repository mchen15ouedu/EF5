#ifndef LAKE_MAP_H
#define LAKE_MAP_H

#include "LakeConfigSection.h"
#include "LakeModel.h"
#include "InletConfigSection.h"
#include "Grid.h"
#include <vector>
#include <map>

class LakeMap {
public:
  void Initialize(std::vector<LakeModelImpl *> *newLakes);
  void FindLakeLocations();
  void FindUpstreamNeighbors();
  std::vector<GridLoc> GetUpstreamNeighbors(LakeModelImpl *lake);
  float CalculateInflow(LakeModelImpl *lake, std::vector<float> *currentQ, std::vector<GridNode> *nodes, TimeVar *currentTime);
  void InitializeInlets(std::vector<InletConfigSection *> *inlets);
  
  // New methods for saving/loading lake relationships
  void SaveLakeRelationships(TimeVar *currentTime, char *statePath);
  bool LoadLakeRelationships(TimeVar *beginTime, char *statePath);

private:
  std::vector<LakeModelImpl *> lakes;
  std::map<LakeModelImpl *, size_t> lakeMap;
  std::vector<std::vector<GridLoc> > lakeNeighbors;
  std::vector<std::vector<InletConfigSection *> > lakeInlets;
};

#endif 