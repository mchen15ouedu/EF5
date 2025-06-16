#include "PrecipReader.h"
#include "AscGrid.h"
#include "BifGrid.h"
#include "MRMSGrid.h"
#include "Messages.h"
#include "TRMMRTGrid.h"
#include "TRMMV6Grid.h"
#include "TifGrid.h"
#include <cstdio>
#include <cstring>

bool PrecipReader::Read(char *file, SUPPORTED_PRECIP_TYPES type,
                        std::vector<GridNode> *nodes,
                        std::vector<float> *currentPrecip, float precipConvert,
                        std::vector<float> *prevPrecip, bool hasQPF) {
  if (!strcmp(lastPrecipFile, file)) {
    if (prevPrecip) {
      for (size_t i = 0; i < nodes->size(); i++) {
        currentPrecip->at(i) = prevPrecip->at(i);
      }
    }
    return true; // This is the same precip file that we read last time, we
                 // assume currentPrecip is still valid!
  }

  if (!hasQPF) {
    // Update this here so we recheck for missing files & don't recheck for
    // forecast precip
    strcpy(lastPrecipFile, file);
  }

  // static FloatGrid *precipGrid = NULL;
  FloatGrid *precipGrid = NULL;

  switch (type) {
  case PRECIP_ASCII:
    precipGrid = ReadFloatAscGrid(file);
    break;
  case PRECIP_BIF:
    precipGrid = ReadFloatBifGrid(file);
    break;
  case PRECIP_TIF:
    precipGrid = ReadFloatTifGrid(file);
    break;
  case PRECIP_MRMS:
    precipGrid = ReadFloatMRMSGrid(file);
    break;
  case PRECIP_TRMMRT:
    precipGrid = ReadFloatTRMMRTGrid(file, precipGrid);
    break;
    // case PRECIP_TRMMV7:
    //	precipGrid = ReadFloatTRMMV6Grid(file);
    break;
  default:
    ERROR_LOG("Unsupported precip format!");
  }

  if (!precipGrid) {
    // The precip file was not found! We return zeros if there is no qpf.
    if (!hasQPF) {
      for (size_t i = 0; i < nodes->size(); i++) {
        currentPrecip->at(i) = 0;
      }
    }
    return false;
  }

  if (precipGrid->cellSize < g_DEM->cellSize) {
    printf("ERROR: Precipitation grid resolution (cellSize=%.2f) is higher than DEM resolution (cellSize=%.2f). Please resample your precipitation data to match or be coarser than the DEM.\n", precipGrid->cellSize, g_DEM->cellSize);
    delete precipGrid;
    exit(1);
  }

  // Debug prints to show grid extents and cell sizes
  printf("DEM: left=%f right=%f top=%f bottom=%f cellSize=%f\n",
         g_DEM->extent.left, g_DEM->extent.right, g_DEM->extent.top, 
         g_DEM->extent.bottom, g_DEM->cellSize);
  printf("Precip: left=%f right=%f top=%f bottom=%f cellSize=%f\n",
         precipGrid->extent.left, precipGrid->extent.right, precipGrid->extent.top,
         precipGrid->extent.bottom, precipGrid->cellSize);

  if (hasQPF) {
    // Update this here so we recheck for missing files & don't recheck for
    // forecast precip
    strcpy(lastPrecipFile, file);
  }

  // We have two options now... Either the precip grid & the basic grids are the
  // same Or they are different!

  if (g_DEM->IsSpatialMatch(precipGrid)) {
    // The grids are the same! Our life is easy!
    #pragma omp parallel for
    for (size_t i = 0; i < nodes->size(); i++) {
      GridNode *node = &(nodes->at(i));
      if (precipGrid->data[node->y][node->x] != precipGrid->noData &&
          precipGrid->data[node->y][node->x] > 0.0) {
        currentPrecip->at(i) = precipGrid->data[node->y][node->x] * precipConvert;
      } else {
        currentPrecip->at(i) = 0;
      }
    }
  } else {
    // The grids are different, we must do some resampling fun.
    #pragma omp parallel for
    for (size_t i = 0; i < nodes->size(); i++) {
      GridNode *node = &(nodes->at(i));
      RefLoc demPoint;
      // Get the geographic coordinates of the DEM point
      g_DEM->GetRefLoc(node->x, node->y, &demPoint);
      
      // Find the corresponding point in precipitation grid
      GridLoc precipPt;
      if (precipGrid->GetGridLoc(demPoint.x, demPoint.y, &precipPt)) {
        // Check bounds to be extra safe
        if (precipPt.x >= 0 && precipPt.x < precipGrid->numCols && 
            precipPt.y >= 0 && precipPt.y < precipGrid->numRows &&
            precipGrid->data[precipPt.y][precipPt.x] != precipGrid->noData &&
            precipGrid->data[precipPt.y][precipPt.x] > 0.0) {
          currentPrecip->at(i) = precipGrid->data[precipPt.y][precipPt.x] * precipConvert;
        } else {
          currentPrecip->at(i) = 0;
        }
      } else {
        currentPrecip->at(i) = 0;
      }
    }
  }

  delete precipGrid;
  return true;
}
