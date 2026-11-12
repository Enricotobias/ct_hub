#include "ads1115_driver.h"

#include "config.h"

#include <cstdint>

namespace {

AdsBusAdapter g_adapter{};
bool g_ready = false;

uint16_t configForPair(AdsPair pair) {
  return pair == AdsPair::A0_A1 ? ADS_CONFIG_A0_A1 : ADS_CONFIG_A2_A3;
}

bool hasAdapter() {
  return g_adapter.writeConfig != nullptr && g_adapter.readConversion != nullptr &&
         g_adapter.nowMicros != nullptr && g_adapter.delayMicros != nullptr;
}

float convertRaw(uint16_t raw) {
  const int16_t signedRaw = static_cast<int16_t>(raw);
  return static_cast<float>(signedRaw) * CT_ADS1115_LSB_MV;
}

}  // namespace

void adsConfigure(const AdsBusAdapter& adapter) {
  g_adapter = adapter;
  g_ready = false;
}

bool adsBegin() {
  g_ready = hasAdapter();
  return g_ready;
}

bool adsProbe() {
  return g_ready || adsBegin();
}

bool adsReadDifferentialMv(AdsPair pair, float& millivolts) {
  millivolts = 0.0f;
  if (!adsProbe() || !g_adapter.writeConfig(g_adapter.context, configForPair(pair))) {
    return false;
  }

  const uint64_t startUs = g_adapter.nowMicros(g_adapter.context);
  const uint64_t deadlineUs = startUs + ADS_CONVERSION_READY_TIMEOUT_US;
  for (;;) {
    uint16_t raw = 0;
    bool ready = false;
    if (!g_adapter.readConversion(g_adapter.context, raw, ready)) {
      return false;
    }
    if (ready) {
      millivolts = convertRaw(raw);
      return true;
    }
    if (g_adapter.nowMicros(g_adapter.context) >= deadlineUs) {
      return false;
    }
    g_adapter.delayMicros(g_adapter.context, 100);
  }
}

void adsRecover() {
  g_ready = false;
  (void)adsBegin();
}
