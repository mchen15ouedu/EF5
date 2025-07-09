#include "LakeMap.h"
#include "BasicGrids.h"
#include "Messages.h"
#include <cstdio>
#include <cmath>
#include <strings.h>
#include <fstream> // Required for SaveLakeRelationships
#include <sstream> // Required for LoadLakeRelationships
#include <algorithm> // Required for std::transform

void LakeMap::Initialize(std::vector<LakeModelImpl *> *newLakes) {
  // Copy the list of lakes over to internal storage
  lakes = (*newLakes);

  size_t countLakes = lakes.size();

  // Initialize the map that contains the index into a vector for each LakeModelImpl *
  for (size_t i = 0; i < countLakes; i++) {
    lakeMap[lakes[i]] = i;
  }

  // Initialize storage for the upstream neighbors of each lake
  lakeNeighbors.resize(countLakes);
  
  // Initialize storage for the inlets of each lake
  lakeInlets.resize(countLakes);
}

void LakeMap::FindLakeLocations() {
  for (size_t i = 0; i < lakes.size(); i++) {
    LakeModelImpl *lake = lakes[i];
    GridLoc *loc = lake->GetLocation();
    
    // Convert lat/lon to grid coordinates using same logic as gauge location finding
    if (!g_FAM->GetGridLoc(lake->GetLon(), lake->GetLat(), loc)) {
      WARNING_LOGF("Lake %s is outside the basic grid domain!\n", lake->GetName());
      continue;
    }
    
    // If lake has observed flow accumulation, search for best FAM match (same as gauges)
    if (lake->HasObsFlowAccum()) {
      float areaFactor = 1.0 / g_Projection->GetArea(lake->GetLon(), lake->GetLat());
      int maxDist = 20000.0 / g_Projection->GetLen(lake->GetLon(), lake->GetLat(), FLOW_NORTH);
      if (maxDist < 2) {
        maxDist = 2;
      }
      
      GridLoc minLoc;
      float testError, minError = pow((float)(g_FAM->data[loc->y][loc->x]) - 
                                        lake->GetObsFlowAccum() * areaFactor, 2.0);
      minLoc.x = loc->x;
      minLoc.y = loc->y;
      long testX, testY;
      
      for (int dist = 1; dist < maxDist; dist++) {
        // Check 8 directions in spiral pattern (same as gauge logic)
        testX = loc->x + 1 * dist;
        testY = loc->y;
        if (testX < g_FAM->numCols && g_FAM->data[testY][testX] != g_FAM->noData) {
          testError = pow((float)(g_FAM->data[testY][testX]) - lake->GetObsFlowAccum() * areaFactor, 2.0);
          if (minError > testError) {
            minError = testError;
            minLoc.x = testX;
            minLoc.y = testY;
          }
        }
        
        testX = loc->x + 1 * dist;
        testY = loc->y + 1 * dist;
        if (testX < g_FAM->numCols && testY < g_FAM->numRows && g_FAM->data[testY][testX] != g_FAM->noData) {
          testError = pow((float)(g_FAM->data[testY][testX]) - lake->GetObsFlowAccum() * areaFactor, 2.0);
          if (minError > testError) {
            minError = testError;
            minLoc.x = testX;
            minLoc.y = testY;
          }
        }
        
        testX = loc->x;
        testY = loc->y + 1 * dist;
        if (testY < g_FAM->numRows && g_FAM->data[testY][testX] != g_FAM->noData) {
          testError = pow((float)(g_FAM->data[testY][testX]) - lake->GetObsFlowAccum() * areaFactor, 2.0);
          if (minError > testError) {
            minError = testError;
            minLoc.x = testX;
            minLoc.y = testY;
          }
        }
        
        testX = loc->x - 1 * dist;
        testY = loc->y + 1 * dist;
        if (testX > 0 && testY < g_FAM->numRows && g_FAM->data[testY][testX] != g_FAM->noData) {
          testError = pow((float)(g_FAM->data[testY][testX]) - lake->GetObsFlowAccum() * areaFactor, 2.0);
          if (minError > testError) {
            minError = testError;
            minLoc.x = testX;
            minLoc.y = testY;
          }
        }
        
        testX = loc->x - 1 * dist;
        testY = loc->y;
        if (testX > 0 && g_FAM->data[testY][testX] != g_FAM->noData) {
          testError = pow((float)(g_FAM->data[testY][testX]) - lake->GetObsFlowAccum() * areaFactor, 2.0);
          if (minError > testError) {
            minError = testError;
            minLoc.x = testX;
            minLoc.y = testY;
          }
        }
        
        testX = loc->x - 1 * dist;
        testY = loc->y - 1 * dist;
        if (testX > 0 && testY > 0 && g_FAM->data[testY][testX] != g_FAM->noData) {
          testError = pow((float)(g_FAM->data[testY][testX]) - lake->GetObsFlowAccum() * areaFactor, 2.0);
          if (minError > testError) {
            minError = testError;
            minLoc.x = testX;
            minLoc.y = testY;
          }
        }
        
        testX = loc->x;
        testY = loc->y - 1 * dist;
        if (testY > 0 && g_FAM->data[testY][testX] != g_FAM->noData) {
          testError = pow((float)(g_FAM->data[testY][testX]) - lake->GetObsFlowAccum() * areaFactor, 2.0);
          if (minError > testError) {
            minError = testError;
            minLoc.x = testX;
            minLoc.y = testY;
          }
        }
        
        testX = loc->x + 1 * dist;
        testY = loc->y - 1 * dist;
        if (testX < g_FAM->numCols && testY > 0 && g_FAM->data[testY][testX] != g_FAM->noData) {
          testError = pow((float)(g_FAM->data[testY][testX]) - lake->GetObsFlowAccum() * areaFactor, 2.0);
          if (minError > testError) {
            minError = testError;
            minLoc.x = testX;
            minLoc.y = testY;
          }
        }
      }
      
      loc->x = minLoc.x;
      loc->y = minLoc.y;
    } else {
      // If no observed flow accumulation provided, search for largest FAM value (up to 50 steps)
      int maxDist = 50;
      GridLoc maxLoc;
      long maxFAM = g_FAM->data[loc->y][loc->x];
      maxLoc.x = loc->x;
      maxLoc.y = loc->y;
      long testX, testY;
      
      for (int dist = 1; dist < maxDist; dist++) {
        // Check 8 directions in spiral pattern
        testX = loc->x + 1 * dist;
        testY = loc->y;
        if (testX < g_FAM->numCols && g_FAM->data[testY][testX] != g_FAM->noData) {
          if (g_FAM->data[testY][testX] > maxFAM) {
            maxFAM = g_FAM->data[testY][testX];
            maxLoc.x = testX;
            maxLoc.y = testY;
          }
        }
        
        testX = loc->x + 1 * dist;
        testY = loc->y + 1 * dist;
        if (testX < g_FAM->numCols && testY < g_FAM->numRows && g_FAM->data[testY][testX] != g_FAM->noData) {
          if (g_FAM->data[testY][testX] > maxFAM) {
            maxFAM = g_FAM->data[testY][testX];
            maxLoc.x = testX;
            maxLoc.y = testY;
          }
        }
        
        testX = loc->x;
        testY = loc->y + 1 * dist;
        if (testY < g_FAM->numRows && g_FAM->data[testY][testX] != g_FAM->noData) {
          if (g_FAM->data[testY][testX] > maxFAM) {
            maxFAM = g_FAM->data[testY][testX];
            maxLoc.x = testX;
            maxLoc.y = testY;
          }
        }
        
        testX = loc->x - 1 * dist;
        testY = loc->y + 1 * dist;
        if (testX > 0 && testY < g_FAM->numRows && g_FAM->data[testY][testX] != g_FAM->noData) {
          if (g_FAM->data[testY][testX] > maxFAM) {
            maxFAM = g_FAM->data[testY][testX];
            maxLoc.x = testX;
            maxLoc.y = testY;
          }
        }
        
        testX = loc->x - 1 * dist;
        testY = loc->y;
        if (testX > 0 && g_FAM->data[testY][testX] != g_FAM->noData) {
          if (g_FAM->data[testY][testX] > maxFAM) {
            maxFAM = g_FAM->data[testY][testX];
            maxLoc.x = testX;
            maxLoc.y = testY;
          }
        }
        
        testX = loc->x - 1 * dist;
        testY = loc->y - 1 * dist;
        if (testX > 0 && testY > 0 && g_FAM->data[testY][testX] != g_FAM->noData) {
          if (g_FAM->data[testY][testX] > maxFAM) {
            maxFAM = g_FAM->data[testY][testX];
            maxLoc.x = testX;
            maxLoc.y = testY;
          }
        }
        
        testX = loc->x;
        testY = loc->y - 1 * dist;
        if (testY > 0 && g_FAM->data[testY][testX] != g_FAM->noData) {
          if (g_FAM->data[testY][testX] > maxFAM) {
            maxFAM = g_FAM->data[testY][testX];
            maxLoc.x = testX;
            maxLoc.y = testY;
          }
        }
        
        testX = loc->x + 1 * dist;
        testY = loc->y - 1 * dist;
        if (testX < g_FAM->numCols && testY > 0 && g_FAM->data[testY][testX] != g_FAM->noData) {
          if (g_FAM->data[testY][testX] > maxFAM) {
            maxFAM = g_FAM->data[testY][testX];
            maxLoc.x = testX;
            maxLoc.y = testY;
          }
        }
      }
      
      loc->x = maxLoc.x;
      loc->y = maxLoc.y;
    }
    
    INFO_LOGF("Lake %s (%f, %f; %ld, %ld): FAM %f", lake->GetName(),
              lake->GetLat(), lake->GetLon(), loc->y, loc->x,
              (float)g_FAM->data[loc->y][loc->x]);
  }
}

void LakeMap::FindUpstreamNeighbors() {
  for (size_t i = 0; i < lakes.size(); i++) {
    LakeModelImpl *lake = lakes[i];
    GridLoc *loc = lake->GetLocation();
    std::vector<GridLoc> &neighbors = lakeNeighbors[i];
    neighbors.clear();
    
    // Check 8 neighboring cells for flow direction
    for (int dy = -1; dy <= 1; dy++) {
      for (int dx = -1; dx <= 1; dx++) {
        if (dx == 0 && dy == 0) continue; // Skip center cell
        
        int nx = loc->x + dx;
        int ny = loc->y + dy;
        
        // Check bounds
        if (nx < 0 || nx >= g_DEM->numCols || ny < 0 || ny >= g_DEM->numRows) {
          continue;
        }
        
        // Check if this neighbor flows into the lake cell
        // Flow direction is encoded in g_DDM->data[ny][nx]
        FLOW_DIR neighborFlowDir = (FLOW_DIR)g_DDM->data[ny][nx];
        bool flowsToLake = false;
        
        switch (neighborFlowDir) {
          case FLOW_NORTH:
            flowsToLake = (dx == 0 && dy == 1);
            break;
          case FLOW_NORTHEAST:
            flowsToLake = (dx == 1 && dy == 1);
            break;
          case FLOW_EAST:
            flowsToLake = (dx == 1 && dy == 0);
            break;
          case FLOW_SOUTHEAST:
            flowsToLake = (dx == 1 && dy == -1);
            break;
          case FLOW_SOUTH:
            flowsToLake = (dx == 0 && dy == -1);
            break;
          case FLOW_SOUTHWEST:
            flowsToLake = (dx == -1 && dy == -1);
            break;
          case FLOW_WEST:
            flowsToLake = (dx == -1 && dy == 0);
            break;
          case FLOW_NORTHWEST:
            flowsToLake = (dx == -1 && dy == 1);
            break;
          default:
            flowsToLake = false;
            break;
        }
        
        if (flowsToLake) {
          GridLoc neighbor;
          neighbor.x = nx;
          neighbor.y = ny;
          neighbors.push_back(neighbor);
        }
      }
    }
    
    INFO_LOGF("Lake %s has %lu upstream neighbors", lake->GetName(), (unsigned long)neighbors.size());
  }
}

std::vector<GridLoc> LakeMap::GetUpstreamNeighbors(LakeModelImpl *lake) {
  std::map<LakeModelImpl *, size_t>::iterator itr = lakeMap.find(lake);
  if (itr == lakeMap.end()) {
    return std::vector<GridLoc>();
  }
  return lakeNeighbors[itr->second];
}

float LakeMap::CalculateInflow(LakeModelImpl *lake, std::vector<float> *currentQ, std::vector<GridNode> *nodes, TimeVar *currentTime) {
  // First check if this lake has inlets configured
  std::map<LakeModelImpl *, size_t>::iterator itr = lakeMap.find(lake);
  if (itr != lakeMap.end()) {
    size_t lakeIndex = itr->second;
    if (lakeIndex < lakeInlets.size() && !lakeInlets[lakeIndex].empty()) {
      // Use inlet-based inflow calculation
      float totalInflow = 0.0f;
      
      for (size_t i = 0; i < lakeInlets[lakeIndex].size(); i++) {
        InletConfigSection *inlet = lakeInlets[lakeIndex][i];
        if (inlet && currentTime) {
          float inletQ = inlet->GetObserved(currentTime);
          if (!std::isnan(inletQ)) {
            totalInflow += inletQ;
          } else {
            // If inlet has no Q value at this timestep, assume Q = 0
            totalInflow += 0.0f;
          }
        }
      }
      
      return totalInflow; // Return sum of all inlets (not average)
    }
  }
  
  // Fallback to FAM neighbor-based inflow calculation
  std::vector<GridLoc> neighbors = GetUpstreamNeighbors(lake);
  
  if (neighbors.empty()) {
    // Fallback: use lake cell itself if no upstream neighbors found
    GridLoc *loc = lake->GetLocation();
    int nodeIdx = -1;
    for (size_t ni = 0; ni < nodes->size(); ++ni) {
      if ((*nodes)[ni].x == loc->x && (*nodes)[ni].y == loc->y) {
        nodeIdx = (int)ni;
        break;
      }
    }
    if (nodeIdx >= 0 && nodeIdx < (int)currentQ->size()) {
      return (*currentQ)[nodeIdx];
    }
    return 0.0f;
  }
  
  // Calculate average inflow from upstream neighbors
  float inflow = 0.0f;
  int neighborCount = 0;
  
  for (size_t n = 0; n < neighbors.size(); ++n) {
    int nx = neighbors[n].x;
    int ny = neighbors[n].y;
    
    // Find node index for this neighbor
    int nodeIdx = -1;
    for (size_t ni = 0; ni < nodes->size(); ++ni) {
      if ((*nodes)[ni].x == nx && (*nodes)[ni].y == ny) {
        nodeIdx = (int)ni;
        break;
      }
    }
    if (nodeIdx >= 0 && nodeIdx < (int)currentQ->size()) {
      inflow += (*currentQ)[nodeIdx];
      neighborCount++;
    }
  }
  
  return (neighborCount > 0) ? (inflow / neighborCount) : 0.0f;
}



void LakeMap::InitializeInlets(std::vector<InletConfigSection *> *inlets) {
  if (!inlets) return;
  
  // Clear existing inlet assignments
  for (size_t i = 0; i < lakeInlets.size(); i++) {
    lakeInlets[i].clear();
  }
  
  // Assign inlets to lakes based on lake name from control file
  for (size_t i = 0; i < inlets->size(); i++) {
    InletConfigSection *inlet = inlets->at(i);
    if (!inlet) continue;
    
    // Find the lake this inlet belongs to by matching lake name
    bool foundLake = false;
    for (size_t j = 0; j < lakes.size(); j++) {
      if (strcasecmp(lakes[j]->GetLakeName().c_str(), inlet->GetLakeName()) == 0) {
        lakeInlets[j].push_back(inlet);
        INFO_LOGF("Assigned inlet %s to lake %s", inlet->GetName(), inlet->GetLakeName());
        foundLake = true;
        break;
      }
    }
    
    if (!foundLake) {
      WARNING_LOGF("Inlet %s references unknown lake %s", inlet->GetName(), inlet->GetLakeName());
    }
  }
  
  // Report inlet grouping results
  for (size_t i = 0; i < lakes.size(); i++) {
    INFO_LOGF("Lake %s has %lu inlets configured", lakes[i]->GetLakeName().c_str(), (unsigned long)lakeInlets[i].size());
  }
  
  // Load time series for all inlets
  for (size_t i = 0; i < inlets->size(); i++) {
    InletConfigSection *inlet = inlets->at(i);
    if (inlet) {
      inlet->LoadTS();
    }
  }
} 

void LakeMap::SaveLakeRelationships(TimeVar *currentTime, char *statePath) {
  if (!currentTime || !statePath) {
    return;
  }
  
  // Get the time components from the TimeVar object
  tm *timeInfo = currentTime->GetTM();
  if (!timeInfo) {
    printf("Warning: Could not get time information for lake relationships state file\n");
    return;
  }
  
  // Create filename for lake relationships state file
  char filename[512];
  sprintf(filename, "%s/lake_relationships_%04d%02d%02d_%02d%02d.txt", 
          statePath, (timeInfo->tm_year + 1900), (timeInfo->tm_mon + 1), timeInfo->tm_mday,
          timeInfo->tm_hour, timeInfo->tm_min);
  
  std::ofstream file(filename);
  if (!file.is_open()) {
    printf("Warning: Could not open lake relationships state file: %s\n", filename);
    return;
  }
  
  // Write header with timestamp
  file << "# Lake Relationships State File" << std::endl;
  file << "# Generated: " << (timeInfo->tm_year + 1900) << "-" 
       << ((timeInfo->tm_mon + 1) < 10 ? "0" : "") << (timeInfo->tm_mon + 1) << "-"
       << (timeInfo->tm_mday < 10 ? "0" : "") << timeInfo->tm_mday << " "
       << (timeInfo->tm_hour < 10 ? "0" : "") << timeInfo->tm_hour << ":"
       << (timeInfo->tm_min < 10 ? "0" : "") << timeInfo->tm_min << std::endl;
  file << "# Format: LakeName,NeighborX,NeighborY" << std::endl;
  
  // Write lake relationships
  for (size_t i = 0; i < lakes.size(); i++) {
    LakeModelImpl *lake = lakes[i];
    std::vector<GridLoc> &neighbors = lakeNeighbors[i];
    
    for (size_t j = 0; j < neighbors.size(); j++) {
      file << lake->GetLakeName() << "," << neighbors[j].x << "," << neighbors[j].y << std::endl;
    }
  }
  
  file.close();
  printf("Info: Lake relationships saved to %s\n", filename);
}

bool LakeMap::LoadLakeRelationships(TimeVar *beginTime, char *statePath) {
  if (!beginTime || !statePath) {
    return false;
  }
  
  // Get the time components from the TimeVar object
  tm *timeInfo = beginTime->GetTM();
  if (!timeInfo) {
    printf("Warning: Could not get time information for lake relationships state file\n");
    return false;
  }
  
  // Create filename for lake relationships state file
  char filename[512];
  sprintf(filename, "%s/lake_relationships_%04d%02d%02d_%02d%02d.txt", 
          statePath, (timeInfo->tm_year + 1900), (timeInfo->tm_mon + 1), timeInfo->tm_mday,
          timeInfo->tm_hour, timeInfo->tm_min);
  
  std::ifstream file(filename);
  if (!file.is_open()) {
    printf("Info: Lake relationships state file not found: %s\n", filename);
    return false;
  }
  
  // Clear existing relationships
  for (size_t i = 0; i < lakeNeighbors.size(); i++) {
    lakeNeighbors[i].clear();
  }
  
  std::string line;
  // Skip header lines
  while (std::getline(file, line)) {
    if (line.empty() || line[0] == '#') {
      continue;
    }
    break;
  }
  
  // Read lake relationships
  int lineNum = 0;
  do {
    lineNum++;
    if (line.empty() || line[0] == '#') {
      continue;
    }
    
    std::istringstream iss(line);
    std::string lakeName, xStr, yStr;
    
    if (std::getline(iss, lakeName, ',') && 
        std::getline(iss, xStr, ',') && 
        std::getline(iss, yStr)) {
      
      // Find the lake by name
      LakeModelImpl *targetLake = NULL;
      size_t lakeIndex = 0;
      for (size_t i = 0; i < lakes.size(); i++) {
        std::string currentLakeName = lakes[i]->GetLakeName();
        std::transform(currentLakeName.begin(), currentLakeName.end(), currentLakeName.begin(), (int(*)(int))std::tolower);
        std::string searchLakeName = lakeName;
        std::transform(searchLakeName.begin(), searchLakeName.end(), searchLakeName.begin(), (int(*)(int))std::tolower);
        
        if (currentLakeName == searchLakeName) {
          targetLake = lakes[i];
          lakeIndex = i;
          break;
        }
      }
      
      if (targetLake && lakeIndex < lakeNeighbors.size()) {
        GridLoc neighbor;
        neighbor.x = std::atoi(xStr.c_str());
        neighbor.y = std::atoi(yStr.c_str());
        lakeNeighbors[lakeIndex].push_back(neighbor);
      } else {
        printf("Warning: Could not find lake '%s' or invalid index in line %d\n", lakeName.c_str(), lineNum);
      }
    } else {
      printf("Warning: Invalid format in line %d: %s\n", lineNum, line.c_str());
    }
  } while (std::getline(file, line));
  
  file.close();
  printf("Info: Lake relationships loaded from %s\n", filename);
  return true;
} 