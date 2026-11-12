#include "task_network.h"
#include "mqtt_service.h"
#include "ota_store.h"
#include "debug_log.h"

#include <cstdio>
#include <cstring>

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ctime>

NetworkScheduleDecision networkScheduleStep(const NetworkScheduleInput& input) {
  NetworkScheduleDecision decision{};
  decision.publishFailureCount = input.publishFailures;
  if (!input.connected) return decision;

  decision.publishStatus = input.lastDataPublishMs == 0 && input.lastInfoPublishMs == 0;
  decision.publishInfo = decision.publishStatus ||
                        input.nowMonoMs - input.lastInfoPublishMs >= NETWORK_INFO_INTERVAL_MS;
  const bool dataDue = input.lastDataPublishMs == 0 ||
                       input.nowMonoMs - input.lastDataPublishMs >= NETWORK_DATA_INTERVAL_MS;
  const bool isFreshAfterReconnect = input.currentScanSequence > input.lastPublishedScanSequence &&
                                     input.currentScanSequence > 0;
  decision.waitForFreshScan = input.reconnectStartedMs != 0 && !isFreshAfterReconnect;
  decision.publishData = dataDue && !decision.waitForFreshScan;
  return decision;
}

bool networkEnqueueCommand(NetworkCommandType type, const uint8_t* payload, uint16_t length) {
  if (payload == nullptr || length == 0 || length > RUNTIME_COMMAND_PAYLOAD_SIZE) return false;
  RuntimeCommand command{};
  command.type = static_cast<uint8_t>(type);
  command.length = length;
  std::memcpy(command.payload, payload, length);
  return runtimePostCommand(command);
}

bool networkRequestMaintenance(uint32_t token) {
  return runtimeRequestNetworkMaintenance(token);
}

bool networkReleaseMaintenance(uint32_t token) {
  return runtimeReleaseNetworkMaintenance(token);
}

bool networkIsReconnectInhibited() {
  NetworkMaintenanceState state{};
  runtimeCopyNetworkMaintenance(state);
  return state.requested;
}

void networkHandleIncomingMessage(void* context, const char* topic,
                                  const uint8_t* payload, std::size_t length) {
  NetworkTaskAdapter* adapter = static_cast<NetworkTaskAdapter*>(context);
  const char* updateTopic = mqttServiceUpdateTopic();
  const char* commandTopic = mqttServiceCommandTopic();
  if (adapter == nullptr || topic == nullptr || payload == nullptr) return;
  ctDebugLog("INFO", "NETWORK", "message received; topic=%s bytes=%u",
             topic, static_cast<unsigned>(length));
  if (commandTopic != nullptr && std::strcmp(topic, commandTopic) == 0) {
    if (length == 0 || length > RUNTIME_COMMAND_PAYLOAD_SIZE) return;
    JsonDocument document;
    if (deserializeJson(document, payload, length)) return;
    JsonObjectConst commandObject = document.as<JsonObjectConst>();
    const char* commandName = commandObject["command"] | "";
    NetworkCommandType type;
    if (std::strcmp(commandName, "info") == 0 ||
        (commandName[0] == '\0' && commandObject["active_channels"].isNull() &&
         commandObject["channel"].isNull() &&
         commandObject["delay_seconds"].isNull() &&
         commandObject["expires_at"].isNull())) {
      type = NetworkCommandType::Info;
    } else if (std::strcmp(commandName, "config") == 0 ||
               (commandName[0] == '\0' && !commandObject["active_channels"].isNull())) {
      type = NetworkCommandType::Config;
    } else if (std::strcmp(commandName, "calibration") == 0 ||
               (commandName[0] == '\0' && !commandObject["channel"].isNull() &&
                !commandObject["gain"].isNull())) {
      type = NetworkCommandType::Calibration;
    } else if (std::strcmp(commandName, "reboot") == 0 ||
               (commandName[0] == '\0' &&
                (!commandObject["delay_seconds"].isNull() ||
                 !commandObject["expires_at"].isNull()))) {
      type = NetworkCommandType::Reboot;
    } else {
      return;
    }
    if (!networkEnqueueCommand(type, payload, static_cast<uint16_t>(length)) &&
        adapter->commands.publishAck != nullptr) {
      const char* requestId = commandObject["request_id"].is<const char*>()
                                  ? commandObject["request_id"].as<const char*>()
                                  : nullptr;
      const char* ackCommand = type == NetworkCommandType::Info ? "info"
                               : type == NetworkCommandType::Calibration ? "calibration"
                               : type == NetworkCommandType::Config ? "config"
                                                                    : "reboot";
      const AckEvent ack{requestId, ackCommand, "busy", "COMMAND_QUEUE_FULL"};
      (void)adapter->commands.publishAck(adapter->commands.context, ack);
    }
    return;
  }
  if (updateTopic == nullptr || std::strcmp(topic, updateTopic) != 0) return;

  const OtaParseResult parsed = otaParseRequest(payload, length);
  AckEvent ack{parsed.request.requestId[0] == '\0' ? nullptr : parsed.request.requestId,
               "update", parsed.accepted ? "accepted" : "failed", parsed.detail};
  OtaFinalResult cached{};
  const OtaDeduplication dedup = parsed.accepted
      ? otaCheckRequest(parsed.request, cached) : OtaDeduplication::NewRequest;
  if (dedup == OtaDeduplication::RequestIdConflict) {
    ack.status = "failed";
    ack.detail = "REQUEST_ID_CONFLICT";
  } else if (dedup == OtaDeduplication::CompletedDuplicate) {
    ack.status = cached.status;
    ack.detail = cached.detail;
  } else if (dedup == OtaDeduplication::ActiveDuplicate) {
    ack.status = "accepted";
    ack.detail = "OTA_IN_PROGRESS";
  } else if (parsed.accepted && !runtimePostOtaRequest(parsed.request)) {
    ack.status = "busy";
    ack.detail = "COMMAND_QUEUE_FULL";
  } else if (parsed.accepted) {
    ack.detail = "OTA_QUEUED";
  }
  if (adapter->commands.publishAck != nullptr) {
    (void)adapter->commands.publishAck(adapter->commands.context, ack);
  }
}

bool networkProcessOneCommand(const NetworkCommandAdapter& adapter) {
  RuntimeCommand queued{};
  if (!runtimeTakeCommand(queued)) return false;
  const CommandKind kind = static_cast<CommandKind>(queued.type);
  const CommandParseResult parsed = commandParse(
      kind, reinterpret_cast<const uint8_t*>(queued.payload), queued.length);
  ctDebugLog(parsed.accepted ? "INFO" : "WARN", "NETWORK",
             "command parsed; type=%u accepted=%u detail=%s",
             static_cast<unsigned>(kind), parsed.accepted ? 1U : 0U, parsed.detail);
  AckEvent ack{parsed.command.requestId[0] == '\0' ? nullptr : parsed.command.requestId,
               kind == CommandKind::Info ? "info"
               : kind == CommandKind::Calibration ? "calibration"
               : kind == CommandKind::Config ? "config"
                                             : "reboot",
               "failed", parsed.detail};
  if (parsed.accepted) {
    const CommandOutcome outcome = commandHandle(parsed.command);
    ack.status = outcome.status;
    ack.detail = outcome.detail;
    if (outcome.status[0] == 'a') {
      bool applied = true;
      if (kind == CommandKind::Info) {
        applied = adapter.publishInfo != nullptr && adapter.publishInfo(adapter.context);
      } else if (kind == CommandKind::Calibration && adapter.applyCalibration != nullptr) {
        applied = adapter.applyCalibration(adapter.context, parsed.command.channel, parsed.command.gain);
      } else if (kind == CommandKind::Config) {
        if (parsed.command.configOperation == ConfigOperation::ActiveChannels) {
          applied = adapter.applyActiveChannels != nullptr &&
                    adapter.applyActiveChannels(adapter.context, parsed.command.activeChannels);
        } else if (parsed.command.configOperation == ConfigOperation::CalibrationGain) {
          applied = adapter.applyCalibration != nullptr &&
                    adapter.applyCalibration(adapter.context, parsed.command.channel,
                                             parsed.command.gain);
        } else {
          applied = false;
        }
      } else if (kind == CommandKind::Reboot && adapter.scheduleReboot != nullptr) {
        applied = adapter.scheduleReboot(adapter.context, parsed.command.delaySeconds,
                                         parsed.command.requestId);
      }
      ack.status = applied ? "completed" : "failed";
      ack.detail = applied
                       ? (kind == CommandKind::Info ? "INFO_PUBLISHED"
                          : kind == CommandKind::Config &&
                                    parsed.command.configOperation == ConfigOperation::ActiveChannels
                                ? "ACTIVE_CHANNELS_UPDATED"
                                : kind == CommandKind::Config ? "CALIBRATION_UPDATED" : "APPLIED")
                       : "APPLY_FAILED";
      commandRecordFinalResult(parsed.command.requestId, ack.command, ack.status, ack.detail);
    }
  }
  if (adapter.publishAck != nullptr) (void)adapter.publishAck(adapter.context, ack);
  return true;
}

void TaskNetwork(void* parameter) {
  NetworkTaskAdapter* adapter = static_cast<NetworkTaskAdapter*>(parameter);
  constexpr uint32_t kReconnectDelaysMs[] = {1000, 2000, 5000, 10000, 30000};
  uint8_t reconnectAttempt = 0;
  uint64_t nextConnectMs = 0;
  uint64_t lastDataPublishMs = 0;
  uint64_t lastInfoPublishMs = 0;
  uint64_t reconnectStartedMs = 0;
  uint64_t freshScanDeadlineMs = 0;
  uint32_t reconnectScanSequence = 0;
  uint32_t lastPublishedScanSequence = 0;
  uint32_t publishFailures = 0;
  bool wasConnected = false;
  bool updateTopicSubscribed = false;
  bool commandTopicSubscribed = false;
  uint32_t connectAttempts = 0;

  ctDebugLog("INFO", "NETWORK", "TaskNetwork started; mqtt_retry_initial_ms=%lu",
             static_cast<unsigned long>(kReconnectDelaysMs[0]));

  for (;;) {
    const uint64_t nowMonoMs = adapter != nullptr && adapter->nowMonoMs != nullptr
                                   ? adapter->nowMonoMs(adapter->context)
                                   : millis();
    mqttServiceLoop();
    NetworkMaintenanceState maintenance{};
    runtimeCopyNetworkMaintenance(maintenance);
    if (maintenance.requested) {
      if (!maintenance.granted) {
        bool disconnected = adapter == nullptr || adapter->isConnected == nullptr ||
                            !adapter->isConnected(adapter->context);
        if (!disconnected && adapter->disconnect != nullptr) {
          adapter->disconnect(adapter->context);
          disconnected = adapter->isConnected == nullptr || !adapter->isConnected(adapter->context);
        }
        if (disconnected) {
          (void)runtimeAcknowledgeNetworkMaintenance(maintenance.token, true, nowMonoMs);
          wasConnected = false;
          updateTopicSubscribed = false;
          commandTopicSubscribed = false;
        }
      }
      runtimeMarkHeartbeat(TaskKind::Network, nowMonoMs);
      if (adapter != nullptr && adapter->idle != nullptr) adapter->idle(adapter->context, 10);
      else delay(10);
      continue;
    }
    bool connected = wasConnected;
    if (adapter != nullptr && adapter->isConnected != nullptr) {
      connected = adapter->isConnected(adapter->context);
    }
    if (!connected && nowMonoMs >= nextConnectMs) {
      ++connectAttempts;
      ctDebugLog("INFO", "NETWORK", "connection attempt=%lu device_id=%s",
                 static_cast<unsigned long>(connectAttempts), mqttServiceDeviceId());
      const bool connectedByAttempt = adapter != nullptr && adapter->connect != nullptr
                                          ? adapter->connect(adapter->context, mqttServiceDeviceId())
                                          : mqttServiceConnect();
      mqttServiceLoop();
      connected = connectedByAttempt;
      if (adapter != nullptr && adapter->isConnected != nullptr) {
        connected = adapter->isConnected(adapter->context);
      }
      if (!connected) {
        const uint8_t delayIndex = reconnectAttempt < 5 ? reconnectAttempt : 4;
        nextConnectMs = nowMonoMs + kReconnectDelaysMs[delayIndex];
        ctDebugLog("WARN", "NETWORK", "connection attempt failed; retry_ms=%lu",
                   static_cast<unsigned long>(kReconnectDelaysMs[delayIndex]));
        if (reconnectAttempt < 5) ++reconnectAttempt;
      } else {
        ctDebugLog("INFO", "NETWORK", "connection established");
        updateTopicSubscribed = false;
        commandTopicSubscribed = false;
      }
    }

    if (adapter != nullptr) (void)networkProcessOneCommand(adapter->commands);

    PendingReboot pendingReboot{};
    const bool rebootDeadlineReached = runtimeGetPendingReboot(pendingReboot) &&
        static_cast<uint32_t>(static_cast<uint32_t>(nowMonoMs) -
                              static_cast<uint32_t>(pendingReboot.deadlineMonoMs)) < 0x80000000U;
    if (pendingReboot.pending && rebootDeadlineReached) {
      (void)mqttPublishStatus(DeviceMode::Offline);
      mqttServiceLoop();
      if (adapter != nullptr && adapter->disconnect != nullptr) {
        adapter->disconnect(adapter->context);
      }
      runtimeClearPendingReboot();
      delay(20);
      ESP.restart();
    }

    if (adapter != nullptr && adapter->isConnected != nullptr) {
      connected = adapter->isConnected(adapter->context);
    }
    if (!connected && wasConnected) {
      ctDebugLog("WARN", "NETWORK", "connection lost");
      lastDataPublishMs = 0;
      lastInfoPublishMs = 0;
      reconnectStartedMs = 0;
      wasConnected = false;
    } else if (connected && !wasConnected) {
      ctDebugLog("INFO", "NETWORK", "network session active; waiting for fresh CT scan");
      reconnectAttempt = 0;
      reconnectStartedMs = nowMonoMs;
      freshScanDeadlineMs = nowMonoMs + CT_READING_STALE_AFTER_MS;
      CtSnapshot baseline{};
      if (ctCopySnapshot(baseline, nowMonoMs)) {
        reconnectScanSequence = baseline.scanSequence;
        lastPublishedScanSequence = baseline.scanSequence;
      }
      wasConnected = true;
    }
    if (connected && !updateTopicSubscribed && adapter != nullptr && adapter->subscribe != nullptr) {
      const char* updateTopic = mqttServiceUpdateTopic();
      updateTopicSubscribed = updateTopic != nullptr &&
                              adapter->subscribe(adapter->context, updateTopic,
                                                 networkHandleIncomingMessage, adapter);
    }
    if (connected && !commandTopicSubscribed && adapter != nullptr && adapter->subscribe != nullptr) {
      const char* commandTopic = mqttServiceCommandTopic();
      commandTopicSubscribed = commandTopic != nullptr &&
                               adapter->subscribe(adapter->context, commandTopic,
                                                  networkHandleIncomingMessage, adapter);
    }
    if (connected) {
      RuntimeEvent otaResult{};
      if (adapter != nullptr && adapter->commands.publishAck != nullptr) {
        RuntimeEvent otaProgress{};
        if (runtimeTakeOtaProgress(otaProgress)) {
          const AckEvent progressAck{otaProgress.requestId[0] == '\0' ? nullptr : otaProgress.requestId,
                                     otaProgress.command,
                                     otaProgress.status, otaProgress.detail};
          if (!adapter->commands.publishAck(adapter->commands.context, progressAck)) {
            (void)runtimePostOtaProgress(otaProgress);
          }
        }
        while (runtimeTakeOtaResult(otaResult)) {
          const AckEvent ack{otaResult.requestId[0] == '\0' ? nullptr : otaResult.requestId,
                             otaResult.command, otaResult.status, otaResult.detail};
          if (!adapter->commands.publishAck(adapter->commands.context, ack)) {
            (void)runtimePostOtaResult(otaResult);
            break;
          }
          otaAcknowledgeFinalPublished();
        }
      }
      CtSnapshot snapshot{};
      if (ctCopySnapshot(snapshot, nowMonoMs)) {
        if (reconnectStartedMs != 0 && snapshot.scanSequence <= reconnectScanSequence &&
            nowMonoMs >= freshScanDeadlineMs) {
          reconnectStartedMs = 0;
        }

        const NetworkScheduleInput input{
            nowMonoMs,
            lastDataPublishMs,
            lastInfoPublishMs,
            reconnectStartedMs,
            snapshot.scanSequence,
            lastPublishedScanSequence,
            publishFailures,
            true};
        const NetworkScheduleDecision decision = networkScheduleStep(input);
        if (decision.publishStatus) {
          if (!mqttPublishStatus(DeviceMode::Online)) ++publishFailures;
        }
        if (decision.publishInfo) {
          if (mqttPublishInfo()) lastInfoPublishMs = nowMonoMs;
          else { lastInfoPublishMs = nowMonoMs; ++publishFailures; }
        }
        if (decision.publishData) {
          bool timeSynced = false;
          int64_t utcEpoch = 0;
          if (adapter != nullptr && adapter->readUtcTime != nullptr) {
            (void)adapter->readUtcTime(adapter->context, timeSynced, utcEpoch);
          }
          mqttServiceSetPublishTime(timeSynced, utcEpoch);
          if (mqttPublishData(snapshot)) {
            lastPublishedScanSequence = snapshot.scanSequence;
          } else {
            ++publishFailures;
          }
          lastDataPublishMs = nowMonoMs;
        }
      }
    }

    runtimeMarkHeartbeat(TaskKind::Network, nowMonoMs);
    if (adapter != nullptr && adapter->idle != nullptr) adapter->idle(adapter->context, 10);
    else delay(10);
  }
}
