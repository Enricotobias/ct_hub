#include "mqtt_service.h"

#include "device_identity.h"
#include "debug_log.h"

#include <cstdio>

namespace {
constexpr std::size_t kMqttPayloadCapacity = 8192;

MqttTransportAdapter g_adapter{};
MqttTopics g_topics{};
PublishContext g_publishContext{};
char g_deviceId[CT_DEVICE_ID_SIZE]{};
RuntimeSnapshotProvider g_snapshotProvider = nullptr;
void* g_snapshotContext = nullptr;
char g_publishBuffer[kMqttPayloadCapacity]{};

bool publishDocument(const char* topic, bool retained, JsonDocument& document) {
  if (topic == nullptr) {
    ctDebugLog("ERROR", "MQTT", "publish skipped; topic is unavailable");
    return false;
  }
  if (g_adapter.publish == nullptr) {
    ctDebugLog("ERROR", "MQTT", "publish skipped; transport adapter is unavailable; topic=%s", topic);
    return false;
  }
  if (document.overflowed()) {
    ctDebugLog("ERROR", "MQTT", "publish serialization document overflowed; topic=%s", topic);
    return false;
  }
  g_publishBuffer[0] = '\0';
  const std::size_t length = serializeJson(document, g_publishBuffer, sizeof(g_publishBuffer));
  if (length == 0 || length >= sizeof(g_publishBuffer)) {
    ctDebugLog("ERROR", "MQTT", "publish serialization failed; topic=%s bytes=%u",
               topic, static_cast<unsigned>(length));
    return false;
  }
  const bool published = g_adapter.publish(g_adapter.context, topic, g_publishBuffer, retained);
  ctDebugLog(published ? "INFO" : "ERROR", "MQTT",
             "publish %s; topic=%s bytes=%u retained=%u",
             published ? "succeeded" : "failed", topic,
             static_cast<unsigned>(length), retained ? 1U : 0U);
  return published;
}
}

bool mqttServiceSetIdentity(const char* topicRoot, const char* bootId) {
  char deviceId[CT_DEVICE_ID_SIZE]{};
  if (!ctBuildStableDeviceId(deviceId, sizeof(deviceId))) return false;
  MqttTopics topics{};
  if (!mqttBuildTopics(topicRoot, deviceId, topics)) return false;
  std::snprintf(g_deviceId, sizeof(g_deviceId), "%s", deviceId);
  g_topics = topics;
  g_publishContext.deviceId = g_deviceId;
  g_publishContext.bootId = bootId;
  return true;
}

const char* mqttServiceDeviceId() { return g_deviceId[0] == '\0' ? nullptr : g_deviceId; }
const char* mqttServiceUpdateTopic() { return g_topics.update[0] == '\0' ? nullptr : g_topics.update; }
const char* mqttServiceCommandTopic() { return g_topics.command[0] == '\0' ? nullptr : g_topics.command; }

void mqttServiceConfigure(const MqttTransportAdapter& adapter,
                          RuntimeSnapshotProvider snapshotProvider, void* snapshotContext) {
  g_adapter = adapter;
  g_snapshotProvider = snapshotProvider;
  g_snapshotContext = snapshotContext;
}

void mqttServiceSetPublishContext(const PublishContext& context) {
  g_publishContext = context;
  g_publishContext.deviceId = g_deviceId;
}

void mqttServiceSetPublishTime(bool timeSynced, int64_t utcEpoch) {
  g_publishContext.timeSynced = timeSynced;
  g_publishContext.utcEpoch = timeSynced ? utcEpoch : 0;
}

void mqttServiceInit() {}

bool mqttServiceConnect() {
  return g_adapter.connect != nullptr && g_deviceId[0] != '\0' &&
         g_adapter.connect(g_adapter.context, g_deviceId);
}

void mqttServiceLoop() {
  if (g_adapter.loop != nullptr) g_adapter.loop(g_adapter.context);
}

bool mqttPublishStatus(DeviceMode mode) {
  JsonDocument document;
  if (!mqttBuildStatusDocument(mode, document)) return false;
  return publishDocument(g_topics.status, true, document);
}

bool mqttPublishInfo() {
  if (g_snapshotProvider == nullptr) return false;
  RuntimeSnapshot snapshot{};
  if (!g_snapshotProvider(g_snapshotContext, snapshot)) return false;
  JsonDocument document;
  if (!mqttBuildInfoDocument(snapshot, document)) return false;
  return publishDocument(g_topics.info, true, document);
}

bool mqttPublishData(const CtSnapshot& snapshot) {
  PublishContext context = g_publishContext;
  context.sequence = snapshot.scanSequence;
  context.monotonicMs = snapshot.capturedMonotonicMs;
  mqttServiceSetPublishContext(context);
  JsonDocument document;
  if (!mqttBuildDataDocument(snapshot, context, document)) return false;
  return publishDocument(g_topics.data, false, document);
}

bool mqttPublishAck(const AckEvent& event) {
  JsonDocument document;
  if (!mqttBuildAckDocument(event, document)) return false;
  return publishDocument(g_topics.ack, false, document);
}
