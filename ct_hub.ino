#include "board_adapters.h"
#include "ct_store.h"
#include "config.h"
#include "runtime_state.h"
#include "task_health.h"
#include "task_network.h"
#include "task_sensor.h"
#include "mqtt_schema.h"
#include "mqtt_service.h"
#include "ownership.h"
#include "ota.h"
#include "ota_platform.h"
#include "debug_log.h"

#include <Arduino.h>
#include <cstdio>
#include <esp_system.h>

namespace {
constexpr uint32_t kSensorTaskStackSize = 16384;
constexpr uint32_t kNetworkTaskStackSize = 16384;
constexpr uint32_t kHealthTaskStackSize = 4096;
constexpr uint32_t kOtaTaskStackSize = 12288;
constexpr UBaseType_t kSensorTaskPriority = 3;
constexpr UBaseType_t kNetworkTaskPriority = 2;
constexpr UBaseType_t kHealthTaskPriority = 1;
constexpr UBaseType_t kOtaTaskPriority = 1;

const OwnershipClaim kOwnershipClaims[] = {
    {OwnedResource::SensorStore, 1},
    {OwnedResource::MqttTransport, 2},
    {OwnedResource::Calibration, 2},
    {OwnedResource::RgbOutput, 3},
    {OwnedResource::HttpUpdate, 4},
};
CalibrationPersistence g_calibration{};
volatile bool g_resourceFailure = false;
char g_bootId[24]{};
}

void setup() {
  Serial.begin(115200);
  ctDebugLog("INFO", "BOOT", "setup started; serial_baud=115200");
  const RuntimeInitResult resources = runtimeInit();
  if (!resources.mutexReady || !resources.commandQueueReady || !resources.eventQueueReady) {
    ctDebugLog("ERROR", "BOOT", "runtime initialization failed");
    return;
  }
  ctDebugLog("INFO", "BOOT", "runtime initialized");
  if (!ctStoreInit()) {
    ctDebugLog("ERROR", "BOOT", "CT store initialization failed");
    return;
  }
  ctDebugLog("INFO", "BOOT", "CT store initialized");
  if (!ctLoadCalibration(g_calibration)) {
    g_resourceFailure = true;
    ctDebugLog("ERROR", "BOOT", "calibration load failed; defaults remain active");
  } else {
    ctDebugLog("INFO", "BOOT", "calibration loaded; revision=%lu",
               static_cast<unsigned long>(g_calibration.revision));
  }
  uint8_t activeChannelCount = CT_DEFAULT_ACTIVE_CHANNEL_COUNT;
  if (!ctLoadActiveChannelCount(activeChannelCount)) {
    g_resourceFailure = true;
    ctDebugLog("ERROR", "BOOT", "active channel count load failed; default remains active");
  } else {
    ctDebugLog("INFO", "BOOT", "active channel count loaded; channels=%u",
               static_cast<unsigned>(activeChannelCount));
  }
  otaConfigure(otaPlatformCreateAdapter());
  std::snprintf(g_bootId, sizeof(g_bootId), "%08lx%08lx",
                static_cast<unsigned long>(esp_random()),
                static_cast<unsigned long>(esp_random()));
  if (!mqttServiceSetIdentity(CT_MQTT_ROOT, g_bootId)) {
    ctDebugLog("ERROR", "BOOT", "device identity/topic initialization failed");
    return;
  }
  if (!ownershipClaimsAreValid(kOwnershipClaims,
                               static_cast<uint8_t>(sizeof(kOwnershipClaims) / sizeof(kOwnershipClaims[0])))) {
    ctDebugLog("ERROR", "BOOT", "resource ownership validation failed");
    return;
  }

  boardConfigureSensorAdapters();
  boardConfigureNetworkAdapters(g_calibration, g_resourceFailure);
  boardConfigureHealthAdapter(g_resourceFailure);
  ctDebugLog("INFO", "BOOT", "board adapters configured");

  if (xTaskCreate(TaskHealth, "CTHealth", kHealthTaskStackSize,
                  boardHealthAdapter(), kHealthTaskPriority, nullptr) != pdPASS) {
    g_resourceFailure = true;
    ctDebugLog("ERROR", "BOOT", "TaskHealth creation failed");
    return;
  }
  ctDebugLog("INFO", "BOOT", "TaskHealth created; stack=%lu priority=%u",
             static_cast<unsigned long>(kHealthTaskStackSize), static_cast<unsigned>(kHealthTaskPriority));
  if (xTaskCreate(TaskSensor, "CTSensor", kSensorTaskStackSize,
                  nullptr, kSensorTaskPriority, nullptr) != pdPASS) {
    g_resourceFailure = true;
    ctDebugLog("ERROR", "BOOT", "TaskSensor creation failed");
    return;
  }
  ctDebugLog("INFO", "BOOT", "TaskSensor created; stack=%lu priority=%u",
             static_cast<unsigned long>(kSensorTaskStackSize), static_cast<unsigned>(kSensorTaskPriority));
  if (xTaskCreate(TaskNetwork, "CTNetwork", kNetworkTaskStackSize,
                  boardNetworkTaskAdapter(), kNetworkTaskPriority, nullptr) != pdPASS) {
    g_resourceFailure = true;
    ctDebugLog("ERROR", "BOOT", "TaskNetwork creation failed");
    return;
  }
  ctDebugLog("INFO", "BOOT", "TaskNetwork created; stack=%lu priority=%u",
             static_cast<unsigned long>(kNetworkTaskStackSize), static_cast<unsigned>(kNetworkTaskPriority));
  if (xTaskCreate(TaskOTA, "CTOTA", kOtaTaskStackSize,
                  nullptr, kOtaTaskPriority, nullptr) != pdPASS) {
    g_resourceFailure = true;
    ctDebugLog("ERROR", "BOOT", "TaskOTA creation failed");
    return;
  }
  ctDebugLog("INFO", "BOOT", "TaskOTA created; stack=%lu priority=%u",
             static_cast<unsigned long>(kOtaTaskStackSize), static_cast<unsigned>(kOtaTaskPriority));
  ctDebugLog("INFO", "BOOT", "firmware tasks started; device_id=%s", mqttServiceDeviceId());
}

void loop() { delay(1); }
