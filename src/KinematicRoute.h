#ifndef KW_MODEL_H
#define KW_MODEL_H

#include "ModelBase.h"

class LakeModelImpl; // forward decl: lake cells are coupled in-sweep (see Route)
class TimeVar;

enum KW_LAYER {
  KW_LAYER_FASTFLOW,
  KW_LAYER_INTERFLOW,
  KW_LAYER_BASEFLOW,
  KW_LAYER_QTY,
};

enum STATES_KW { STATE_KW_PQ, STATE_KW_PO, STATE_KW_IR, STATE_KW_QTY };

struct KWGridNode : BasicGridNode {
  float params[PARAM_KINEMATIC_QTY];
  float states[STATE_KW_QTY];

  bool channelGridCell;
  double slopeSqrt;
  double hillSlopeSqrt;

  double
      nexTime[KW_LAYER_QTY]; // This is a by product of computing cell routing
  KWGridNode *routeCNode[2][KW_LAYER_QTY]; // This is the node we route water to
  GridNode *routeNode[2][KW_LAYER_QTY];
  double routeAmount[2][KW_LAYER_QTY];
  double incomingWater[KW_LAYER_QTY];

  // double reservoirs[KW_LAYER_QTY]; // CREST has two excess storage reservoirs
  // (overland & interflow)

  // double previousStreamflow; //cms
  // double previousOverland;
  double incomingWaterOverland, incomingWaterChannel;

  // Non-NULL if this channel cell is a lake/reservoir outlet. When set, channel
  // routing at this cell is replaced by the reservoir step so the regulated
  // outflow propagates downstream within the same sweep.
  LakeModelImpl *lakeModel;
};

class KWRoute : public RoutingModel {

public:
  KWRoute();
  ~KWRoute();
  float SetObsInflow(long index, float inflow);
  bool InitializeModel(std::vector<GridNode> *newNodes,
                       std::map<GaugeConfigSection *, float *> *paramSettings,
                       std::vector<FloatGrid *> *paramGrids);
  void InitializeStates(TimeVar *beginTime, char *statePath,
                        std::vector<float> *fastFlow,
                        std::vector<float> *interFlow,
                        std::vector<float> *baseFlow);
  void SaveStates(TimeVar *currentTime, char *statePath,
                  GridWriterFull *gridWriter);
  bool Route(float stepHours, std::vector<float> *fastFlow,
             std::vector<float> *interFlow, std::vector<float> *baseFlow, std::vector<float> *discharge);
  float GetMaxSpeed() { return maxSpeed; }

  // Couple a lake/reservoir to the routing cell at the given node index. After
  // this, the cell's channel outflow is governed by the reservoir step rather
  // than the kinematic wave, so lake regulation reaches downstream cells.
  void RegisterLake(long nodeIndex, LakeModelImpl *lake);
  // Provide the current simulation time so the in-sweep reservoir step can look
  // up engineered (dam) discharge. NULL disables engineered lookup.
  void SetCurrentTime(TimeVar *t) { currentRouteTime = t; }

private:
  void RouteInt(float stepSeconds, GridNode *node, KWGridNode *cNode,
                float fastFlow, float interFlow, float baseFlow);
  void
  InitializeParameters(std::map<GaugeConfigSection *, float *> *paramSettings,
                       std::vector<FloatGrid *> *paramGrids);
  void InitializeRouting(float timeSeconds);

  std::vector<GridNode> *nodes;
  std::vector<KWGridNode> kwNodes;
  float maxSpeed;
  bool initialized;
  bool hasLakes;            // true once at least one lake cell is registered
  TimeVar *currentRouteTime; // current sim time for engineered-discharge lookup
};

#endif
