#include "mqtt_schema.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>

namespace {
constexpr uint8_t kFinalAckCacheSize = 8;
struct FinalAckRecord {
  bool used;
  AckEvent event;
  char requestId[40];
  char command[20];
  char status[20];
  char detail[64];
};
FinalAckRecord g_finalAckCache[kFinalAckCacheSize]{};

void copyText(char* output, std::size_t capacity, const char* input) {
  if (capacity == 0) return;
  if (input == nullptr) input = "";
  std::snprintf(output, capacity, "%s", input);
}

const char* findJsonValue(const char* text, const char* key) {
  if (text == nullptr || key == nullptr) return nullptr;
  char needle[32]{};
  std::snprintf(needle, sizeof(needle), "\"%s\"", key);
  const char* cursor = std::strstr(text, needle);
  if (cursor == nullptr) return nullptr;
  cursor = std::strchr(cursor + std::strlen(needle), ':');
  if (cursor == nullptr) return nullptr;
  ++cursor;
  while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' || *cursor == '\n') ++cursor;
  return cursor;
}

bool readJsonString(const char* text, const char* key, char* output, std::size_t capacity) {
  const char* value = findJsonValue(text, key);
  if (value == nullptr || *value != '"') return false;
  ++value;
  const char* end = std::strchr(value, '"');
  if (end == nullptr || end == value || static_cast<std::size_t>(end - value) >= capacity) return false;
  std::memcpy(output, value, static_cast<std::size_t>(end - value));
  output[end - value] = '\0';
  return true;
}

bool readJsonUnsigned(const char* text, const char* key, uint64_t& output) {
  const char* value = findJsonValue(text, key);
  if (value == nullptr || *value < '0' || *value > '9') return false;
  char* end = nullptr;
  output = std::strtoull(value, &end, 10);
  return end != value;
}

bool readJsonInteger(const char* text, const char* key, long& output) {
  const char* value = findJsonValue(text, key);
  if (value == nullptr) return false;
  char* end = nullptr;
  output = std::strtol(value, &end, 10);
  if (end == value) return false;
  while (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n') ++end;
  return *end == '\0' || *end == ',' || *end == '}' || *end == ']';
}

bool hasFinalResult(const char* requestId) {
  if (requestId == nullptr || *requestId == '\0') return false;
  for (const FinalAckRecord& record : g_finalAckCache) {
    if (record.used && std::strcmp(record.requestId, requestId) == 0) return true;
  }
  return false;
}

bool copyTopic(char* output, std::size_t capacity, const char* root, const char* deviceId,
               const char* leaf) {
  if (output == nullptr || root == nullptr || deviceId == nullptr || leaf == nullptr) return false;
  const int written = std::snprintf(output, capacity, "%s/%s/%s", root, deviceId, leaf);
  return written > 0 && static_cast<std::size_t>(written) < capacity;
}

const char* qualityName(CtQuality quality) {
  switch (quality) {
    case CtQuality::Ok: return "ok";
    case CtQuality::Degraded: return "degraded";
    case CtQuality::SensorError: return "sensor_error";
    case CtQuality::Stale: return "stale";
    case CtQuality::Disabled: return "disabled";
    case CtQuality::Paused: return "paused";
  }
  return "unknown";
}

const char* modeName(DeviceMode mode) {
  switch (mode) {
    case DeviceMode::Booting: return "booting";
    case DeviceMode::Online: return "online";
    case DeviceMode::Degraded: return "degraded";
    case DeviceMode::Paused: return "paused";
    case DeviceMode::Offline: return "offline";
  }
  return "offline";
}
}

bool mqttBuildTopics(const char* root, const char* deviceId, MqttTopics& output) {
  output = {};
  if (!copyTopic(output.data, sizeof(output.data), root, deviceId, "data") ||
      !copyTopic(output.status, sizeof(output.status), root, deviceId, "status") ||
      !copyTopic(output.info, sizeof(output.info), root, deviceId, "info") ||
      !copyTopic(output.ack, sizeof(output.ack), root, deviceId, "ack") ||
      !copyTopic(output.command, sizeof(output.command), root, deviceId, "command")) return false;
  const int updateLength = std::snprintf(output.update, sizeof(output.update),
                                         "%s/command/%s/update", root, deviceId);
  return updateLength > 0 && static_cast<std::size_t>(updateLength) < sizeof(output.update);
}

bool mqttBuildDataDocument(const CtSnapshot& snapshot, const PublishContext& context, JsonDocument& document) {
  JsonObject root = document.to<JsonObject>();
  root["schema_version"] = MQTT_SCHEMA_VERSION;
  root["device_id"] = context.deviceId == nullptr ? "" : context.deviceId;
  root["boot_id"] = context.bootId == nullptr ? "" : context.bootId;
  root["sequence"] = context.sequence;
  root["monotonic_ms"] = context.monotonicMs;
  if (context.timeSynced) root["timestamp"] = context.utcEpoch;
  else root["timestamp"].set(nullptr);
  root["scan_duration_ms"] = snapshot.durationMs;
  root["paused"] = snapshot.measurementPaused;
  JsonObject channels = root["channels"].to<JsonObject>();
  char key[8]{};
  for (uint8_t index = 0; index < CT_CHANNEL_COUNT; ++index) {
    std::snprintf(key, sizeof(key), "ct%02u", static_cast<unsigned>(index + 1U));
    JsonObject channel = channels[key].to<JsonObject>();
    const CtReading& reading = snapshot.readings[index];
    if (reading.valid) channel["current_a"] = reading.currentA;
    else channel["current_a"].set(nullptr);
    channel["valid"] = reading.valid;
    channel["quality"] = qualityName(reading.quality);
    if (reading.valid && context.monotonicMs >= reading.monotonicMs) {
      channel["age_ms"] = context.monotonicMs - reading.monotonicMs;
    } else {
      channel["age_ms"].set(nullptr);
    }
  }
  return true;
}

bool mqttBuildInfoDocument(const RuntimeSnapshot& snapshot, JsonDocument& document) {
  JsonObject root = document.to<JsonObject>();
  root["schema_version"] = MQTT_SCHEMA_VERSION;
  root["network_connected"] = snapshot.networkConnected;
  root["sensor_sequence"] = snapshot.sensorSequence;
  root["calibration_revision"] = snapshot.calibrationRevision;
  root["boot_count"] = snapshot.bootCount;
  root["crash_count"] = snapshot.crashCount;
  root["memory"]["free_heap"] = snapshot.stats.freeHeapBytes;
  root["tasks"]["sensor_stack_high_water"] = snapshot.stats.sensorStackHighWaterBytes;
  root["tasks"]["network_stack_high_water"] = snapshot.stats.networkStackHighWaterBytes;
  root["ota"]["phase"] = snapshot.stats.otaPhase;
  root["ota"]["progress_bytes"] = snapshot.stats.otaProgressBytes;
  return true;
}

bool mqttBuildStatusDocument(DeviceMode mode, JsonDocument& document) {
  JsonObject root = document.to<JsonObject>();
  root["status"] = modeName(mode);
  root["reason"] = mode == DeviceMode::Offline ? "CONNECTION_LOST" : "STATE_CHANGE";
  return true;
}

bool mqttBuildAckDocument(const AckEvent& event, JsonDocument& document) {
  JsonObject root = document.to<JsonObject>();
  root["request_id"] = event.requestId == nullptr ? nullptr : event.requestId;
  root["command"] = event.command == nullptr ? "" : event.command;
  root["status"] = event.status == nullptr ? "failed" : event.status;
  root["detail"] = event.detail == nullptr ? "" : event.detail;
  return true;
}

CommandParseResult commandParse(CommandKind kind, const uint8_t* payload, size_t length) {
  CommandParseResult result{};
  result.command.kind = kind;
  result.detail = "MALFORMED_JSON";
  if (payload == nullptr || length == 0 || length > 2048) { result.detail = "PAYLOAD_TOO_LARGE"; return result; }
  char text[2049]{};
  std::memcpy(text, payload, length);
  if (!readJsonString(text, "request_id", result.command.requestId, sizeof(result.command.requestId))) {
    result.detail = "REQUEST_ID_REQUIRED";
    return result;
  }
  if (kind == CommandKind::Calibration || kind == CommandKind::Config) {
    if (kind == CommandKind::Config) {
      char commandName[20]{};
      if (!readJsonString(text, "command", commandName, sizeof(commandName)) ||
          std::strcmp(commandName, "config") != 0) {
        result.detail = "CONFIG_COMMAND_REQUIRED";
        return result;
      }
    }
    uint64_t channel = 0;
    const char* gain = findJsonValue(text, "gain");
    const char* activeChannels = findJsonValue(text, "active_channels");
    const char* channelValue = findJsonValue(text, "channel");
    if (kind == CommandKind::Config) {
      if (activeChannels != nullptr) {
        uint64_t count = 0;
        if (channelValue != nullptr || gain != nullptr ||
            !readJsonUnsigned(text, "active_channels", count) || count < 1 || count > 16) {
          result.detail = "INVALID_ACTIVE_CHANNEL_COUNT";
          return result;
        }
        result.command.configOperation = ConfigOperation::ActiveChannels;
        result.command.activeChannels = static_cast<uint8_t>(count);
      } else {
        if (channelValue == nullptr || gain == nullptr ||
            !readJsonUnsigned(text, "channel", channel) || channel < 1 || channel > 16) {
          result.detail = "CONFIG_FIELDS_REQUIRED";
          return result;
        }
        char* gainEnd = nullptr;
        result.command.gain = std::strtof(gain, &gainEnd);
        if (gainEnd == gain || !std::isfinite(result.command.gain) ||
            result.command.gain < 0.1f || result.command.gain > 10.0f) {
          result.detail = "INVALID_CALIBRATION";
          return result;
        }
        result.command.configOperation = ConfigOperation::CalibrationGain;
        result.command.channel = static_cast<uint8_t>(channel);
      }
    } else {
      if (!readJsonUnsigned(text, "channel", channel) || gain == nullptr ||
          channel < 1 || channel > 16) {
        result.detail = "CALIBRATION_FIELDS_REQUIRED";
        return result;
      }
      char* gainEnd = nullptr;
      result.command.channel = static_cast<uint8_t>(channel);
      result.command.gain = std::strtof(gain, &gainEnd);
      if (gainEnd == gain || !std::isfinite(result.command.gain) ||
          result.command.gain < 0.1f || result.command.gain > 10.0f) {
        result.detail = "INVALID_CALIBRATION";
        return result;
      }
    }
  }
  if (kind == CommandKind::Reboot) {
    if (findJsonValue(text, "expires_at") != nullptr) {
      result.detail = "EXPIRES_AT_NOT_SUPPORTED_FOR_REBOOT";
      return result;
    }
    result.command.delaySeconds = 3;
    if (findJsonValue(text, "delay_seconds") != nullptr) {
      long delaySeconds = 0;
      if (!readJsonInteger(text, "delay_seconds", delaySeconds)) {
        result.detail = "INVALID_DELAY_SECONDS";
        return result;
      }
      if (delaySeconds < 1) delaySeconds = 1;
      if (delaySeconds > 60) delaySeconds = 60;
      result.command.delaySeconds = static_cast<uint8_t>(delaySeconds);
    }
  }
  result.accepted = true;
  result.detail = "ACCEPTED";
  return result;
}

CommandOutcome commandHandle(const ParsedCommand& command) {
  if (hasFinalResult(command.requestId)) return {"completed", "FINAL_RESULT_AVAILABLE"};
  return {"accepted", "QUEUED"};
}

bool commandLookupFinalResult(const char* requestId, AckEvent& output) {
  output = {nullptr, nullptr, nullptr, nullptr};
  if (requestId == nullptr) return false;
  for (const FinalAckRecord& record : g_finalAckCache) {
    if (record.used && std::strcmp(record.requestId, requestId) == 0) {
      output = record.event;
      return true;
    }
  }
  return false;
}

void commandRecordFinalResult(const char* requestId, const char* command,
                              const char* status, const char* detail) {
  if (requestId == nullptr || *requestId == '\0') return;
  FinalAckRecord* target = nullptr;
  for (FinalAckRecord& record : g_finalAckCache) {
    if (record.used && std::strcmp(record.requestId, requestId) == 0) { target = &record; break; }
    if (target == nullptr && !record.used) target = &record;
  }
  if (target == nullptr) target = &g_finalAckCache[0];
  target->used = true;
  copyText(target->requestId, sizeof(target->requestId), requestId);
  copyText(target->command, sizeof(target->command), command);
  copyText(target->status, sizeof(target->status), status);
  copyText(target->detail, sizeof(target->detail), detail);
  target->event = {target->requestId, target->command, target->status, target->detail};
}
