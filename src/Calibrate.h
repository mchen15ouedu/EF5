#ifndef CALIBRATE_H
#define CALIBRATE_H

#include "CaliParamConfigSection.h"
#include "ObjectiveFunc.h"
#include "Simulator.h"
#include "LakeCaliParamConfigSection.h"

class Calibrate {
public:
  virtual void
  Initialize(CaliParamConfigSection *caliParamConfigNew,
             RoutingCaliParamConfigSection *routingCaliParamConfigNew,
             SnowCaliParamConfigSection *snowCaliParamConfigNew,
             LakeCaliParamConfigSection *lakeCaliParamConfigNew,
             int numParamsWBNew, int numParamsRNew, int numParamsSNew, int numParamsLNew,
             Simulator *simNew) = 0;
  virtual void CalibrateParams() = 0;

protected:
  Simulator *sim;
  int numParams, numParamsWB, numParamsR, numParamsS, numParamsL;
  CaliParamConfigSection *caliParamConfig;
  RoutingCaliParamConfigSection *routingCaliParamConfig;
  SnowCaliParamConfigSection *snowCaliParamConfig;
  LakeCaliParamConfigSection *lakeCaliParamConfig;
};

#endif
