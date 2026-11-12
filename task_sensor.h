#pragma once

#include "ct_types.h"

#include <cstdint>

using SensorSelectMux = bool (*)(void* context, uint8_t muxChannel);
using SensorDelayUs = void (*)(void* context, uint32_t delayUs);
using SensorReadSample = bool (*)(void* context, uint8_t channelIndex, AdsPair pair, float& millivolts);

struct SensorScanAdapter {
  void* context;
  bool (*initialize)(void* context);
  SensorSelectMux selectMux;
  SensorDelayUs delayUs;
  SensorReadSample readSampleMv;
  uint64_t (*nowMillis)(void* context);
};

void ctConfigureSensorScan(const SensorScanAdapter& adapter);
bool ctMeasureChannel(uint8_t channelIndex, CtReading& reading);
bool ctScanOnce(CtSnapshot& snapshot);
void TaskSensor(void* parameter);
