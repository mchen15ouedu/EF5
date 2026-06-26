#include "TempReader.h"
#include "AscGrid.h"
#include "BifGrid.h"
#include "Messages.h"
#include "PqfGrid.h"
#include "TifGrid.h"
#include "GridWriterFull.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include "DatedName.h"
#include <map>

void TempReader::ReadDEM(char *file) {
  tempDEM = ReadFloatTifGrid(file);
  if (!tempDEM) {
    WARNING_LOGF("Failed to load temperature grid DEM %s\n", file);
  } else {
    // Successfully loaded temperature grid DEM without logging
  }
}

void TempReader::EnsureElevationCorrection(FloatGrid *tempGrid, std::vector<GridNode> *nodes) {
  if (elevCorrInitialized && temMod.size() == nodes->size() &&
      temModCols == tempGrid->numCols && temModRows == tempGrid->numRows) {
    return;
  }
  temMod.resize(nodes->size());
  temModCols = tempGrid->numCols;
  temModRows = tempGrid->numRows;

  // Compute minimum DEM elevation only for tempGrid pixels overlapping the basin
  std::map<long long, float> minElevByPixel;

  GridLoc pt;
  for (size_t i = 0; i < nodes->size(); i++) {
    GridNode *node = &(nodes->at(i));
    if (tempGrid->GetGridLoc(node->refLoc.x, node->refLoc.y, &pt)) {
      long long key = (((long long)pt.y) << 32) | (unsigned long)pt.x;
      float elev = g_DEM->data[node->y][node->x];
      std::map<long long, float>::iterator it = minElevByPixel.find(key);
      if (it == minElevByPixel.end()) {
        minElevByPixel.insert(std::make_pair(key, elev));
      } else if (elev < it->second) {
        it->second = elev;
      }
    }
  }

  // Build per-node temperature modifier: -0.0065 * (elev - minElev(pixel))
  for (size_t i = 0; i < nodes->size(); i++) {
    GridNode *node = &(nodes->at(i));
    if (tempGrid->GetGridLoc(node->refLoc.x, node->refLoc.y, &pt)) {
      long long key = (((long long)pt.y) << 32) | (unsigned long)pt.x;
      float elev = g_DEM->data[node->y][node->x];
      std::map<long long, float>::iterator it = minElevByPixel.find(key);
      if (it != minElevByPixel.end()) {
        float baseElev = it->second;
        float diffHeight = elev - baseElev; // meters
        temMod[i] = -0.0065f * diffHeight;
      } else {
        temMod[i] = 0.0f;
      }
    } else {
      temMod[i] = 0.0f;
    }
  }

  elevCorrInitialized = true;
}

bool TempReader::Read(char *file, SUPPORTED_TEMP_TYPES type,
                      std::vector<GridNode> *nodes,
                      std::vector<float> *currentTemp,
                      std::vector<float> *prevTemp, bool hasF) {
  if (!strcmp(lastTempFile, file)) {
    if (prevTemp) {
      for (size_t i = 0; i < nodes->size(); i++) {
        currentTemp->at(i) = prevTemp->at(i);
      }
    }
    return true; // This is the same temp file that we read last time, we assume
                 // currentPET is still valid!
  }

  if (!hasF) {
    // Update this here so we recheck for missing files & don't recheck for
    // forecast precip
    strcpy(lastTempFile, file);
  }

  FloatGrid *tempGrid = NULL;

  switch (type) {
  case TEMP_ASCII:
    tempGrid = ReadFloatAscGrid(file);
    break;
  case TEMP_TIF:
    tempGrid = ReadFloatTifGrid(file);
    break;
  case TEMP_BIF:
    tempGrid = ReadFloatBifGrid(file);
    break;
  case TEMP_PQF:
    tempGrid = ReadFloatPqfGrid(file);
    break;
  default:
    ERROR_LOG("Unsupported Temp format!");
    break;
  }

  if (!tempGrid) {
    // The temp file was not found! We return zeros if there is no qpf.
    if (!hasF) {
      for (size_t i = 0; i < nodes->size(); i++) {
        currentTemp->at(i) = 0;
      }
    }
    return false;
  }

  if (hasF) {
    // Update this here so we recheck for missing files & don't recheck for
    // forecast precip
    strcpy(lastTempFile, file);
  }

  // elevCorr mode: separate from tempDEM logic
  if (elevCorr) {
    EnsureElevationCorrection(tempGrid, nodes);
    if (g_DEM->IsSpatialMatch(tempGrid)) {
      for (size_t i = 0; i < nodes->size(); i++) {
        GridNode *node = &(nodes->at(i));
        if (tempGrid->data[node->y][node->x] != tempGrid->noData) {
          currentTemp->at(i) = tempGrid->data[node->y][node->x] + temMod[i];
        } else {
          currentTemp->at(i) = tempGrid->noData;
        }
      }
    } else {
      GridLoc pt;
      for (size_t i = 0; i < nodes->size(); i++) {
        GridNode *node = &(nodes->at(i));
        if (tempGrid->GetGridLoc(node->refLoc.x, node->refLoc.y, &pt) &&
            tempGrid->data[pt.y][pt.x] != tempGrid->noData) {
          currentTemp->at(i) = tempGrid->data[pt.y][pt.x] + temMod[i];
        } else {
          currentTemp->at(i) = tempGrid->noData;
        }
      }
    }
  } else {
    // Original behavior (including optional tempDEM lapse)
    if (g_DEM->IsSpatialMatch(tempGrid)) {
      for (size_t i = 0; i < nodes->size(); i++) {
        GridNode *node = &(nodes->at(i));
        if (tempGrid->data[node->y][node->x] != tempGrid->noData) {
          currentTemp->at(i) = tempGrid->data[node->y][node->x];
        } else {
          currentTemp->at(i) = 0.0f;
        }
      }
    } else {
      GridLoc pt;
      for (size_t i = 0; i < nodes->size(); i++) {
        GridNode *node = &(nodes->at(i));
        if (tempGrid->GetGridLoc(node->refLoc.x, node->refLoc.y, &pt) &&
            tempGrid->data[pt.y][pt.x] != tempGrid->noData) {
          if (tempDEM && tempDEM->IsSpatialMatch(tempGrid)) {
            float temp = tempGrid->data[pt.y][pt.x];
            float diffHeight =
                g_DEM->data[node->y][node->x] - tempDEM->data[pt.y][pt.x];
            float tempMod = -0.0065f * diffHeight;
            currentTemp->at(i) = temp + tempMod;
          } else {
            currentTemp->at(i) = tempGrid->data[pt.y][pt.x];
          }
        } else {
          currentTemp->at(i) = 0.0f;
        }
      }
    }
  }

  // We don't actually need to keep the PET grid in memory anymore
  delete tempGrid;

  return true;
}

bool TempReader::SaveElevCorrState(TimeVar *currentTime, char *statePath, GridWriterFull *gridWriter,
                                   std::vector<GridNode> *nodes) {
  if (!elevCorr || !elevCorrInitialized || temMod.size() != nodes->size()) {
    return false;
  }
  DatedName timeStr;
  timeStr.SetNameStr("YYYYMMDD_HHUU");
  timeStr.ProcessNameLoose(NULL);
  timeStr.UpdateName(currentTime->GetTM());
  char buffer[300];
  sprintf(buffer, "%s/%s_%s.tif", statePath, "temmod", timeStr.GetName());
  gridWriter->WriteGrid(nodes, &temMod, buffer, false);
  elevCorrSaved = true;
  return true;
}
