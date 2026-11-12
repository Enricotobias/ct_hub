#include "task_health.h"

#include "config.h"
#include "debug_log.h"
#include "ota.h"

#include <Arduino.h>

namespace {
HealthAdapter g_adapter{};
uint64_t g_otaHealthySinceMs = 0;
bool g_otaGateFinished = false;
}

HealthDecision healthEvaluate(const HealthInput& input) {
  HealthDecision output{};
  output.bootFailure = input.resourceFailure;
  output.sensorFault = !input.sensorProgressing && input.nowMonoMs > input.sensorHeartbeatMs + 2000;
  output.networkFault = input.nowMonoMs > input.networkHeartbeatMs + 20000;
  if (output.bootFailure) output.mode = DeviceMode::Offline;
  else if (!input.wifiConnected) output.mode = DeviceMode::Offline;
  else if (!input.mqttConnected || output.networkFault) output.mode = DeviceMode::Degraded;
  else if (output.sensorFault) output.mode = DeviceMode::Degraded;
  else output.mode = DeviceMode::Online;
  return output;
}

RgbOutput healthLedOutput(const HealthDecision& decision, uint64_t nowMonoMs) {
  const bool phase = ((nowMonoMs / 500U) % 2U) == 0U;
  if (otaCurrentPhase() != OtaPhase::Idle) {
    return {phase, false, phase, true};
  }
  if (decision.mode == DeviceMode::Offline) return {phase, false, false, true};
  if (decision.networkFault) return {false, false, phase, true};
  if (decision.sensorFault) return {phase, phase, false, true};
  return {false, true, false, false};
}

void healthConfigure(const HealthAdapter& adapter) { g_adapter = adapter; }

void healthResetOtaGate() {
  g_otaHealthySinceMs = 0;
  g_otaGateFinished = false;
}

bool healthOtaGateStep(const HealthInput& input) {
  if (g_otaGateFinished || otaCurrentPhase() != OtaPhase::AwaitingHealthValidation) return false;
  bool runningMatchesTarget = false;
  if (!otaCheckRunningExpectedImage(runningMatchesTarget)) return false;
  if (!runningMatchesTarget) {
    g_otaGateFinished = true;
    return false;
  }
  const bool healthy = !input.resourceFailure && input.sensorProgressing &&
                       input.nowMonoMs >= input.sensorHeartbeatMs &&
                       input.nowMonoMs - input.sensorHeartbeatMs <= 2000U;
  if (!healthy) {
    g_otaHealthySinceMs = 0;
    return false;
  }
  if (g_otaHealthySinceMs == 0) g_otaHealthySinceMs = input.nowMonoMs;
  if (input.nowMonoMs - g_otaHealthySinceMs < CT_OTA_HEALTH_GATE_MS) return false;

  const bool completed = otaCompleteHealthValidation(input.nowMonoMs);
  g_otaGateFinished = completed;
  return completed;
}

void TaskHealth(void* parameter) {
  const HealthAdapter* adapter = parameter == nullptr ? &g_adapter : static_cast<const HealthAdapter*>(parameter);
  ctDebugLog("INFO", "HEALTH", "TaskHealth started; interval_ms=500");
  bool hasPreviousDecision = false;
  HealthDecision previousDecision{};
  bool inputFailureLogged = false;
  for (;;) {
    if (adapter != nullptr && adapter->readInput != nullptr) {
      HealthInput input{};
      if (adapter->readInput(adapter->context, input)) {
        inputFailureLogged = false;
        const uint64_t now = adapter->nowMillis == nullptr ? millis() : adapter->nowMillis(adapter->context);
        input.nowMonoMs = now;
        const HealthDecision decision = healthEvaluate(input);
        if (!hasPreviousDecision || decision.mode != previousDecision.mode ||
            decision.bootFailure != previousDecision.bootFailure ||
            decision.sensorFault != previousDecision.sensorFault ||
            decision.networkFault != previousDecision.networkFault) {
          ctDebugLog("INFO", "HEALTH",
                     "state mode=%u boot_failure=%u sensor_fault=%u network_fault=%u wifi=%u mqtt=%u",
                     static_cast<unsigned>(decision.mode), decision.bootFailure ? 1U : 0U,
                     decision.sensorFault ? 1U : 0U, decision.networkFault ? 1U : 0U,
                     input.wifiConnected ? 1U : 0U, input.mqttConnected ? 1U : 0U);
          previousDecision = decision;
          hasPreviousDecision = true;
        }
        if (adapter->writeRgb != nullptr) adapter->writeRgb(adapter->context, healthLedOutput(decision, now));
        if (healthOtaGateStep(input)) {
          ctDebugLog("INFO", "HEALTH", "OTA post-boot health validation completed");
        }
        runtimeMarkHeartbeat(TaskKind::Health, now);
      } else if (!inputFailureLogged) {
        ctDebugLog("ERROR", "HEALTH", "health input read failed");
        inputFailureLogged = true;
      }
    } else if (!inputFailureLogged) {
      ctDebugLog("ERROR", "HEALTH", "health adapter is unavailable");
      inputFailureLogged = true;
    }
    delay(500);
  }
}
