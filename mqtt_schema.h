#pragma once

#include "ct_types.h"

#include <cstdint>
#include <cstddef>

#include <ArduinoJson.h>

constexpr uint8_t MQTT_SCHEMA_VERSION = 1;

enum class DeviceMode : uint8_t { Booting, Online, Degraded, Paused, Offline };

struct MqttTopics {
  char data[96];
  char status[96];
  char info[96];
  char ack[96];
  char command[96];
  char update[96];
};

struct PublishContext {
  const char* deviceId;
  const char* bootId;
  uint32_t sequence;
  uint64_t monotonicMs;
  int64_t utcEpoch;
  bool timeSynced;
};

struct RuntimeSnapshot {
  RuntimeStats stats;
  uint32_t bootCount;
  uint32_t crashCount;
  uint32_t calibrationRevision;
  uint32_t sensorSequence;
  bool networkConnected;
};

struct AckEvent {
  const char* requestId;
  const char* command;
  const char* status;
  const char* detail;
};

void commandRecordFinalResult(const char* requestId, const char* command,
                              const char* status, const char* detail);

enum class CommandKind : uint8_t { Info = 0, Calibration = 1, Reboot = 2, OtaUpdate = 3, Config = 4 };
enum class ConfigOperation : uint8_t { None, ActiveChannels, CalibrationGain };
struct ParsedCommand {
  CommandKind kind;
  ConfigOperation configOperation;
  char requestId[40];
  uint8_t channel;
  uint8_t activeChannels;
  uint8_t delaySeconds;
  float gain;
};
struct CommandParseResult { bool accepted; ParsedCommand command; const char* detail; };
struct CommandOutcome { const char* status; const char* detail; };

CommandParseResult commandParse(CommandKind kind, const uint8_t* payload, size_t length);
CommandOutcome commandHandle(const ParsedCommand& command);
bool commandLookupFinalResult(const char* requestId, AckEvent& output);

bool mqttBuildTopics(const char* root, const char* deviceId, MqttTopics& output);
bool mqttBuildDataDocument(const CtSnapshot& snapshot, const PublishContext& context, JsonDocument& document);
bool mqttBuildInfoDocument(const RuntimeSnapshot& snapshot, JsonDocument& document);
bool mqttBuildStatusDocument(DeviceMode mode, JsonDocument& document);
bool mqttBuildAckDocument(const AckEvent& event, JsonDocument& document);
