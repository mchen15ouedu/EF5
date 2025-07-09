#include "GaugeMap.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

void GaugeMap::Initialize(std::vector<GaugeConfigSection *> *newGauges) {
  // Copy the list of gauges over to internal storage
  gauges = (*newGauges);

  size_t countGauges = gauges.size();

  // Resize the outer vector in the tree that contains all of the interior
  // gauges
  gaugeTree.resize(countGauges);

  // Initialize the map that contains the index into a vector for each
  // GaugeConfigSection *
  for (size_t i = 0; i < countGauges; i++) {
    gaugeMap[gauges[i]] = i;
  }

  // Initialize storage for the partial values contributing to each gauge
  partialVal.resize(countGauges);
  partialArea.resize(countGauges);
}

void GaugeMap::AddUpstreamGauge(GaugeConfigSection *downStream,
                                GaugeConfigSection *upStream) {
  size_t countGauges = gauges.size();
  for (size_t i = 0; i < countGauges; i++) {
    if (gauges[i] == downStream) {
      printf("%s is upstream(direct) of %s\n", upStream->GetName(),
             downStream->GetName());
      gaugeTree[i].push_back(upStream);
    }
  }

  for (size_t i = 0; i < countGauges; i++) {
    std::vector<GaugeConfigSection *> *intGauges = &(gaugeTree[i]);
    for (size_t j = 0; j < intGauges->size(); j++) {
      if (intGauges->at(j) == downStream) {
        printf("%s is upstream(indirect) of %s\n", upStream->GetName(),
               gauges[j]->GetName());
        intGauges->push_back(upStream);
      }
    }
  }
}

void GaugeMap::GaugeAverage(std::vector<GridNode> *nodes,
                            std::vector<float> *currentValue,
                            std::vector<float> *gaugeAvg) {
  size_t countGauges = gauges.size();
  size_t countNodes = nodes->size();

  // Zero out the partial vectors
  for (size_t i = 0; i < countGauges; i++) {
    partialVal[i] = 0;
    partialArea[i] = 0;
  }

  // Add up contributions to each gauge
  for (size_t i = 0; i < countNodes; i++) {
    GridNode *node = &((*nodes)[i]);
    size_t gaugeIndex = gaugeMap[node->gauge];
    partialVal[gaugeIndex] += ((*currentValue)[i] * node->area);
    partialArea[gaugeIndex] += node->area;
  }

  for (size_t i = 0; i < countGauges; i++) {
    float totalVal = 0;
    float totalArea = 0;

    totalVal += partialVal[i];
    totalArea += partialArea[i];

    std::vector<GaugeConfigSection *> *intGauges = &(gaugeTree[i]);
    for (size_t j = 0; j < intGauges->size(); j++) {
      size_t gaugeIndex = gaugeMap[intGauges->at(j)];
      totalVal += partialVal[gaugeIndex];
      totalArea += partialArea[gaugeIndex];
    }

    gaugeAvg->at(i) = (totalVal / totalArea);
  }
}

void GaugeMap::GetGaugeArea(std::vector<GridNode> *nodes,
                            std::vector<float> *gaugeArea) {

  size_t countGauges = gauges.size();
  size_t countNodes = nodes->size();

  // Zero out the partial vectors
  for (size_t i = 0; i < countGauges; i++) {
    partialArea[i] = 0;
  }

  // Add up contributions to each gauge
  for (size_t i = 0; i < countNodes; i++) {
    GridNode *node = &((*nodes)[i]);
    size_t gaugeIndex = gaugeMap[node->gauge];
    partialArea[gaugeIndex] += node->area;
  }

  for (size_t i = 0; i < countGauges; i++) {
    float totalArea = 0;

    totalArea += partialArea[i];

    std::vector<GaugeConfigSection *> *intGauges = &(gaugeTree[i]);
    for (size_t j = 0; j < intGauges->size(); j++) {
      size_t gaugeIndex = gaugeMap[intGauges->at(j)];
      totalArea += partialArea[gaugeIndex];
    }

    gaugeArea->at(i) = totalArea;
  }
}

void GaugeMap::SaveGaugeRelationships(TimeVar *currentTime, char *statePath) {
  if (!currentTime || !statePath) {
    return;
  }
  
  // Get the time components from the TimeVar object
  tm *timeInfo = currentTime->GetTM();
  if (!timeInfo) {
    printf("Warning: Could not get time information for gauge relationships state file\n");
    return;
  }
  
  // Create filename for gauge relationships state file
  char filename[512];
  sprintf(filename, "%s/gauge_relationships_%04d%02d%02d_%02d%02d.txt", 
          statePath, timeInfo->tm_year + 1900, timeInfo->tm_mon + 1, timeInfo->tm_mday,
          timeInfo->tm_hour, timeInfo->tm_min);
  
  std::ofstream file(filename);
  if (!file.is_open()) {
    printf("Warning: Could not open gauge relationships state file: %s\n", filename);
    return;
  }
  
  // Write header with timestamp
  file << "# Gauge Relationships State File" << std::endl;
  file << "# Generated: " << (timeInfo->tm_year + 1900) << "-" 
       << ((timeInfo->tm_mon + 1) < 10 ? "0" : "") << (timeInfo->tm_mon + 1) << "-"
       << (timeInfo->tm_mday < 10 ? "0" : "") << timeInfo->tm_mday << " "
       << (timeInfo->tm_hour < 10 ? "0" : "") << timeInfo->tm_hour << ":"
       << (timeInfo->tm_min < 10 ? "0" : "") << timeInfo->tm_min << std::endl;
  file << "# Format: downstream_gauge_name,upstream_gauge_name" << std::endl;
  file << std::endl;
  
  // Write gauge relationships
  size_t countGauges = gauges.size();
  for (size_t i = 0; i < countGauges; i++) {
    std::vector<GaugeConfigSection *> *intGauges = &(gaugeTree[i]);
    for (size_t j = 0; j < intGauges->size(); j++) {
      file << gauges[i]->GetName() << "," << intGauges->at(j)->GetName() << std::endl;
    }
  }
  
  file.close();
  printf("Saved gauge relationships to: %s\n", filename);
}

bool GaugeMap::LoadGaugeRelationships(TimeVar *beginTime, char *statePath) {
  if (!beginTime || !statePath) {
    return false;
  }
  
  // Get the time components from the TimeVar object
  tm *timeInfo = beginTime->GetTM();
  if (!timeInfo) {
    printf("Warning: Could not get time information for gauge relationships state file\n");
    return false;
  }
  
  // Create filename for gauge relationships state file
  char filename[512];
  sprintf(filename, "%s/gauge_relationships_%04d%02d%02d_%02d%02d.txt", 
          statePath, timeInfo->tm_year + 1900, timeInfo->tm_mon + 1, timeInfo->tm_mday,
          timeInfo->tm_hour, timeInfo->tm_min);
  
  std::ifstream file(filename);
  if (!file.is_open()) {
    printf("Info: No gauge relationships state file found: %s\n", filename);
    printf("Will rebuild gauge relationships from scratch.\n");
    return false;
  }
  
  printf("Loading gauge relationships from: %s\n", filename);
  
  // Clear existing relationships
  size_t countGauges = gauges.size();
  for (size_t i = 0; i < countGauges; i++) {
    gaugeTree[i].clear();
  }
  
  std::string line;
  int lineNum = 0;
  
  // Skip header lines (start with #)
  while (std::getline(file, line)) {
    lineNum++;
    if (line.empty() || line[0] == '#') {
      continue;
    }
    
    // Parse relationship line: downstream,upstream
    std::istringstream iss(line);
    std::string downstreamName, upstreamName;
    
    if (std::getline(iss, downstreamName, ',') && std::getline(iss, upstreamName)) {
      // Find the gauge objects by name
      GaugeConfigSection *downstreamGauge = NULL;
      GaugeConfigSection *upstreamGauge = NULL;
      
      for (size_t i = 0; i < countGauges; i++) {
        std::string gaugeName = gauges[i]->GetName();
        std::transform(gaugeName.begin(), gaugeName.end(), gaugeName.begin(), (int(*)(int))std::tolower);
        std::string downstreamNameLower = downstreamName;
        std::transform(downstreamNameLower.begin(), downstreamNameLower.end(), downstreamNameLower.begin(), (int(*)(int))std::tolower);
        std::string upstreamNameLower = upstreamName;
        std::transform(upstreamNameLower.begin(), upstreamNameLower.end(), upstreamNameLower.begin(), (int(*)(int))std::tolower);
        
        if (gaugeName == downstreamNameLower) {
          downstreamGauge = gauges[i];
        }
        if (gaugeName == upstreamNameLower) {
          upstreamGauge = gauges[i];
        }
      }
      
      if (downstreamGauge && upstreamGauge) {
        // Add the relationship using the existing method
        AddUpstreamGauge(downstreamGauge, upstreamGauge);
      } else {
        printf("Warning: Could not find gauges in line %d: %s,%s\n", 
               lineNum, downstreamName.c_str(), upstreamName.c_str());
      }
    } else {
      printf("Warning: Invalid format in line %d: %s\n", lineNum, line.c_str());
    }
  }
  
  file.close();
  printf("Successfully loaded gauge relationships.\n");
  return true;
}
