#ifndef SIMULATOR_H
#define SIMULATOR_H

#include "GaugeConfigSection.h"
#include "GaugeMap.h"
#include "GridNode.h"
#include "Model.h"
#include "ModelBase.h"
#include "PETConfigSection.h"
#include "PETReader.h"
#include "PrecipConfigSection.h"
#include "PrecipReader.h"
#include "RPSkewness.h"
#include "TaskConfigSection.h"
#include "TempConfigSection.h"
#include "TempReader.h"
#include "LakeModel.h"
#include "LakeMap.h"
#include "InletConfigSection.h"

class Simulator {
public:
  bool Initialize(TaskConfigSection *taskN);
  void PreloadForcings(char *file, bool cali);
  bool LoadSavedForcings(char *file, bool cali);
  void SaveForcings(char *file);

  void CleanUp();
  void BasinAvg();
  void Simulate(bool trackPeaks = false);
  float SimulateForCali(float *testParams);
  float *SimulateForCaliTS(float *testParams);
  float *GetObsTS();
  size_t GetNumSteps() { return totalTimeStepsOutsideWarm; }

private:
  bool InitializeBasic(TaskConfigSection *task);
  bool InitializeSimu(TaskConfigSection *task);
  bool InitializeCali(TaskConfigSection *task);
  bool InitializeGridParams(TaskConfigSection *task);

  void SimulateDistributed(bool trackPeaks);
  void SimulateLumped();

  float GetNumSimulatedYears();
  int LoadForcings(PrecipReader *precipReader, PETReader *petReader,
                   TempReader *tempReader);
  void SaveLP3Params();
  void SaveTSOutput();
  bool IsOutputTS();
  void LoadDAFile(TaskConfigSection *task);
  void AssimilateData();
  void OutputCombinedOutput();

  bool HasLakesWithOutputTS(); // Check if any lakes have outputts=true

  bool ReadThresFile(char *file, std::vector<GridNode> *nodes,
                     std::vector<float> *thresVals);
  float ComputeThresValue(float discharge, float action, float minor,
                          float moderate, float major);
  float ComputeThresValueP(float discharge, float action, float actionSD,
                           float minor, float minorSD, float moderate,
                           float moderateSD, float major, float majorSD);
  float CalcProb(float discharge, float mean, float sd);



  // These guys are the basic variables
  TaskConfigSection *task;
  GridNodeVec nodes;
  GridNodeVec lumpedNodes;
  WaterBalanceModel *wbModel;
  RoutingModel *rModel;
  SnowModel *sModel;
  InundationModel *iModel;
  GaugeMap gaugeMap;
  LakeMap lakeMap;
  PrecipConfigSection *precipSec, *qpfSec;
  PETConfigSection *petSec;
  TempConfigSection *tempSec, *tempFSec;
  std::vector<GaugeConfigSection *> *gauges;
  std::map<GaugeConfigSection *, float *> fullParamSettings, *paramSettings;
  std::map<GaugeConfigSection *, float *> fullParamSettingsRoute,
      *paramSettingsRoute;
  std::map<GaugeConfigSection *, float *> fullParamSettingsSnow,
      *paramSettingsSnow;
  std::map<GaugeConfigSection *, float *> fullParamSettingsInundation,
      *paramSettingsInundation;
  std::map<GaugeConfigSection *, float *> fullParamSettingsLake,
      *paramSettingsLake;
  TimeUnit *timeStep, *timeStepSR, *timeStepLR, *timeStepPrecip, *timeStepQPF,
      *timeStepPET, *timeStepTemp, *timeStepTempF;
  float precipConvert, qpfConvert, petConvert, timeStepHours, timeStepHoursLR;
  TimeVar currentTime, currentTimePrecip, currentTimeQPF, currentTimePET,
      currentTimeTemp, currentTimeTempF, beginTime, endTime, warmEndTime,
      beginLRTime;
    std::vector<TimeVar*> currentTimes, beginTimes, endTimes;
  DatedName *precipFile, *qpfFile, *petFile, *tempFile, *tempFFile,
      currentTimeText, currentTimeTextOutput;
  std::vector<float> currentFF, currentSF, currentBF, currentQ, avgPrecip, avgPET, avgSWE,
      currentSWE, avgT, avgSM,avgGW, avgFF, avgSF, avgBF, currentDepth;
  std::vector<float> currentLakeVolume; // Lake volume for output
  std::vector<LakeModelImpl*> lakeModels; // All lake models for inflow calculation
  std::vector<InletConfigSection*> inlets; // All inlet configurations
  std::vector<FloatGrid *> paramGrids, paramGridsRoute, paramGridsSnow,
      paramGridsInundation, paramGridsLake;
  bool hasQPF, hasTempF, wantsDA;
  bool inLR;
  std::vector<bool> gaugesUsed;

  // This is for simulations only
  std::vector<float> currentPrecipSimu, currentPETSimu, currentTempSimu;
  std::vector<FILE *> gaugeOutputs;
  int griddedOutputs;
  bool outputRP;
  bool useStates, saveStates;
  bool preloadedForcings;
  std::vector<RPData> rpData;
  char *outputPath;
  char *statePath;
  TimeVar stateTime;
  std::vector<std::vector<float> > peakVals;
  GridWriterFull gridWriter;
  float numYears;
  int missingQPE, missingQPF;

  // This is for calibrations only
  std::vector<std::vector<float> > currentPrecipCali, currentPETCali,
      currentTempCali;
  std::vector<float> obsQ, simQ;
  CaliParamConfigSection *caliParamSec;
  RoutingCaliParamConfigSection *routingCaliParamSec;
  SnowCaliParamConfigSection *snowCaliParamSec;
  LakeCaliParamConfigSection *lakeCaliParamSec;
  OBJECTIVES objectiveFunc;
  GaugeConfigSection *caliGauge;
  float *caliWBParams;
  float *caliRParams;
  float *caliSParams;
  float *caliLParams;
  size_t totalTimeSteps, totalTimeStepsOutsideWarm;
  int numWBParams, numRParams, numSParams, numLParams;
  int caliGaugeIndex;
  std::vector<WaterBalanceModel *> caliWBModels;
  std::vector<RoutingModel *> caliRModels;
  std::vector<SnowModel *> caliSModels;
  std::vector<LakeModelImpl *> caliLModels;
  std::vector<float *> caliWBCurrentParams, caliRCurrentParams,
      caliSCurrentParams, caliLCurrentParams;
  std::vector<std::map<GaugeConfigSection *, float *> > caliWBFullParamSettings,
      caliRFullParamSettings, caliSFullParamSettings, caliLFullParamSettings;
};

#endif
