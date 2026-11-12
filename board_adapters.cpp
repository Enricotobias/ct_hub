#include "board_adapters.h"

#include "ads1115_driver.h"
#include "ct_store.h"
#include "debug_log.h"
#include "mqtt_service.h"
#include "ota.h"
#include "runtime_state.h"
#include "task_sensor.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <Wire.h>
#include <esp_timer.h>

#include <ctime>
#include <cstdio>

namespace {
constexpr std::size_t kMqttBufferSize = 8192;

struct BoardContext {
  CalibrationPersistence* calibration;
  volatile bool* resourceFailure;
  volatile bool sensorReady;
};

BoardContext g_context{};
WiFiClient g_mqttClient;
PubSubClient g_mqtt(g_mqttClient);
NetworkTaskAdapter g_networkAdapter{};
HealthAdapter g_healthAdapter{};
NetworkMessageCallback g_messageCallback = nullptr;
void* g_messageCallbackContext = nullptr;
bool g_wifiStartRequested = false;
bool g_wifiConfigWarningLogged = false;
uint32_t g_lastSubscribeFailureLogMs = 0;
bool g_hasSubscribeFailureLog = false;
int g_lastLoggedWifiStatus = -1;
bool g_timeConfigured = false;
bool g_mqttBufferConfigured = false;
bool g_ledPinsConfigured = false;
bool g_ledPwmReady = false;

bool writeAdsConfig(void*, uint16_t config) {
  Wire.beginTransmission(CT_ADS1115_ADDRESS);
  Wire.write(0x01);
  Wire.write(static_cast<uint8_t>(config >> 8));
  Wire.write(static_cast<uint8_t>(config & 0xffU));
  return Wire.endTransmission(true) == 0;
}

bool readAdsRegister(uint8_t registerAddress, uint16_t& value) {
  Wire.beginTransmission(CT_ADS1115_ADDRESS);
  Wire.write(registerAddress);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(static_cast<uint8_t>(CT_ADS1115_ADDRESS), static_cast<uint8_t>(2),
                       static_cast<uint8_t>(true)) != 2) return false;
  value = static_cast<uint16_t>(Wire.read()) << 8;
  value |= static_cast<uint16_t>(Wire.read());
  return true;
}

bool readAdsConversion(void*, uint16_t& raw, bool& ready) {
  uint16_t config = 0;
  if (!readAdsRegister(0x01, config)) return false;
  ready = (config & 0x8000U) != 0;
  if (!ready) return true;
  return readAdsRegister(0x00, raw);
}

uint64_t adsNowMicros(void*) {
  return static_cast<uint64_t>(esp_timer_get_time());
}

void delayMicros(void*, uint32_t delayUs) {
  delayMicroseconds(delayUs);
}

bool initializeSensor(void* context) {
  BoardContext* board = static_cast<BoardContext*>(context);
  Wire.begin(CT_I2C_SDA_PIN, CT_I2C_SCL_PIN);
  Wire.setTimeOut(20);
  pinMode(CT_MUX_S0_PIN, OUTPUT);
  pinMode(CT_MUX_S1_PIN, OUTPUT);
  pinMode(CT_MUX_S2_PIN, OUTPUT);
  digitalWrite(CT_MUX_S0_PIN, LOW);
  digitalWrite(CT_MUX_S1_PIN, LOW);
  digitalWrite(CT_MUX_S2_PIN, LOW);
  const bool ready = adsBegin();
  if (board != nullptr) board->sensorReady = ready;
  ctDebugLog(ready ? "INFO" : "ERROR", "SENSOR",
             "ADS1115 initialization %s; address=0x%02X",
             ready ? "succeeded" : "failed", CT_ADS1115_ADDRESS);
  return ready;
}

bool selectMux(void*, uint8_t muxChannel) {
  if (muxChannel >= CT_CHANNELS_PER_BANK) return false;
  digitalWrite(CT_MUX_S0_PIN, (muxChannel & 0x01U) != 0 ? HIGH : LOW);
  digitalWrite(CT_MUX_S1_PIN, (muxChannel & 0x02U) != 0 ? HIGH : LOW);
  digitalWrite(CT_MUX_S2_PIN, (muxChannel & 0x04U) != 0 ? HIGH : LOW);
  return true;
}

void sensorDelayUs(void*, uint32_t delayUs) {
  delayMicroseconds(delayUs);
}

bool readSampleMv(void*, uint8_t, AdsPair pair, float& millivolts) {
  return adsReadDifferentialMv(pair, millivolts);
}

uint64_t nowMillis(void*) {
  return static_cast<uint64_t>(millis());
}

void ensureWiFiStarted() {
  if (g_wifiStartRequested) return;
  if (CT_WIFI_SSID == nullptr || CT_WIFI_SSID[0] == '\0') {
    if (!g_wifiConfigWarningLogged) {
      ctDebugLog("ERROR", "WIFI", "SSID is empty; Wi-Fi start skipped");
      g_wifiConfigWarningLogged = true;
    }
    return;
  }
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(CT_WIFI_SSID, CT_WIFI_PASSWORD);
  g_wifiStartRequested = true;
  ctDebugLog("INFO", "WIFI", "connection started; status=%d", static_cast<int>(WiFi.status()));
}

bool connectMqtt(void*, const char* clientId) {
  if (clientId == nullptr) {
    ctDebugLog("ERROR", "MQTT", "connect skipped; client ID is unavailable");
    return false;
  }
  if (WiFi.status() != WL_CONNECTED) {
    ctDebugLog("WARN", "MQTT", "connect deferred; Wi-Fi status=%d", static_cast<int>(WiFi.status()));
    return false;
  }
  if (CT_MQTT_HOST == nullptr || CT_MQTT_HOST[0] == '\0') {
    ctDebugLog("ERROR", "MQTT", "connect skipped; broker host is empty");
    return false;
  }
  if (!g_mqttBufferConfigured) {
    if (!g_mqtt.setBufferSize(kMqttBufferSize)) {
      ctDebugLog("ERROR", "MQTT", "buffer allocation failed; requested=%u",
                 static_cast<unsigned>(kMqttBufferSize));
      return false;
    }
    g_mqttBufferConfigured = true;
  }
  g_mqtt.setServer(CT_MQTT_HOST, CT_MQTT_PORT);
  g_mqtt.setKeepAlive(45);
  g_mqtt.setSocketTimeout(5);
  char offlineStatus[96]{};
  const int statusLength = std::snprintf(offlineStatus, sizeof(offlineStatus),
                                         "{\"status\":\"offline\",\"reason\":\"CONNECTION_LOST\"}");
  const char* statusTopic = nullptr;
  MqttTopics topics{};
  if (mqttBuildTopics(CT_MQTT_ROOT, clientId, topics)) statusTopic = topics.status;
  if (statusTopic == nullptr || statusLength <= 0 ||
      static_cast<std::size_t>(statusLength) >= sizeof(offlineStatus)) {
    ctDebugLog("ERROR", "MQTT", "last-will configuration failed");
    return false;
  }
  ctDebugLog("INFO", "MQTT", "connecting to broker; port=%u", static_cast<unsigned>(CT_MQTT_PORT));
  bool connected = false;
  if (CT_MQTT_USERNAME != nullptr && CT_MQTT_USERNAME[0] != '\0') {
    connected = g_mqtt.connect(clientId, CT_MQTT_USERNAME, CT_MQTT_PASSWORD,
                                statusTopic, 1, true, offlineStatus);
  } else {
    connected = g_mqtt.connect(clientId, statusTopic, 1, true, offlineStatus);
  }
  ctDebugLog(connected ? "INFO" : "ERROR", "MQTT",
             "broker connection %s; state=%d", connected ? "established" : "failed", g_mqtt.state());
  return connected;
}

void mqttLoop(void*) {
  (void)g_mqtt.loop();
}

bool mqttPublish(void*, const char* topic, const char* payload, bool retained) {
  return g_mqtt.publish(topic, payload, retained);
}

void mqttMessageReceived(char* topic, uint8_t* payload, unsigned int length) {
  if (g_messageCallback != nullptr) {
    g_messageCallback(g_messageCallbackContext, topic, payload, length);
  }
}

bool mqttSubscribe(void*, const char* topic, NetworkMessageCallback callback,
                   void* callbackContext) {
  if (topic == nullptr || callback == nullptr) return false;
  g_messageCallback = callback;
  g_messageCallbackContext = callbackContext;
  g_mqtt.setCallback(mqttMessageReceived);
  const bool subscribed = g_mqtt.subscribe(topic, 1);
  const uint32_t now = millis();
  if (subscribed || !g_hasSubscribeFailureLog ||
      now - g_lastSubscribeFailureLogMs >= 5000U) {
    ctDebugLog(subscribed ? "INFO" : "ERROR", "MQTT",
               "subscribe %s; topic=%s; state=%d", subscribed ? "succeeded" : "failed",
               topic, g_mqtt.state());
    if (!subscribed) {
      g_lastSubscribeFailureLogMs = now;
      g_hasSubscribeFailureLog = true;
    }
  }
  return subscribed;
}

bool networkConnect(void*, const char*) {
  ensureWiFiStarted();
  const int wifiStatus = static_cast<int>(WiFi.status());
  if (wifiStatus != g_lastLoggedWifiStatus) {
    ctDebugLog(wifiStatus == WL_CONNECTED ? "INFO" : "WARN", "WIFI",
               "connection state changed; status=%d connected=%u", wifiStatus,
               wifiStatus == WL_CONNECTED ? 1U : 0U);
    g_lastLoggedWifiStatus = wifiStatus;
  }
  if (wifiStatus != WL_CONNECTED) return false;
  if (!g_timeConfigured) {
    configTime(0, 0, "pool.ntp.org", "time.google.com");
    g_timeConfigured = true;
  }
  return mqttServiceConnect();
}

void networkDisconnect(void*) {
  g_mqtt.disconnect();
}

bool readUtcTime(void*, bool& synchronized, int64_t& utcEpoch) {
  const std::time_t now = std::time(nullptr);
  synchronized = now >= static_cast<std::time_t>(CT_OTA_MIN_VALID_EPOCH);
  utcEpoch = synchronized ? static_cast<int64_t>(now) : 0;
  return synchronized;
}

bool publishAck(void*, const AckEvent& event) {
  return mqttPublishAck(event);
}

bool publishInfo(void*) {
  return mqttPublishInfo();
}

bool applyCalibration(void* context, uint8_t channel, float gain) {
  BoardContext* board = static_cast<BoardContext*>(context);
  return board != nullptr && board->calibration != nullptr &&
         ctIsValidExternalChannel(channel) &&
         ctSetCalibrationGain(static_cast<uint8_t>(channel - 1U), gain,
                             *board->calibration);
}

bool applyActiveChannels(void*, uint8_t channelCount) {
  return ctSetActiveChannelCount(channelCount);
}

bool scheduleReboot(void*, uint8_t delaySeconds, const char* requestId) {
  if (delaySeconds < 1 || delaySeconds > 60 || requestId == nullptr) return false;
  PendingReboot pending{true, 0, {}};
  pending.deadlineMonoMs = static_cast<uint32_t>(
      millis() + static_cast<uint32_t>(delaySeconds) * 1000U);
  std::snprintf(pending.requestId, sizeof(pending.requestId), "%s", requestId);
  runtimeSetPendingReboot(pending);
  ctDebugLog("INFO", "NETWORK", "remote reboot scheduled; delay_seconds=%u",
             static_cast<unsigned>(delaySeconds));
  return true;
}

bool networkIsConnected(void*) {
  return g_mqtt.connected();
}

bool readHealthInput(void* context, HealthInput& input) {
  BoardContext* board = static_cast<BoardContext*>(context);
  RuntimeStats stats{};
  runtimeCopyStats(stats);
  input.nowMonoMs = millis();
  input.sensorHeartbeatMs = stats.sensorHeartbeatMs;
  input.networkHeartbeatMs = stats.networkHeartbeatMs;
  input.sensorProgressing = stats.sensorHeartbeatMs != 0 &&
                            input.nowMonoMs >= stats.sensorHeartbeatMs &&
                            input.nowMonoMs - stats.sensorHeartbeatMs <= 2000U;
  input.wifiConnected = WiFi.status() == WL_CONNECTED;
  input.mqttConnected = g_mqtt.connected();
  input.resourceFailure = board == nullptr || board->resourceFailure == nullptr ||
                          *board->resourceFailure || !board->sensorReady;
  bool imageMatches = false;
  (void)otaCheckRunningExpectedImage(imageMatches);
  input.runningImageMatchesTarget = imageMatches;
  return true;
}

void writeRgb(void*, const RgbOutput& output) {
  if (!g_ledPinsConfigured) {
    const bool redReady = ledcAttach(CT_LED_RED_PIN, CT_LED_PWM_FREQUENCY_HZ,
                                     CT_LED_PWM_RESOLUTION_BITS);
    const bool greenReady = ledcAttach(CT_LED_GREEN_PIN, CT_LED_PWM_FREQUENCY_HZ,
                                       CT_LED_PWM_RESOLUTION_BITS);
    const bool blueReady = ledcAttach(CT_LED_BLUE_PIN, CT_LED_PWM_FREQUENCY_HZ,
                                      CT_LED_PWM_RESOLUTION_BITS);
    g_ledPwmReady = redReady && greenReady && blueReady;
    g_ledPinsConfigured = true;
    ctDebugLog(g_ledPwmReady ? "INFO" : "ERROR", "HEALTH",
               "RGB PWM setup %s; red=%d green=%d blue=%d",
               g_ledPwmReady ? "succeeded" : "failed", redReady, greenReady, blueReady);
    if (!g_ledPwmReady && g_context.resourceFailure != nullptr) {
      *g_context.resourceFailure = true;
    }
  }
  if (!g_ledPwmReady) return;
  ledcWrite(CT_LED_RED_PIN, output.red ? CT_LED_BRIGHTNESS_DUTY : 0);
  ledcWrite(CT_LED_GREEN_PIN, output.green ? CT_LED_BRIGHTNESS_DUTY : 0);
  ledcWrite(CT_LED_BLUE_PIN, output.blue ? CT_LED_BRIGHTNESS_DUTY : 0);
}

void idleNetwork(void*, uint32_t delayMs) {
  delay(delayMs);
}

bool provideRuntimeSnapshot(void* context, RuntimeSnapshot& snapshot) {
  BoardContext* board = static_cast<BoardContext*>(context);
  RuntimeStats stats{};
  runtimeCopyStats(stats);
  stats.freeHeapBytes = ESP.getFreeHeap();
  CtSnapshot sensorSnapshot{};
  if (!ctCopySnapshot(sensorSnapshot, millis())) return false;
  snapshot.stats = stats;
  snapshot.sensorSequence = sensorSnapshot.scanSequence;
  snapshot.networkConnected = g_mqtt.connected();
  snapshot.calibrationRevision = board != nullptr && board->calibration != nullptr
                                     ? board->calibration->revision : 0;
  return true;
}

uint64_t networkNowMillis(void*) {
  return static_cast<uint64_t>(millis());
}
}

void boardConfigureSensorAdapters() {
  const AdsBusAdapter adsAdapter{nullptr, writeAdsConfig, readAdsConversion,
                                 adsNowMicros, delayMicros};
  adsConfigure(adsAdapter);
  const SensorScanAdapter sensorAdapter{&g_context, initializeSensor, selectMux,
                                        sensorDelayUs, readSampleMv, nowMillis};
  ctConfigureSensorScan(sensorAdapter);
}

void boardConfigureNetworkAdapters(CalibrationPersistence& calibration,
                                   volatile bool& resourceFailure) {
  g_context.calibration = &calibration;
  g_context.resourceFailure = &resourceFailure;
  const MqttTransportAdapter mqttAdapter{nullptr, connectMqtt, mqttLoop, mqttPublish};
  mqttServiceConfigure(mqttAdapter, provideRuntimeSnapshot, &g_context);
  g_networkAdapter = {&g_context, networkNowMillis, networkIsConnected, networkConnect,
                      networkDisconnect, mqttSubscribe, readUtcTime, idleNetwork,
                      {&g_context, publishAck, publishInfo, applyCalibration,
                       applyActiveChannels, scheduleReboot}};
}

void boardConfigureHealthAdapter(volatile bool& resourceFailure) {
  g_context.resourceFailure = &resourceFailure;
  g_healthAdapter = {&g_context, readHealthInput, writeRgb, nowMillis};
  healthConfigure(g_healthAdapter);
}

NetworkTaskAdapter* boardNetworkTaskAdapter() { return &g_networkAdapter; }
HealthAdapter* boardHealthAdapter() { return &g_healthAdapter; }
