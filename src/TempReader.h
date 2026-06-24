#ifndef TEMP_READER_H
#define TEMP_READER_H

#include "BasicGrids.h"
#include "Defines.h"
#include "TempType.h"
#include <vector>

class GridWriterFull;

class TempReader {
public:
  TempReader() : tempDEM(NULL), elevCorr(false), elevCorrInitialized(false), elevCorrSaved(false), temModCols(0), temModRows(0) {
    lastTempFile[0] = 0;
  }
  bool Read(char *file, SUPPORTED_TEMP_TYPES type, std::vector<GridNode> *nodes,
            std::vector<float> *currentTemp,
            std::vector<float> *prevTemp = NULL, bool hasF = false);
  void ReadDEM(char *file);
  void SetNullDEM() { tempDEM = NULL; }
  void SetElevCorr(bool on) { elevCorr = on; }
  bool SaveElevCorrState(TimeVar *currentTime, char *statePath, GridWriterFull *gridWriter,
                         std::vector<GridNode> *nodes);

private:
  char lastTempFile[CONFIG_MAX_LEN * 2];
  FloatGrid *tempDEM;
  bool elevCorr;
  bool elevCorrInitialized;
  bool elevCorrSaved;
  std::vector<float> temMod;
  long temModCols;
  long temModRows;
  void EnsureElevationCorrection(FloatGrid *tempGrid, std::vector<GridNode> *nodes);
};

#endif
