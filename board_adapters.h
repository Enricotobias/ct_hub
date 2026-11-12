#pragma once

#include "config.h"
#include "task_health.h"
#include "task_network.h"

void boardConfigureSensorAdapters();
void boardConfigureNetworkAdapters(CalibrationPersistence& calibration,
                                   volatile bool& resourceFailure);
void boardConfigureHealthAdapter(volatile bool& resourceFailure);
NetworkTaskAdapter* boardNetworkTaskAdapter();
HealthAdapter* boardHealthAdapter();
