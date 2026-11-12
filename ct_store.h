#pragma once

#include "ct_types.h"

#include <cstdint>

constexpr uint64_t CT_READING_STALE_AFTER_MS = 5000;

bool ctStoreInit();
void ctCommitReading(uint8_t channelIndex, const CtReading& reading);
bool ctCopySnapshot(CtSnapshot& output, uint64_t nowMonoMs);
void ctCommitScan(uint64_t scanSequence, uint32_t durationMs,
                  uint64_t capturedMonotonicMs);
void ctSetMeasurementPaused(bool paused);
void ctInvalidateReadings(CtQuality quality);
