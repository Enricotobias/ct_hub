#pragma once

#include <cstddef>
#include <cstdint>

constexpr std::size_t CT_CHANNEL_COUNT = 16;
constexpr std::size_t CT_CHANNELS_PER_BANK = 8;
constexpr std::size_t CT_CHANNEL_LABEL_SIZE = 8;
constexpr std::size_t CT_EVENT_DETAIL_SIZE = 48;

enum class AdsPair : uint8_t {
  A0_A1,
  A2_A3,
};

enum class CtQuality : uint8_t {
  Ok,
  Degraded,
  SensorError,
  Stale,
  Disabled,
  Paused,
};

enum class RuntimeEventType : uint8_t {
  None,
  SensorRecovered,
  SensorFault,
  NetworkConnected,
  NetworkDisconnected,
  SensorPaused,
  SensorResumed,
  NetworkMaintenanceGranted,
  NetworkMaintenanceReleased,
  OtaProgress,
  OtaFinished,
};

struct CtChannelConfig {
  uint8_t channelNumber;
  bool enabled;
  char label[CT_CHANNEL_LABEL_SIZE];
  uint8_t muxChannel;
  AdsPair adsPair;
  float calibrationGain;
};

struct CtAccumulator {
  uint32_t count;
  uint32_t readErrors;
  double meanMv;
  double m2;
  float minMv;
  float maxMv;
  uint32_t nearZeroSamples;
  uint32_t nearHighSamples;
  uint32_t nearFullScaleSamples;
};

struct CtSampleResult {
  float rmsMv;
  float currentA;
  float meanMv;
  float minMv;
  float maxMv;
  uint32_t sampleCount;
  uint32_t readErrors;
  uint32_t clippingCount;
  CtQuality quality;
  bool valid;
};

struct CtReading {
  uint8_t channelNumber;
  float currentA;
  float rmsMv;
  CtQuality quality;
  uint64_t monotonicMs;
  uint32_t sequence;
  uint32_t sampleCount;
  uint32_t readErrors;
  uint32_t clippingCount;
  bool valid;
};

struct CtSnapshot {
  CtReading readings[CT_CHANNEL_COUNT];
  uint32_t scanSequence;
  uint64_t capturedMonotonicMs;
  uint32_t durationMs;
  bool measurementPaused;
};

struct RuntimeEvent {
  RuntimeEventType type;
  uint8_t channelNumber;
  uint64_t monotonicMs;
  uint32_t token;
  uint8_t progressPercent;
  char requestId[40];
  char command[20];
  char status[16];
  char detail[CT_EVENT_DETAIL_SIZE];
};

struct RuntimeStats {
  uint64_t sensorHeartbeatMs;
  uint64_t networkHeartbeatMs;
  uint64_t healthHeartbeatMs;
  uint32_t reconnectAttempts;
  uint32_t publishFailures;
  uint32_t scanProgress;
  uint32_t freeHeapBytes;
  uint32_t sensorStackHighWaterBytes;
  uint32_t networkStackHighWaterBytes;
  uint64_t otaHeartbeatMs;
  uint8_t otaPhase;
  uint32_t otaProgressBytes;
};
