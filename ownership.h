#pragma once

#include <cstdint>

enum class OwnedResource : uint8_t { SensorStore, MqttTransport, Calibration, RgbOutput, HttpUpdate };

struct OwnershipClaim {
  OwnedResource resource;
  uint8_t ownerTask;
};

bool ownershipClaimsAreValid(const OwnershipClaim* claims, uint8_t count);
