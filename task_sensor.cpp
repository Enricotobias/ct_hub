#include "task_sensor.h"

#include "config.h"
#include "ct_math.h"
#include "ct_store.h"
#include "debug_log.h"
#include "runtime_state.h"

#include <Arduino.h>

namespace {

SensorScanAdapter g_adapter{};
uint32_t g_scanSequence = 0;
constexpr uint32_t kSensorRetryLogIntervalMs = 5000;
constexpr uint32_t kSensorSummaryIntervalMs = 10000;

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

uint64_t sensorNowMs() {
  if (g_adapter.nowMillis != nullptr) return g_adapter.nowMillis(g_adapter.context);
  return millis();
}

void setInvalidReading(uint8_t channelIndex, CtReading& reading, uint32_t errors) {
  reading = {};
  reading.channelNumber = static_cast<uint8_t>(channelIndex + 1U);
  reading.quality = CtQuality::SensorError;
  reading.readErrors = errors;
  reading.valid = false;
}

bool measureChannel(uint8_t channelIndex, const CtChannelConfig& channel,
                    CtReading& reading) {
  setInvalidReading(channelIndex, reading, 0);
  reading.monotonicMs = sensorNowMs();
  reading.sequence = g_scanSequence + 1U;
  if (channelIndex >= CT_CHANNEL_COUNT) return false;
  if (!channel.enabled) {
    reading.quality = CtQuality::Disabled;
    return false;
  }
  if (g_adapter.readSampleMv == nullptr) return false;

  CtAccumulator accumulator{};
  ctResetAccumulator(accumulator);
  for (uint16_t discard = 0; discard < CT_DISCARD_SAMPLES; ++discard) {
    float ignored = 0.0f;
    if (!g_adapter.readSampleMv(g_adapter.context, channelIndex, channel.adsPair, ignored)) {
      setInvalidReading(channelIndex, reading, accumulator.readErrors + 1U);
      return false;
    }
  }
  for (uint32_t sample = 0; sample < CT_MIN_VALID_SAMPLES; ++sample) {
    float millivolts = 0.0f;
    if (!g_adapter.readSampleMv(g_adapter.context, channelIndex, channel.adsPair, millivolts)) {
      setInvalidReading(channelIndex, reading, accumulator.readErrors + 1U);
      return false;
    }
    if (!ctAddSample(accumulator, millivolts)) ++accumulator.readErrors;
  }

  const CtSampleResult result = ctFinishWindow(accumulator, channel.calibrationGain);
  reading.channelNumber = channel.channelNumber;
  reading.currentA = result.currentA;
  reading.rmsMv = result.rmsMv;
  reading.quality = result.quality;
  reading.sampleCount = result.sampleCount;
  reading.readErrors = result.readErrors;
  reading.clippingCount = result.clippingCount;
  reading.valid = result.valid;
  reading.monotonicMs = sensorNowMs();
  reading.sequence = g_scanSequence + 1U;
  return result.valid;
}

}  // namespace

void ctConfigureSensorScan(const SensorScanAdapter& adapter) {
  g_adapter = adapter;
}

bool ctMeasureChannel(uint8_t channelIndex, CtReading& reading) {
  return measureChannel(channelIndex, ctGetChannelConfig(channelIndex), reading);
}

bool ctScanOnce(CtSnapshot& snapshot) {
  snapshot = {};
  if (g_adapter.selectMux == nullptr || g_adapter.delayUs == nullptr ||
      g_adapter.readSampleMv == nullptr) {
    return false;
  }
  CtChannelConfig channels[CT_CHANNEL_COUNT]{};
  if (!ctGetChannelConfigs(channels, CT_CHANNEL_COUNT)) return false;

  bool allValid = true;
  for (uint8_t mux = 0; mux < CT_CHANNELS_PER_BANK; ++mux) {
    if (!g_adapter.selectMux(g_adapter.context, mux)) {
      CtReading first{};
      CtReading second{};
      if (channels[mux].enabled) setInvalidReading(mux, first, 1);
      else {
        setInvalidReading(mux, first, 0);
        first.quality = CtQuality::Disabled;
      }
      if (channels[mux + CT_CHANNELS_PER_BANK].enabled) {
        setInvalidReading(static_cast<uint8_t>(mux + CT_CHANNELS_PER_BANK), second, 1);
      } else {
        setInvalidReading(static_cast<uint8_t>(mux + CT_CHANNELS_PER_BANK), second, 0);
        second.quality = CtQuality::Disabled;
      }
      first.monotonicMs = sensorNowMs();
      second.monotonicMs = first.monotonicMs;
      first.sequence = g_scanSequence + 1U;
      second.sequence = g_scanSequence + 1U;
      snapshot.readings[mux] = first;
      snapshot.readings[mux + CT_CHANNELS_PER_BANK] = second;
      ctCommitReading(mux, first);
      ctCommitReading(static_cast<uint8_t>(mux + CT_CHANNELS_PER_BANK), second);
      runtimeMarkHeartbeat(TaskKind::Sensor, first.monotonicMs);
      if (channels[mux].enabled || channels[mux + CT_CHANNELS_PER_BANK].enabled) {
        allValid = false;
      }
      continue;
    }
    g_adapter.delayUs(g_adapter.context, CT_MUX_SETTLE_US);
    CtReading first{};
    CtReading second{};
    const bool firstValid = measureChannel(mux, channels[mux], first);
    const uint8_t secondIndex = static_cast<uint8_t>(mux + CT_CHANNELS_PER_BANK);
    const bool secondValid = measureChannel(secondIndex, channels[secondIndex], second);
    snapshot.readings[mux] = first;
    snapshot.readings[mux + CT_CHANNELS_PER_BANK] = second;
    ctCommitReading(mux, first);
    ctCommitReading(static_cast<uint8_t>(mux + CT_CHANNELS_PER_BANK), second);
    runtimeMarkHeartbeat(TaskKind::Sensor, sensorNowMs());
    allValid = allValid && (!channels[mux].enabled || firstValid) &&
               (!channels[secondIndex].enabled || secondValid);
  }
  snapshot.scanSequence = ++g_scanSequence;
  snapshot.durationMs = CT_MEASUREMENT_WINDOW_US / 1000U;
  snapshot.measurementPaused = false;
  snapshot.capturedMonotonicMs = sensorNowMs();
  ctCommitScan(snapshot.scanSequence, snapshot.durationMs,
               snapshot.capturedMonotonicMs);
  return allValid || snapshot.scanSequence != 0;
}

void TaskSensor(void* parameter) {
  (void)parameter;
  ctDebugLog("INFO", "SENSOR", "TaskSensor started; channels=%u",
             static_cast<unsigned>(CT_CHANNEL_COUNT));
  bool initialized = false;
  uint64_t lastInitFailureLogMs = 0;
  uint64_t lastSummaryLogMs = 0;
  for (;;) {
    if (!initialized) {
      initialized = g_adapter.initialize == nullptr || g_adapter.initialize(g_adapter.context);
      if (!initialized) {
        const uint64_t now = sensorNowMs();
        if (lastInitFailureLogMs == 0 || now - lastInitFailureLogMs >= kSensorRetryLogIntervalMs) {
          ctDebugLog("WARN", "SENSOR", "sensor initialization not ready; retry_ms=1000");
          lastInitFailureLogMs = now;
        }
        delay(1000);
        continue;
      }
      ctDebugLog("INFO", "SENSOR", "sensor initialization complete");
    }
    SensorMaintenanceState maintenance{};
    runtimeCopySensorMaintenance(maintenance);
    if (maintenance.resumeRequested) {
      ctInvalidateReadings(CtQuality::Paused);
      ctSetMeasurementPaused(false);
      (void)runtimeAcknowledgeSensorResume(maintenance.token, sensorNowMs());
      runtimeMarkHeartbeat(TaskKind::Sensor, sensorNowMs());
      delay(1);
      continue;
    }
    if (maintenance.paused) {
      runtimeMarkHeartbeat(TaskKind::Sensor, sensorNowMs());
      delay(10);
      continue;
    }
    CtSnapshot snapshot{};
    (void)ctScanOnce(snapshot);
    const uint64_t scanNow = sensorNowMs();
    if (lastSummaryLogMs == 0 || scanNow - lastSummaryLogMs >= kSensorSummaryIntervalMs) {
      CtChannelConfig channels[CT_CHANNEL_COUNT]{};
      if (!ctGetChannelConfigs(channels, CT_CHANNEL_COUNT)) {
        ctDebugLog("ERROR", "SENSOR", "channel configuration read failed");
        delay(1);
        continue;
      }
      uint8_t validCount = 0;
      uint8_t activeCount = 0;
      uint32_t totalErrors = 0;
      for (uint8_t index = 0; index < CT_CHANNEL_COUNT; ++index) {
        const CtReading& reading = snapshot.readings[index];
        if (channels[index].enabled) ++activeCount;
        if (reading.valid) ++validCount;
        if (channels[index].enabled) totalErrors += reading.readErrors;
        ctDebugLog(reading.valid || reading.quality == CtQuality::Disabled ? "INFO" : "WARN", "CT",
                   "scan=%lu channel=CT%02u valid=%u current_a=%.3f quality=%s samples=%lu errors=%lu clipping=%lu",
                   static_cast<unsigned long>(snapshot.scanSequence),
                   static_cast<unsigned>(reading.channelNumber), reading.valid ? 1U : 0U,
                   reading.currentA, qualityName(reading.quality),
                   static_cast<unsigned long>(reading.sampleCount),
                   static_cast<unsigned long>(reading.readErrors),
                   static_cast<unsigned long>(reading.clippingCount));
      }
      ctDebugLog(validCount == activeCount ? "INFO" : "WARN", "SENSOR",
                 "scan complete; sequence=%lu valid_channels=%u/%u read_errors=%lu duration_ms=%lu",
                 static_cast<unsigned long>(snapshot.scanSequence), static_cast<unsigned>(validCount),
                 static_cast<unsigned>(activeCount), static_cast<unsigned long>(totalErrors),
                 static_cast<unsigned long>(snapshot.durationMs));
      lastSummaryLogMs = scanNow;
    }
    runtimeCopySensorMaintenance(maintenance);
    if (maintenance.pauseRequested) {
      ctSetMeasurementPaused(true);
      (void)runtimeAcknowledgeSensorPause(maintenance.token, sensorNowMs());
    }
    delay(1);
  }
}
