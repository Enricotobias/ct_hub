#include "ct_store.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace {
CtSnapshot g_snapshot{};
bool g_initialized = false;
SemaphoreHandle_t g_storeMutex = nullptr;

bool lockStore() {
  return g_storeMutex != nullptr && xSemaphoreTake(g_storeMutex, portMAX_DELAY) == pdTRUE;
}

void unlockStore() { xSemaphoreGive(g_storeMutex); }
}

bool ctStoreInit() {
  if (g_storeMutex == nullptr) g_storeMutex = xSemaphoreCreateMutex();
  if (g_storeMutex == nullptr || !lockStore()) return false;
  g_snapshot = {};
  g_initialized = true;
  for (uint8_t index = 0; index < CT_CHANNEL_COUNT; ++index) {
    g_snapshot.readings[index].channelNumber = static_cast<uint8_t>(index + 1U);
    g_snapshot.readings[index].quality = CtQuality::SensorError;
  }
  unlockStore();
  return true;
}

void ctCommitReading(uint8_t channelIndex, const CtReading& reading) {
  if (!g_initialized || channelIndex >= CT_CHANNEL_COUNT || !lockStore()) return;
  g_snapshot.readings[channelIndex] = reading;
  unlockStore();
}

bool ctCopySnapshot(CtSnapshot& output, uint64_t nowMonoMs) {
  if (!g_initialized || !lockStore()) return false;
  output = g_snapshot;
  unlockStore();
  for (uint8_t index = 0; index < CT_CHANNEL_COUNT; ++index) {
    CtReading& reading = output.readings[index];
    if (reading.valid && nowMonoMs >= reading.monotonicMs &&
        nowMonoMs - reading.monotonicMs > CT_READING_STALE_AFTER_MS) {
      reading.valid = false;
      reading.quality = CtQuality::Stale;
    }
  }
  return true;
}

void ctCommitScan(uint64_t scanSequence, uint32_t durationMs,
                  uint64_t capturedMonotonicMs) {
  if (!g_initialized || !lockStore()) return;
  g_snapshot.scanSequence = scanSequence;
  g_snapshot.durationMs = durationMs;
  g_snapshot.capturedMonotonicMs = capturedMonotonicMs;
  unlockStore();
}

void ctInvalidateReadings(CtQuality quality) {
  if (!g_initialized || !lockStore()) return;
  for (uint8_t index = 0; index < CT_CHANNEL_COUNT; ++index) {
    g_snapshot.readings[index].valid = false;
    g_snapshot.readings[index].quality = quality;
  }
  unlockStore();
}

void ctSetMeasurementPaused(bool paused) {
  if (!g_initialized || !lockStore()) return;
  g_snapshot.measurementPaused = paused;
  if (paused) {
    for (uint8_t index = 0; index < CT_CHANNEL_COUNT; ++index) {
      g_snapshot.readings[index].valid = false;
      g_snapshot.readings[index].quality = CtQuality::Paused;
    }
  }
  unlockStore();
}
