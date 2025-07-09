#include "ExecutionController.h"
#include "ARS.h"
#include "BasicConfigSection.h"
#include "BasicGrids.h"
#include "BasinConfigSection.h"
#include "DREAM.h"
#include "EnsTaskConfigSection.h"
#include "ExecuteConfigSection.h"
#include "GaugeConfigSection.h"
#include "GeographicProjection.h"
#include "InundationParamSetConfigSection.h"
#include "LAEAProjection.h"
#include "Messages.h"
#include "Model.h"
#include "PETConfigSection.h"
#include "ParamSetConfigSection.h"
#include "PrecipConfigSection.h"
#include "RoutingParamSetConfigSection.h"
#include "Simulator.h"
#include "SnowParamSetConfigSection.h"
#include "TaskConfigSection.h"
#include "TempConfigSection.h"
#include "TimeVar.h"
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>
#include <string>

// Global variable declarations
extern BasicConfigSection *g_basicConfig;
extern ExecuteConfigSection *g_executeConfig;
extern std::map<std::string, PrecipConfigSection *> g_precipConfigs;
extern std::map<std::string, PETConfigSection *> g_petConfigs;
extern std::map<std::string, TempConfigSection *> g_tempConfigs;
extern std::map<std::string, GaugeConfigSection *> g_gaugeConfigs;
extern std::map<std::string, BasinConfigSection *> g_basinConfigs;
extern std::map<std::string, ParamSetConfigSection *> g_paramSetConfigs[];
extern std::map<std::string, RoutingParamSetConfigSection *> g_routingParamSetConfigs[];
extern std::map<std::string, SnowParamSetConfigSection *> g_snowParamSetConfigs[];
extern std::map<std::string, InundationParamSetConfigSection *> g_inundationParamSetConfigs[];

static void LoadProjection();
static void ExecuteSimulation(TaskConfigSection *task);
static void ExecuteSimulationRP(TaskConfigSection *task);
static void ExecuteCalibrationARS(TaskConfigSection *task);
static void ExecuteCalibrationDREAM(TaskConfigSection *task);
static void ExecuteCalibrationDREAMEns(EnsTaskConfigSection *task);
static void ExecuteClipBasin(TaskConfigSection *task);
static void ExecuteClipGauge(TaskConfigSection *task);
static void ExecuteMakeBasic(TaskConfigSection *task);
static void ExecuteMakeBasinAvg(TaskConfigSection *task);
static bool CheckDirectoryExists(const char* path, const char* description);
static bool CheckOptionalDirectoryExists(const char* path, const char* description);
static bool ValidateAllDirectories();

void ExecuteTasks() {

  if (!g_executeConfig) {
    ERROR_LOGF("%s", "No execute section specified!");
    return;
  }

  // Validate all directories specified in control file
  if (!ValidateAllDirectories()) {
    ERROR_LOG("Directory validation failed. Exiting.");
    return;
  }



  std::vector<TaskConfigSection *> *tasks = g_executeConfig->GetTasks();
  std::vector<TaskConfigSection *>::iterator taskItr;

  std::vector<EnsTaskConfigSection *> *ensTasks =
      g_executeConfig->GetEnsTasks();
  std::vector<EnsTaskConfigSection *>::iterator ensTaskItr;

  if (!LoadBasicGrids()) {
    return;
  }
  LoadProjection();

  // Loop through all the ensemble tasks and execute them first
  for (ensTaskItr = ensTasks->begin(); ensTaskItr != ensTasks->end();
       ensTaskItr++) {
    EnsTaskConfigSection *ensTask = (*ensTaskItr);
    INFO_LOGF("Executing ensemble task %s", ensTask->GetName());

    switch (ensTask->GetRunStyle()) {
    case STYLE_CALI_DREAM:
      ExecuteCalibrationDREAMEns(ensTask);
      break;
    default:
      ERROR_LOGF("Unsupport ensemble task run style \"%u\"",
                 ensTask->GetRunStyle());
      break;
    }
  }

  // Loop through all of the tasks and execute them
  for (taskItr = tasks->begin(); taskItr != tasks->end(); taskItr++) {
    TaskConfigSection *task = (*taskItr);
    INFO_LOGF("Executing task %s", task->GetName());

    switch (task->GetRunStyle()) {
    case STYLE_SIMU:
      ExecuteSimulation(task);
      break;
    case STYLE_SIMU_RP:
      ExecuteSimulationRP(task);
      break;
    case STYLE_CALI_ARS:
      ExecuteCalibrationARS(task);
      break;
    case STYLE_CALI_DREAM:
      ExecuteCalibrationDREAM(task);
      break;
    case STYLE_CLIP_BASIN:
      ExecuteClipBasin(task);
      break;
    case STYLE_CLIP_GAUGE:
      ExecuteClipGauge(task);
      break;
    case STYLE_MAKE_BASIC:
      ExecuteMakeBasic(task);
      break;
    case STYLE_BASIN_AVG:
      ExecuteMakeBasinAvg(task);
    default:
      ERROR_LOGF("Unimplemented simulation run style \"%u\"",
                 task->GetRunStyle());
      break;
    }
  }

  FreeBasicGridsData();
}

void LoadProjection() {

  switch (g_basicConfig->GetProjection()) {
  case PROJECTION_GEOGRAPHIC:
    g_Projection = new GeographicProjection();
    g_Projection->SetCellSize(g_DEM->cellSize);
    break;
  case PROJECTION_LAEA:
    g_Projection = new LAEAProjection();
    g_Projection->SetCellSize(g_DEM->cellSize);
    break;
  }

  // Reproject the gauges into the proper map coordinates
  for (std::map<std::string, GaugeConfigSection *>::iterator itr =
           g_gaugeConfigs.begin();
       itr != g_gaugeConfigs.end(); itr++) {

    GaugeConfigSection *gauge = itr->second;
    float newX, newY;
    g_Projection->ReprojectPoint(gauge->GetLon(), gauge->GetLat(), &newX,
                                 &newY);
    gauge->SetLon(newX);
    gauge->SetLat(newY);
  }
}

void ExecuteSimulation(TaskConfigSection *task) {

  Simulator sim;

  if (sim.Initialize(task)) {
    sim.Simulate();
    sim.CleanUp();
  }
}

void ExecuteSimulationRP(TaskConfigSection *task) {

  Simulator sim;

  sim.Initialize(task);
  sim.Simulate(true);
  sim.CleanUp();
}

void ExecuteCalibrationARS(TaskConfigSection *task) {

  Simulator sim;
  char buffer[CONFIG_MAX_LEN * 2];

  sim.Initialize(task);
  sprintf(buffer, "%s/%s", task->GetOutput(), "califorcings.bin");
  sim.PreloadForcings(buffer, true);

  printf("Precip loaded!\n");

  ARS ars;
  int numSnow = 0;
  if (task->GetSnow() != SNOW_QTY) {
    numSnow = numSnowParams[task->GetSnow()];
  }
  int numLake = 0;
  if (task->IsLakeModuleEnabled() && task->GetLakeCaliParamSec()) {
    numLake = numLakeParams[0]; // Assuming lake parameters are the same for all lake types
  }
  ars.Initialize(task->GetCaliParamSec(), task->GetRoutingCaliParamSec(), 
                 task->GetSnowCaliParamSec(), task->GetLakeCaliParamSec(),
                 numModelParams[task->GetModel()],
                 numRouteParams[task->GetRouting()], numSnow, numLake, &sim);
  ars.CalibrateParams();

  sprintf(buffer, "%s/cali_ars.%s.%s.csv", task->GetOutput(),
          task->GetCaliParamSec()->GetGauge()->GetName(),
          modelStrings[task->GetModel()]);
  ars.WriteOutput(buffer, task->GetModel(), task->GetRouting());
}

void ExecuteCalibrationDREAM(TaskConfigSection *task) {

  Simulator sim;
  char buffer[CONFIG_MAX_LEN * 2];

  sim.Initialize(task);
  sprintf(buffer, "%s/%s", task->GetOutput(), "califorcings.bin");
  sim.PreloadForcings(buffer, true);

  INFO_LOGF("%s", "Precip loaded!");

  DREAM dream;
  int numSnow = 0;
  if (task->GetSnow() != SNOW_QTY) {
    numSnow = numSnowParams[task->GetSnow()];
  }
  int numLake = 0;
  if (task->IsLakeModuleEnabled() && task->GetLakeCaliParamSec()) {
    numLake = numLakeParams[0]; // Assuming lake parameters are the same for all lake types
  }
  dream.Initialize(task->GetCaliParamSec(), task->GetRoutingCaliParamSec(),
                   task->GetSnowCaliParamSec(), task->GetLakeCaliParamSec(),
                   numModelParams[task->GetModel()],
                   numRouteParams[task->GetRouting()], numSnow, numLake, &sim);
  dream.CalibrateParams();

  sprintf(buffer, "%s/cali_dream.%s.%s.csv", task->GetOutput(),
          task->GetCaliParamSec()->GetGauge()->GetName(),
          modelStrings[task->GetModel()]);
  dream.WriteOutput(buffer, task->GetModel(), task->GetRouting(),
                    task->GetSnow());
}

void ExecuteCalibrationDREAMEns(EnsTaskConfigSection *task) {

  char buffer[CONFIG_MAX_LEN * 2];
  std::vector<TaskConfigSection *> *tasks = task->GetTasks();
  int numMembers = (int)tasks->size();
  int numParams = 0;

  std::vector<Simulator> sims;
  std::vector<int> paramsPerSim;
  paramsPerSim.resize(numMembers);
  sims.resize(numMembers);

  for (int i = 0; i < numMembers; i++) {
    Simulator *sim = &(sims[i]);
    TaskConfigSection *thisTask = tasks->at(i);
    sim->Initialize(thisTask);
    sprintf(buffer, "%s/%s", thisTask->GetOutput(), "califorcings.bin");
    sim->PreloadForcings(buffer, true);
    numParams += numModelParams[thisTask->GetModel()];
    paramsPerSim[i] = numModelParams[thisTask->GetModel()];
  }

  float *minParams = new float[numParams];
  float *maxParams = new float[numParams];
  int paramIndex = 0;

  for (int i = 0; i < numMembers; i++) {
    TaskConfigSection *thisTask = tasks->at(i);
    int cParams = numModelParams[thisTask->GetModel()];
    memcpy(&(minParams[paramIndex]),
           thisTask->GetCaliParamSec()->GetParamMins(),
           sizeof(float) * cParams);
    memcpy(&(maxParams[paramIndex]),
           thisTask->GetCaliParamSec()->GetParamMaxs(),
           sizeof(float) * cParams);
    paramIndex += cParams;
  }

  INFO_LOGF("%s", "Precip loaded!\n");

  DREAM dream;
  dream.Initialize(tasks->at(0)->GetCaliParamSec(), numParams, minParams,
                   maxParams, &sims, &paramsPerSim);
  dream.CalibrateParams();

  sprintf(buffer, "%s/cali_dream.%s.%s.csv", tasks->at(0)->GetOutput(),
          tasks->at(0)->GetCaliParamSec()->GetGauge()->GetName(), "ensemble");
  dream.WriteOutput(buffer, tasks->at(0)->GetModel(),
                    tasks->at(0)->GetRouting(), tasks->at(0)->GetSnow());
}

void ExecuteClipBasin(TaskConfigSection *task) {
  std::map<GaugeConfigSection *, float *> fullParamSettings, *paramSettings,
      fullRouteParamSettings, *routeParamSettings;
  std::vector<GridNode> nodes;
  GaugeMap gaugeMap;

  // Get the parameter settings for this task
  paramSettings = task->GetParamsSec()->GetParamSettings();
  float *defaultParams = NULL, *defaultRouteParams = NULL;
  GaugeConfigSection *gs = task->GetDefaultGauge();
  std::map<GaugeConfigSection *, float *>::iterator pitr =
      paramSettings->find(gs);
  if (pitr != paramSettings->end()) {
    defaultParams = pitr->second;
  }
  routeParamSettings = task->GetRoutingParamsSec()->GetParamSettings();
  pitr = routeParamSettings->find(gs);
  if (pitr != routeParamSettings->end()) {
    defaultRouteParams = pitr->second;
  }
  
  // Lake parameters are now handled through CSV file, not parameter sets

  CarveBasin(task->GetBasinSec(), &nodes, paramSettings, &fullParamSettings,
             &gaugeMap, defaultParams, routeParamSettings,
             &fullRouteParamSettings, defaultRouteParams, NULL, NULL, NULL,
             NULL, NULL, NULL);

  ClipBasicGrids(task->GetBasinSec(), &nodes, task->GetBasinSec()->GetName(),
                 task->GetOutput());
}

void ExecuteClipGauge(TaskConfigSection *task) {
  std::map<GaugeConfigSection *, float *> fullParamSettings, *paramSettings,
      fullRouteParamSettings, *routeParamSettings;
  std::vector<GridNode> nodes;
  GaugeMap gaugeMap;

  // Get the parameter settings for this task
  paramSettings = task->GetParamsSec()->GetParamSettings();
  float *defaultParams = NULL, *defaultRouteParams = NULL;
  GaugeConfigSection *gs = task->GetDefaultGauge();
  std::map<GaugeConfigSection *, float *>::iterator pitr =
      paramSettings->find(gs);
  if (pitr != paramSettings->end()) {
    defaultParams = pitr->second;
  }
  routeParamSettings = task->GetRoutingParamsSec()->GetParamSettings();
  pitr = routeParamSettings->find(gs);
  if (pitr != routeParamSettings->end()) {
    defaultRouteParams = pitr->second;
  }
  
  // Lake parameters are now handled through CSV file, not parameter sets

  CarveBasin(task->GetBasinSec(), &nodes, paramSettings, &fullParamSettings,
             &gaugeMap, defaultParams, routeParamSettings,
             &fullRouteParamSettings, defaultRouteParams, NULL, NULL, NULL,
             NULL, NULL, NULL);

  GridLoc *loc = (*(task->GetBasinSec()->GetGauges()))[0]->GetGridLoc();
  ClipBasicGrids(loc->x, loc->y, 10, task->GetOutput());
}

void ExecuteMakeBasic(TaskConfigSection *task) { MakeBasic(); }

void ExecuteMakeBasinAvg(TaskConfigSection *task) {
  Simulator sim;

  if (sim.Initialize(task)) {
    sim.BasinAvg();
    sim.CleanUp();
  }
}

bool CheckDirectoryExists(const char* path, const char* description) {
  if (!path || strlen(path) == 0) {
    return true; // Skip empty paths
  }
  
  // Extract directory from file path
  std::string dirPath = std::string(path);
  size_t lastSlash = dirPath.find_last_of("/\\");
  if (lastSlash != std::string::npos) {
    dirPath = dirPath.substr(0, lastSlash);
  } else {
    // If no slash found, it's just a filename in current directory
    return true;
  }
  
  if (dirPath.empty()) {
    return true; // Root directory
  }
  
  struct stat path_stat;
  if (stat(dirPath.c_str(), &path_stat) != 0) {
    ERROR_LOGF("Directory does not exist: %s (%s)", dirPath.c_str(), description);
    return false;
  }
  
  if (!S_ISDIR(path_stat.st_mode)) {
    ERROR_LOGF("Path exists but is not a directory: %s (%s)", dirPath.c_str(), description);
    return false;
  }
  
  return true;
}

bool CheckOptionalDirectoryExists(const char* path, const char* description) {
  if (!path || strlen(path) == 0) {
    return true; // Skip empty paths
  }
  
  // Extract directory from file path
  std::string dirPath = std::string(path);
  size_t lastSlash = dirPath.find_last_of("/\\");
  if (lastSlash != std::string::npos) {
    dirPath = dirPath.substr(0, lastSlash);
  } else {
    // If no slash found, it's just a filename in current directory
    return true;
  }
  
  if (dirPath.empty()) {
    return true; // Root directory
  }
  
  struct stat path_stat;
  if (stat(dirPath.c_str(), &path_stat) != 0) {
    WARNING_LOGF("Optional directory does not exist: %s (%s)", dirPath.c_str(), description);
    return true; // Don't fail for optional directories
  }
  
  if (!S_ISDIR(path_stat.st_mode)) {
    WARNING_LOGF("Optional path exists but is not a directory: %s (%s)", dirPath.c_str(), description);
    return true; // Don't fail for optional directories
  }
  
  return true;
}

bool ValidateAllDirectories() {
  bool allValid = true;
  
  // Check Basic section directories
  if (g_basicConfig) {
    allValid &= CheckDirectoryExists(g_basicConfig->GetDEM(), "DEM file directory");
    allValid &= CheckDirectoryExists(g_basicConfig->GetDDM(), "DDM file directory");
    allValid &= CheckDirectoryExists(g_basicConfig->GetFAM(), "FAM file directory");
  }
  
  // Check all task output directories
  if (g_executeConfig) {
    std::vector<TaskConfigSection *> *tasks = g_executeConfig->GetTasks();
    for (size_t i = 0; i < tasks->size(); i++) {
      TaskConfigSection *task = tasks->at(i);
      allValid &= CheckDirectoryExists(task->GetOutput(), 
                                      (std::string("Model output directory: ") + task->GetName()).c_str());
      
      // Check state directory if using states
      if (task->UseStates()) {
        allValid &= CheckDirectoryExists(task->GetState(), 
                                        (std::string("Model state directory: ") + task->GetName()).c_str());
      }
    }
    
    // Check ensemble task output directories
    std::vector<EnsTaskConfigSection *> *ensTasks = g_executeConfig->GetEnsTasks();
    for (size_t i = 0; i < ensTasks->size(); i++) {
      // Note: Ensemble tasks don't have their own output directories
      // They use the output directories of their constituent tasks
      // The output directories will be checked when processing individual tasks
    }
  }
  
  // Check precipitation directories
  for (std::map<std::string, PrecipConfigSection *>::iterator itr = g_precipConfigs.begin();
       itr != g_precipConfigs.end(); itr++) {
    PrecipConfigSection *precip = itr->second;
    allValid &= CheckDirectoryExists(precip->GetLoc(), 
                                    (std::string("Precipitation directory: ") + itr->first).c_str());
  }
  
  // Check PET directories
  for (std::map<std::string, PETConfigSection *>::iterator itr = g_petConfigs.begin();
       itr != g_petConfigs.end(); itr++) {
    PETConfigSection *pet = itr->second;
    allValid &= CheckDirectoryExists(pet->GetLoc(), 
                                    (std::string("PET directory: ") + itr->first).c_str());
  }
  
  // Check temperature directories (optional)
  for (std::map<std::string, TempConfigSection *>::iterator itr = g_tempConfigs.begin();
       itr != g_tempConfigs.end(); itr++) {
    TempConfigSection *temp = itr->second;
    CheckOptionalDirectoryExists(temp->GetLoc(), 
                                (std::string("Temperature directory: ") + itr->first).c_str());
    
    // Check DEM directory for temperature if specified (optional)
    if (temp->GetDEM() && strlen(temp->GetDEM()) > 0) {
      CheckOptionalDirectoryExists(temp->GetDEM(), 
                                  (std::string("Temperature DEM directory: ") + itr->first).c_str());
    }
  }
  
  // Check gauge observation file directories
  for (std::map<std::string, GaugeConfigSection *>::iterator itr = g_gaugeConfigs.begin();
       itr != g_gaugeConfigs.end(); itr++) {
    // Note: Gauge observation files are handled through TimeSeries, so we can't check them here
    // They will be checked when the gauge is processed
  }
  
  // Check parameter grid files (ParamGrid) - all optional
  // Check water balance parameter grids (optional)
  for (std::map<std::string, ParamSetConfigSection *>::iterator itr = g_paramSetConfigs[0].begin();
       itr != g_paramSetConfigs[0].end(); itr++) {
    ParamSetConfigSection *paramSet = itr->second;
    std::vector<std::string> *paramGrids = paramSet->GetParamGrids();
    for (size_t i = 0; i < paramGrids->size(); i++) {
      if (!(*paramGrids)[i].empty()) {
        char desc[256];
        sprintf(desc, "Water balance parameter grid: %s[%lu]", itr->first.c_str(), (unsigned long)i);
        CheckOptionalDirectoryExists((*paramGrids)[i].c_str(), desc);
      }
    }
  }
  
  // Check routing parameter grids (optional)
  for (std::map<std::string, RoutingParamSetConfigSection *>::iterator itr = g_routingParamSetConfigs[0].begin();
       itr != g_routingParamSetConfigs[0].end(); itr++) {
    RoutingParamSetConfigSection *routingParamSet = itr->second;
    std::vector<std::string> *paramGrids = routingParamSet->GetParamGrids();
    for (size_t i = 0; i < paramGrids->size(); i++) {
      if (!(*paramGrids)[i].empty()) {
        char desc[256];
        sprintf(desc, "Routing parameter grid: %s[%lu]", itr->first.c_str(), (unsigned long)i);
        CheckOptionalDirectoryExists((*paramGrids)[i].c_str(), desc);
      }
    }
  }
  
  // Check snow parameter grids (optional)
  for (std::map<std::string, SnowParamSetConfigSection *>::iterator itr = g_snowParamSetConfigs[0].begin();
       itr != g_snowParamSetConfigs[0].end(); itr++) {
    SnowParamSetConfigSection *snowParamSet = itr->second;
    std::vector<std::string> *paramGrids = snowParamSet->GetParamGrids();
    for (size_t i = 0; i < paramGrids->size(); i++) {
      if (!(*paramGrids)[i].empty()) {
        char desc[256];
        sprintf(desc, "Snow parameter grid: %s[%lu]", itr->first.c_str(), (unsigned long)i);
        CheckOptionalDirectoryExists((*paramGrids)[i].c_str(), desc);
      }
    }
  }
  
  // Check inundation parameter grids (optional)
  for (std::map<std::string, InundationParamSetConfigSection *>::iterator itr = g_inundationParamSetConfigs[0].begin();
       itr != g_inundationParamSetConfigs[0].end(); itr++) {
    InundationParamSetConfigSection *inundationParamSet = itr->second;
    std::vector<std::string> *paramGrids = inundationParamSet->GetParamGrids();
    for (size_t i = 0; i < paramGrids->size(); i++) {
      if (!(*paramGrids)[i].empty()) {
        char desc[256];
        sprintf(desc, "Inundation parameter grid: %s[%lu]", itr->first.c_str(), (unsigned long)i);
        CheckOptionalDirectoryExists((*paramGrids)[i].c_str(), desc);
      }
    }
  }
  
  // Check lake CSV file directories and engineered discharge files
  for (std::map<std::string, BasinConfigSection *>::iterator itr = g_basinConfigs.begin();
       itr != g_basinConfigs.end(); itr++) {
    // Note: Lake CSV files and engineered discharge files are read during basin processing
    // We can't check them here as they're not stored as separate paths in the config
    // They will be checked when the basin is processed
  }
  
  return allValid;
}




