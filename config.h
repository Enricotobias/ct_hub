#pragma once

#include "ct_types.h"

#include <cstddef>
#include <cstdint>

constexpr uint8_t CT_I2C_SDA_PIN = 21;
constexpr uint8_t CT_I2C_SCL_PIN = 22;
constexpr uint8_t CT_MUX_S0_PIN = 25;
constexpr uint8_t CT_MUX_S1_PIN = 26;
constexpr uint8_t CT_MUX_S2_PIN = 27;
constexpr uint8_t CT_LED_RED_PIN = 23;
constexpr uint8_t CT_LED_GREEN_PIN = 19;
constexpr uint8_t CT_LED_BLUE_PIN = 18;
constexpr uint32_t CT_LED_PWM_FREQUENCY_HZ = 5000;
constexpr uint8_t CT_LED_PWM_RESOLUTION_BITS = 8;
constexpr uint8_t CT_LED_BRIGHTNESS_DUTY = 32;
constexpr char CT_MQTT_ROOT[] = "ct-hub";

extern const char* CT_WIFI_SSID;
extern const char* CT_WIFI_PASSWORD;
extern const char* CT_MQTT_HOST;
extern const uint16_t CT_MQTT_PORT;
extern const char* CT_MQTT_USERNAME;
extern const char* CT_MQTT_PASSWORD;
extern const char* CT_ROOT_CA;
extern const char CT_HUB_FW_VERSION_TAG[];

#define CT_HUB_PRODUCT_ID "CT_HUB_16"
#define CT_HUB_BOARD_ID "ESP32_DEVKITC_V4"
constexpr uint32_t CT_OTA_PROGRESS_INTERVAL_MS = 5000;
constexpr uint32_t CT_OTA_NO_DATA_TIMEOUT_MS = 15000;
constexpr uint32_t CT_OTA_TOTAL_TIMEOUT_MS = 600000;
constexpr uint32_t CT_OTA_HEALTH_GATE_MS = 60000;
constexpr uint64_t CT_OTA_MIN_VALID_EPOCH = 1704067200ULL;

constexpr uint8_t CT_ADS1115_ADDRESS = 0x48;
constexpr float CT_ADS1115_LSB_MV = 0.0625f;
constexpr float CT_CURRENT_A_PER_V_RMS = 30.0f;
constexpr uint32_t CT_MUX_SETTLE_US = 2000;
constexpr uint16_t CT_DISCARD_SAMPLES = 16;
constexpr uint32_t CT_SAMPLE_PERIOD_US = 2000;
constexpr uint32_t CT_MEASUREMENT_WINDOW_US = 200000;
constexpr uint32_t CT_MIN_VALID_SAMPLES = 20;
constexpr uint16_t CT_CALIBRATION_SCHEMA_VERSION = 1;
constexpr float CT_CALIBRATION_GAIN_MIN = 0.1f;
constexpr float CT_CALIBRATION_GAIN_MAX = 10.0f;
constexpr uint8_t CT_DEFAULT_ACTIVE_CHANNEL_COUNT = CT_CHANNEL_COUNT;

using CalibrationLoadAdapter = bool (*)(
    void* context,
    uint16_t& schemaVersion,
    uint32_t& revision,
    float* gains,
    std::size_t gainCount
);

using CalibrationSaveAdapter = bool (*)(
    void* context,
    uint16_t schemaVersion,
    uint32_t revision,
    const float* gains,
    std::size_t gainCount
);

struct CalibrationPersistence {
  uint16_t schemaVersion;
  uint32_t revision;
  float gains[CT_CHANNEL_COUNT];
  void* context;
  CalibrationLoadAdapter load;
  CalibrationSaveAdapter save;
};

CtChannelConfig ctGetChannelConfig(uint8_t channelIndex);
bool ctGetChannelConfigs(CtChannelConfig* output, std::size_t channelCount);
bool ctIsValidExternalChannel(uint8_t channelNumber);
bool ctLoadActiveChannelCount(uint8_t& output);
bool ctSetActiveChannelCount(uint8_t channelCount);
bool ctLoadCalibration(CalibrationPersistence& persistence);
bool ctSetCalibrationGain(uint8_t channelIndex, float gain, CalibrationPersistence& persistence);
