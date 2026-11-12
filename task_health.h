#pragma once

#include "runtime_state.h"
#include "mqtt_schema.h"

#include <cstdint>

struct HealthInput {
  uint64_t nowMonoMs;
  uint64_t sensorHeartbeatMs;
  uint64_t networkHeartbeatMs;
  bool wifiConnected;
  bool mqttConnected;
  bool sensorProgressing;
  bool resourceFailure;
  bool runningImageMatchesTarget;
};

struct HealthDecision {
  bool bootFailure;
  bool sensorFault;
  bool networkFault;
  DeviceMode mode;
};

struct RgbOutput { bool red; bool green; bool blue; bool blinking; };

struct HealthAdapter {
  void* context;
  bool (*readInput)(void* context, HealthInput& input);
  void (*writeRgb)(void* context, const RgbOutput& output);
  uint64_t (*nowMillis)(void* context);
};

HealthDecision healthEvaluate(const HealthInput& input);
RgbOutput healthLedOutput(const HealthDecision& decision, uint64_t nowMonoMs);
void healthConfigure(const HealthAdapter& adapter);
void healthResetOtaGate();
bool healthOtaGateStep(const HealthInput& input);
void TaskHealth(void* parameter);
