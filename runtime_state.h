#pragma once

#include "ct_types.h"
#include "ota.h"

#include <cstdint>

constexpr uint8_t RUNTIME_COMMAND_QUEUE_DEPTH = 4;
constexpr uint8_t RUNTIME_EVENT_QUEUE_DEPTH = 8;
constexpr uint16_t RUNTIME_COMMAND_PAYLOAD_SIZE = 2048;
constexpr uint8_t RUNTIME_OTA_QUEUE_DEPTH = 1;

enum class TaskKind : uint8_t { Sensor, Network, Health, Ota };

struct RuntimeInitResult {
  bool mutexReady;
  bool commandQueueReady;
  bool eventQueueReady;
};

struct RuntimeCommand {
  uint8_t type;
  uint16_t length;
  char payload[RUNTIME_COMMAND_PAYLOAD_SIZE];
};

RuntimeInitResult runtimeInit();
bool runtimePostEvent(const RuntimeEvent& event);
bool runtimeTakeEvent(RuntimeEvent& event);
bool runtimePostCommand(const RuntimeCommand& command);
bool runtimeTakeCommand(RuntimeCommand& command);
void runtimeMarkHeartbeat(TaskKind task, uint64_t nowMonoMs);
void runtimeCopyStats(RuntimeStats& output);

struct PendingReboot { bool pending; uint64_t deadlineMonoMs; char requestId[40]; };
void runtimeSetPendingReboot(const PendingReboot& state);
bool runtimeGetPendingReboot(PendingReboot& state);
void runtimeClearPendingReboot();

struct SensorMaintenanceState {
  uint32_t token;
  bool pauseRequested;
  bool paused;
  bool resumeRequested;
};

struct NetworkMaintenanceState {
  uint32_t token;
  bool requested;
  bool granted;
};

bool runtimeRequestSensorPause(uint32_t token);
bool runtimeRequestSensorResume(uint32_t token);
bool runtimeCancelSensorPause(uint32_t token);
void runtimeCopySensorMaintenance(SensorMaintenanceState& output);
bool runtimeAcknowledgeSensorPause(uint32_t token, uint64_t nowMonoMs);
bool runtimeAcknowledgeSensorResume(uint32_t token, uint64_t nowMonoMs);
bool runtimeRequestNetworkMaintenance(uint32_t token);
bool runtimeReleaseNetworkMaintenance(uint32_t token);
void runtimeCopyNetworkMaintenance(NetworkMaintenanceState& output);
bool runtimeAcknowledgeNetworkMaintenance(uint32_t token, bool granted, uint64_t nowMonoMs);
bool runtimePostOtaRequest(const OtaRequest& request);
bool runtimeTakeOtaRequest(OtaRequest& request);
bool runtimePostOtaResult(const RuntimeEvent& event);
bool runtimeTakeOtaResult(RuntimeEvent& event);
bool runtimePostOtaProgress(const RuntimeEvent& event);
bool runtimeTakeOtaProgress(RuntimeEvent& event);
void runtimeMarkOtaProgress(uint8_t phase, uint32_t bytes, uint64_t nowMonoMs);
