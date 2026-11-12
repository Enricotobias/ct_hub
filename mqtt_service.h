#pragma once

#include "mqtt_schema.h"

#include <cstddef>

struct MqttTransportAdapter {
  void* context;
  bool (*connect)(void* context, const char* clientId);
  void (*loop)(void* context);
  bool (*publish)(void* context, const char* topic, const char* payload, bool retained);
};

using RuntimeSnapshotProvider = bool (*)(void* context, RuntimeSnapshot& snapshot);

bool mqttServiceSetIdentity(const char* topicRoot, const char* bootId);
const char* mqttServiceDeviceId();
const char* mqttServiceUpdateTopic();
const char* mqttServiceCommandTopic();
void mqttServiceConfigure(const MqttTransportAdapter& adapter,
                          RuntimeSnapshotProvider snapshotProvider, void* snapshotContext);
void mqttServiceSetPublishContext(const PublishContext& context);
void mqttServiceSetPublishTime(bool timeSynced, int64_t utcEpoch);
void mqttServiceInit();
bool mqttServiceConnect();
void mqttServiceLoop();
bool mqttPublishStatus(DeviceMode mode);
bool mqttPublishInfo();
bool mqttPublishData(const CtSnapshot& snapshot);
bool mqttPublishAck(const AckEvent& event);
