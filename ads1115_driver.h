#pragma once

#include "ct_types.h"

#include <cstdint>

constexpr uint16_t ADS_CONFIG_A0_A1 = 0x85E3;
constexpr uint16_t ADS_CONFIG_A2_A3 = 0xB5E3;
constexpr uint32_t ADS_CONVERSION_READY_TIMEOUT_US = 5000;
constexpr uint32_t ADS_TRANSACTION_TIMEOUT_US = 10000;

using AdsWriteConfig = bool (*)(void* context, uint16_t config);
using AdsReadConversion = bool (*)(void* context, uint16_t& raw, bool& ready);
using AdsNowMicros = uint64_t (*)(void* context);
using AdsDelayMicros = void (*)(void* context, uint32_t delayUs);

struct AdsBusAdapter {
  void* context;
  AdsWriteConfig writeConfig;
  AdsReadConversion readConversion;
  AdsNowMicros nowMicros;
  AdsDelayMicros delayMicros;
};

void adsConfigure(const AdsBusAdapter& adapter);
bool adsBegin();
bool adsProbe();
bool adsReadDifferentialMv(AdsPair pair, float& millivolts);
void adsRecover();
