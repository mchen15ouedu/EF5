#pragma once
#include <string>
#include <map>
#include <vector>
#include "ModelBase.h"
#include "TimeUnit.h"
#include "GridNode.h"
#include "Grid.h"
#include "Model.h"

// Forward declarations
class LakeMap;
class GaugeConfigSection;
class GridWriterFull;

struct LakeInfo {
    std::string name;
    double lat;
    double lon;
    double th_volume;
    double area;
    double retention_constant; // Retention constant K (hours)
    double obsFlowAccum; // Observed flow accumulation (optional)
    bool obsFlowAccumSet; // Flag indicating if observed flow accumulation is set
    
    // Default constructor for proper initialization
    LakeInfo() : lat(0.0), lon(0.0), th_volume(0.0), area(0.0), 
                 retention_constant(24.0), obsFlowAccum(0.0), obsFlowAccumSet(false) {}
};

// Lake state enumeration
enum STATES_LAKE { 
    STATE_LAKE_STORAGE, 
    STATE_LAKE_OUTFLOW, 
    STATE_LAKE_QTY 
};

// Lake grid node structure for state saving
struct LakeGridNode : BasicGridNode {
    float params[PARAM_LAKE_QTY];
    float states[STATE_LAKE_QTY];
    
    // Lake-specific data
    std::string lakeName;
    double storage;      // Current storage (m^3)
    double area;         // Surface area (m^2)
    double outflow;      // Outflow (m^3/s)
    double inflow;       // Inflow (m^3/s)
    double precipitation;// Precipitation (mm or m)
    double evaporation;  // Evaporation (mm or m)
    double th_volume;   // Threshold volume (m^3)
    double retentionConstant; // Retention constant K (hours)
    bool wm_flag;        // Use engineered discharge if true
    
    // Default constructor for proper initialization
    LakeGridNode() : storage(0.0), area(0.0), outflow(0.0), inflow(0.0),
                     precipitation(0.0), evaporation(0.0), th_volume(0.0),
                     retentionConstant(24.0), wm_flag(false) {
        // Initialize parameter and state arrays
        for (int i = 0; i < PARAM_LAKE_QTY; i++) {
            params[i] = 0.0f;
        }
        for (int i = 0; i < STATE_LAKE_QTY; i++) {
            states[i] = 0.0f;
        }
    }
};

class LakeModelImpl : public WaterBalanceModel {
public:
    LakeModelImpl(const LakeInfo& info, bool wm_flag = false, std::map<std::string, double>* engineeredDischarge = NULL);
    ~LakeModelImpl();

    // Model interface methods
    bool InitializeModel(std::vector<GridNode> *nodes,
                        std::map<GaugeConfigSection *, float *> *paramSettings,
                        std::vector<FloatGrid *> *paramGrids);
    void InitializeStates(TimeVar *beginTime, char *statePath);
    void SaveStates(TimeVar *currentTime, char *statePath,
                    GridWriterFull *gridWriter);

    const char *GetName() { return "lake"; }
    bool IsLumped() { return false; } // Lake model is distributed
    
    // Lake-specific methods
    std::string GetLakeName() const { return lakeName; }
    double GetStorage() const { return storage; }
    double GetOutflow() const { return outflow; }
    void SetRetentionConstant(double K) { retentionConstant = K; }
    double GetRetentionConstant() const { return retentionConstant; }
    double GetThVolume() const { return th_volume; }
    
    // Location and flow accumulation methods (similar to gauges)
    double GetLat() const { return lat; }
    double GetLon() const { return lon; }
    GridLoc* GetLocation() { return &gridLoc; }
    bool HasObsFlowAccum() const { return obsFlowAccumSet; }
    double GetObsFlowAccum() const { return obsFlowAccum; }
    void SetObsFlowAccum(double value) { obsFlowAccum = value; obsFlowAccumSet = true; }

    // Simulate one timestep (legacy method for backward compatibility)
    void Step(const std::string& timestamp, double inflow, double precipitation, double evaporation, double dt);

    // WaterBalanceModel interface method
    bool WaterBalance(float stepHours,
                      std::vector<float> *precip,
                      std::vector<float> *pet,
                      std::vector<float> *fastFlow,
                      std::vector<float> *interFlow,
                      std::vector<float> *baseFlow,
                      std::vector<float> *soilMoisture,
                      std::vector<float> *groundwater);
    
    // Two-part lake balance approach
    void ApplyVerticalBalance(float stepHours, std::vector<float>* precip, std::vector<float>* pet);
    void ApplyHorizontalBalance(float stepHours, std::vector<float>* currentQ, std::vector<GridNode>* nodes, TimeVar* currentTime, LakeMap* lakeMap);

    // Members
    std::string lakeName;    // Lake name
    double storage;      // Current storage (m^3)
    double area;         // Surface area (m^2)
    double outflow;      // Outflow (m^3/s)
    double inflow;       // Inflow (m^3/s)
    double precipitation;// Precipitation (mm or m)
    double evaporation;  // Evaporation (mm or m)
    double dt;           // Timestep (s)
    double th_volume;   // Threshold volume (m^3)
    bool wm_flag;        // Use engineered discharge if true
    std::map<std::string, double>* engineeredDischarge; // Pointer to engineered discharge time series
    
    // Linear reservoir parameter for dry season outflow
    double retentionConstant; // Retention constant K (hours)
    
    // Grid-based state management
    std::vector<GridNode> *nodes;
    std::vector<LakeGridNode> lakeNodes;
    int lakeNodeIndex; // Index of the grid node where this lake is located
    
    // Location and flow accumulation (similar to gauges)
    double lat;
    double lon;
    double obsFlowAccum;
    bool obsFlowAccumSet;
    GridLoc gridLoc;
};

// Legacy LakeModel class for backward compatibility
class LegacyLakeModel {
public:
    LegacyLakeModel(const LakeInfo& info, bool wm_flag = false, std::map<std::string, double>* engineeredDischarge = NULL);

    // Simulate one timestep
    void Step(const std::string& timestamp, double inflow, double precipitation, double evaporation, double dt);

    double GetOutflow() const;
    double GetStorage() const;
    double GetThVolume() const;
    std::string GetLakeName() const;

    // Linear reservoir parameter getters and setters
    void SetRetentionConstant(double K) { retentionConstant = K; }
    double GetRetentionConstant() const { return retentionConstant; }

    // Members
    std::string lakeName;    // Lake name
    double storage;      // Current storage (m^3)
    double area;         // Surface area (m^2)
    double outflow;      // Outflow (m^3/s)
    double inflow;       // Inflow (m^3/s)
    double precipitation;// Precipitation (mm or m)
    double evaporation;  // Evaporation (mm or m)
    double dt;           // Timestep (s)
    double th_volume;   // Threshold volume (m^3)
    bool wm_flag;        // Use engineered discharge if true
    std::map<std::string, double>* engineeredDischarge; // Pointer to engineered discharge time series
    
    // Linear reservoir parameter for dry season outflow
    double retentionConstant; // Retention constant K (hours)
};

// CSV utility for reading engineered discharge
void ReadEngineeredDischargeCSV(const std::string& filename, std::map<std::string, double>& dischargeMap);
void ReadEngineeredDischargeCSV(const std::string& filename, std::map<std::string, std::map<std::string, double> >& dischargeMap);

void ReadLakeInfoCSV(const std::string& filename, std::map<std::string, LakeInfo>& lakeInfoMap); 