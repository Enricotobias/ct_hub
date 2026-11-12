#include "runtime_state.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace {
RuntimeEvent g_events[RUNTIME_EVENT_QUEUE_DEPTH]{};
uint8_t g_eventHead = 0;
uint8_t g_eventTail = 0;
uint8_t g_eventCount = 0;
RuntimeCommand g_commands[RUNTIME_COMMAND_QUEUE_DEPTH]{};
uint8_t g_commandHead = 0;
uint8_t g_commandTail = 0;
uint8_t g_commandCount = 0;
RuntimeStats g_stats{};
bool g_ready = false;
PendingReboot g_pendingReboot{};
SensorMaintenanceState g_sensorMaintenance{};
NetworkMaintenanceState g_networkMaintenance{};
OtaRequest g_otaRequests[RUNTIME_OTA_QUEUE_DEPTH]{};
uint8_t g_otaHead = 0;
uint8_t g_otaTail = 0;
uint8_t g_otaCount = 0;
RuntimeEvent g_otaResult{};
bool g_otaResultPending = false;
RuntimeEvent g_otaProgress{};
bool g_otaProgressPending = false;
SemaphoreHandle_t g_stateMutex = nullptr;

bool lockState() {
  return g_stateMutex != nullptr && xSemaphoreTake(g_stateMutex, portMAX_DELAY) == pdTRUE;
}

void unlockState() { xSemaphoreGive(g_stateMutex); }
}

RuntimeInitResult runtimeInit() {
  if (g_stateMutex == nullptr) g_stateMutex = xSemaphoreCreateMutex();
  if (g_stateMutex == nullptr || !lockState()) return {false, false, false};
  g_eventHead = 0;
  g_eventTail = 0;
  g_eventCount = 0;
  g_commandHead = 0;
  g_commandTail = 0;
  g_commandCount = 0;
  g_stats = {};
  g_ready = true;
  g_pendingReboot = {};
  g_sensorMaintenance = {};
  g_networkMaintenance = {};
  g_otaHead = 0;
  g_otaTail = 0;
  g_otaCount = 0;
  g_otaResult = {};
  g_otaResultPending = false;
  g_otaProgress = {};
  g_otaProgressPending = false;
  unlockState();
  return {true, true, true};
}

bool runtimePostEvent(const RuntimeEvent& event) {
  if (!g_ready || !lockState()) return false;
  if (g_eventCount >= RUNTIME_EVENT_QUEUE_DEPTH) { unlockState(); return false; }
  g_events[g_eventTail] = event;
  g_eventTail = static_cast<uint8_t>((g_eventTail + 1U) % RUNTIME_EVENT_QUEUE_DEPTH);
  ++g_eventCount;
  unlockState();
  return true;
}

bool runtimeTakeEvent(RuntimeEvent& event) {
  if (!g_ready || !lockState()) return false;
  if (g_eventCount == 0) { unlockState(); return false; }
  event = g_events[g_eventHead];
  g_eventHead = static_cast<uint8_t>((g_eventHead + 1U) % RUNTIME_EVENT_QUEUE_DEPTH);
  --g_eventCount;
  unlockState();
  return true;
}

bool runtimePostCommand(const RuntimeCommand& command) {
  if (!g_ready || !lockState()) return false;
  if (g_commandCount >= RUNTIME_COMMAND_QUEUE_DEPTH) { unlockState(); return false; }
  g_commands[g_commandTail] = command;
  g_commandTail = static_cast<uint8_t>((g_commandTail + 1U) % RUNTIME_COMMAND_QUEUE_DEPTH);
  ++g_commandCount;
  unlockState();
  return true;
}

bool runtimeTakeCommand(RuntimeCommand& command) {
  if (!g_ready || !lockState()) return false;
  if (g_commandCount == 0) { unlockState(); return false; }
  command = g_commands[g_commandHead];
  g_commandHead = static_cast<uint8_t>((g_commandHead + 1U) % RUNTIME_COMMAND_QUEUE_DEPTH);
  --g_commandCount;
  unlockState();
  return true;
}

void runtimeMarkHeartbeat(TaskKind task, uint64_t nowMonoMs) {
  if (!g_ready || !lockState()) return;
  if (task == TaskKind::Sensor) g_stats.sensorHeartbeatMs = nowMonoMs;
  if (task == TaskKind::Network) g_stats.networkHeartbeatMs = nowMonoMs;
  if (task == TaskKind::Health) g_stats.healthHeartbeatMs = nowMonoMs;
  if (task == TaskKind::Ota) g_stats.otaHeartbeatMs = nowMonoMs;
  unlockState();
}

void runtimeCopyStats(RuntimeStats& output) {
  if (!g_ready || !lockState()) { output = {}; return; }
  output = g_stats;
  unlockState();
}

void runtimeSetPendingReboot(const PendingReboot& state) {
  if (!g_ready || !lockState()) return;
  g_pendingReboot = state;
  unlockState();
}
bool runtimeGetPendingReboot(PendingReboot& state) {
  if (!g_ready || !lockState()) { state = {}; return false; }
  state = g_pendingReboot;
  unlockState();
  return state.pending;
}
void runtimeClearPendingReboot() {
  if (!g_ready || !lockState()) return;
  g_pendingReboot = {};
  unlockState();
}

bool runtimeRequestSensorPause(uint32_t token) {
  if (!g_ready || token == 0 || !lockState()) return false;
  if (g_sensorMaintenance.token != 0 && g_sensorMaintenance.token != token) {
    unlockState();
    return false;
  }
  g_sensorMaintenance.token = token;
  g_sensorMaintenance.pauseRequested = true;
  g_sensorMaintenance.resumeRequested = false;
  unlockState();
  return true;
}

bool runtimeRequestSensorResume(uint32_t token) {
  if (!g_ready || token == 0 || !lockState()) return false;
  const bool accepted = g_sensorMaintenance.token == token &&
                       (g_sensorMaintenance.paused || g_sensorMaintenance.resumeRequested);
  if (accepted) g_sensorMaintenance.resumeRequested = true;
  unlockState();
  return accepted;
}

bool runtimeCancelSensorPause(uint32_t token) {
  if (!g_ready || token == 0 || !lockState()) return false;
  const bool cancelled = g_sensorMaintenance.token == token &&
                         g_sensorMaintenance.pauseRequested && !g_sensorMaintenance.paused;
  if (cancelled) g_sensorMaintenance = {};
  unlockState();
  return cancelled;
}

void runtimeCopySensorMaintenance(SensorMaintenanceState& output) {
  if (!g_ready || !lockState()) { output = {}; return; }
  output = g_sensorMaintenance;
  unlockState();
}

bool runtimeAcknowledgeSensorPause(uint32_t token, uint64_t nowMonoMs) {
  if (!g_ready || !lockState()) return false;
  if (g_sensorMaintenance.token != token || !g_sensorMaintenance.pauseRequested) {
    unlockState();
    return false;
  }
  g_sensorMaintenance.pauseRequested = false;
  g_sensorMaintenance.paused = true;
  unlockState();
  RuntimeEvent event{};
  event.type = RuntimeEventType::SensorPaused;
  event.token = token;
  event.monotonicMs = nowMonoMs;
  (void)runtimePostEvent(event);
  return true;
}

bool runtimeAcknowledgeSensorResume(uint32_t token, uint64_t nowMonoMs) {
  if (!g_ready || !lockState()) return false;
  if (g_sensorMaintenance.token != token || !g_sensorMaintenance.resumeRequested) {
    unlockState();
    return false;
  }
  g_sensorMaintenance = {};
  unlockState();
  RuntimeEvent event{};
  event.type = RuntimeEventType::SensorResumed;
  event.token = token;
  event.monotonicMs = nowMonoMs;
  (void)runtimePostEvent(event);
  return true;
}

bool runtimeRequestNetworkMaintenance(uint32_t token) {
  if (!g_ready || token == 0 || !lockState()) return false;
  if (g_networkMaintenance.token != 0 && g_networkMaintenance.token != token) {
    unlockState();
    return false;
  }
  g_networkMaintenance.token = token;
  g_networkMaintenance.requested = true;
  unlockState();
  return true;
}

bool runtimeReleaseNetworkMaintenance(uint32_t token) {
  if (!g_ready || token == 0 || !lockState()) return false;
  if (g_networkMaintenance.token != token) { unlockState(); return false; }
  g_networkMaintenance.requested = false;
  g_networkMaintenance.granted = false;
  unlockState();
  return true;
}

void runtimeCopyNetworkMaintenance(NetworkMaintenanceState& output) {
  if (!g_ready || !lockState()) { output = {}; return; }
  output = g_networkMaintenance;
  unlockState();
}

bool runtimeAcknowledgeNetworkMaintenance(uint32_t token, bool granted, uint64_t nowMonoMs) {
  if (!g_ready || !lockState()) return false;
  if (g_networkMaintenance.token != token || !g_networkMaintenance.requested) {
    unlockState();
    return false;
  }
  g_networkMaintenance.granted = granted;
  unlockState();
  RuntimeEvent event{};
  event.type = granted ? RuntimeEventType::NetworkMaintenanceGranted
                       : RuntimeEventType::NetworkMaintenanceReleased;
  event.token = token;
  event.monotonicMs = nowMonoMs;
  (void)runtimePostEvent(event);
  return true;
}

bool runtimePostOtaRequest(const OtaRequest& request) {
  if (!g_ready || !lockState()) return false;
  if (g_otaCount >= RUNTIME_OTA_QUEUE_DEPTH) { unlockState(); return false; }
  g_otaRequests[g_otaTail] = request;
  g_otaTail = static_cast<uint8_t>((g_otaTail + 1U) % RUNTIME_OTA_QUEUE_DEPTH);
  ++g_otaCount;
  unlockState();
  return true;
}

bool runtimeTakeOtaRequest(OtaRequest& request) {
  if (!g_ready || !lockState()) return false;
  if (g_otaCount == 0) { unlockState(); return false; }
  request = g_otaRequests[g_otaHead];
  g_otaHead = static_cast<uint8_t>((g_otaHead + 1U) % RUNTIME_OTA_QUEUE_DEPTH);
  --g_otaCount;
  unlockState();
  return true;
}

bool runtimePostOtaResult(const RuntimeEvent& event) {
  if (!g_ready || event.type != RuntimeEventType::OtaFinished || !lockState()) return false;
  g_otaResult = event;
  g_otaResultPending = true;
  unlockState();
  return true;
}

bool runtimeTakeOtaResult(RuntimeEvent& event) {
  if (!g_ready || !lockState()) return false;
  if (!g_otaResultPending) { unlockState(); return false; }
  event = g_otaResult;
  g_otaResult = {};
  g_otaResultPending = false;
  unlockState();
  return true;
}

bool runtimePostOtaProgress(const RuntimeEvent& event) {
  if (!g_ready || event.type != RuntimeEventType::OtaProgress || !lockState()) return false;
  g_otaProgress = event;
  g_otaProgressPending = true;
  unlockState();
  return true;
}

bool runtimeTakeOtaProgress(RuntimeEvent& event) {
  if (!g_ready || !lockState()) return false;
  if (!g_otaProgressPending) { unlockState(); return false; }
  event = g_otaProgress;
  g_otaProgress = {};
  g_otaProgressPending = false;
  unlockState();
  return true;
}

void runtimeMarkOtaProgress(uint8_t phase, uint32_t bytes, uint64_t nowMonoMs) {
  if (!g_ready || !lockState()) return;
  g_stats.otaPhase = phase;
  g_stats.otaProgressBytes = bytes;
  g_stats.otaHeartbeatMs = nowMonoMs;
  unlockState();
}
