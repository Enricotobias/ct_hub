#pragma once

#include "ct_store.h"
#include "mqtt_schema.h"
#include "runtime_state.h"

#include <cstdint>

constexpr uint64_t NETWORK_DATA_INTERVAL_MS = 10000;
constexpr uint64_t NETWORK_INFO_INTERVAL_MS = 60000;

struct NetworkScheduleInput {
  uint64_t nowMonoMs;
  uint64_t lastDataPublishMs;
  uint64_t lastInfoPublishMs;
  uint64_t reconnectStartedMs;
  uint32_t currentScanSequence;
  uint32_t lastPublishedScanSequence;
  uint32_t publishFailures;
  bool connected;
};

struct NetworkScheduleDecision {
  bool publishStatus;
  bool publishInfo;
  bool publishData;
  bool waitForFreshScan;
  uint32_t publishFailureCount;
};

enum class NetworkCommandType : uint8_t { Info = 0, Calibration = 1, Reboot = 2, Config = 4 };

using NetworkAckCallback = bool (*)(void* context, const AckEvent& event);
using NetworkInfoCallback = bool (*)(void* context);
using NetworkCalibrationCallback = bool (*)(void* context, uint8_t channel, float gain);
using NetworkActiveChannelCallback = bool (*)(void* context, uint8_t channelCount);
using NetworkRebootCallback = bool (*)(void* context, uint8_t delaySeconds,
                                       const char* requestId);
using NetworkMessageCallback = void (*)(void* context, const char* topic,
                                        const uint8_t* payload, std::size_t length);

struct NetworkCommandAdapter {
  void* context;
  NetworkAckCallback publishAck;
  NetworkInfoCallback publishInfo;
  NetworkCalibrationCallback applyCalibration;
  NetworkActiveChannelCallback applyActiveChannels;
  NetworkRebootCallback scheduleReboot;
};

struct NetworkTaskAdapter {
  void* context;
  uint64_t (*nowMonoMs)(void* context);
  bool (*isConnected)(void* context);
  bool (*connect)(void* context, const char* clientId);
  void (*disconnect)(void* context);
  bool (*subscribe)(void* context, const char* topic,
                    NetworkMessageCallback callback, void* callbackContext);
  bool (*readUtcTime)(void* context, bool& synchronized, int64_t& utcEpoch);
  void (*idle)(void* context, uint32_t delayMs);
  NetworkCommandAdapter commands;
};

NetworkScheduleDecision networkScheduleStep(const NetworkScheduleInput& input);
bool networkEnqueueCommand(NetworkCommandType type, const uint8_t* payload, uint16_t length);
bool networkProcessOneCommand(const NetworkCommandAdapter& adapter);
bool networkRequestMaintenance(uint32_t token);
bool networkReleaseMaintenance(uint32_t token);
bool networkIsReconnectInhibited();
void networkHandleIncomingMessage(void* context, const char* topic,
                                  const uint8_t* payload, std::size_t length);
void networkTaskStep(NetworkTaskAdapter& adapter);
void TaskNetwork(void* parameter);
