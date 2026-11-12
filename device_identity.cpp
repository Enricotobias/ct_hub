#include "device_identity.h"

#include <cstdio>
#include <cstring>

#include <Arduino.h>
#include <WiFi.h>

namespace {
bool isValidMac(const char* mac) {
  if (mac == nullptr || std::strlen(mac) != 17) return false;
  bool allZero = true;
  for (std::size_t index = 0; index < 17; ++index) {
    if (index == 2 || index == 5 || index == 8 || index == 11 || index == 14) {
      if (mac[index] != ':') return false;
      continue;
    }
    const char value = mac[index];
    const bool hexadecimal = (value >= '0' && value <= '9') ||
                             (value >= 'A' && value <= 'F') ||
                             (value >= 'a' && value <= 'f');
    if (!hexadecimal) return false;
    if (value != '0') allZero = false;
  }
  return !allZero;
}

bool copyMac(char* output, std::size_t capacity, const char* mac) {
  if (output == nullptr || capacity < CT_DEVICE_ID_SIZE || !isValidMac(mac)) return false;
  std::memcpy(output, mac, CT_DEVICE_ID_SIZE);
  return true;
}
}

bool ctBuildStableDeviceId(char* output, std::size_t capacity) {
  if (output == nullptr || capacity == 0) return false;
  output[0] = '\0';
  if (capacity < CT_DEVICE_ID_SIZE) return false;

  const uint64_t efuseMac = ESP.getEfuseMac();
  char formattedEfuseMac[CT_DEVICE_ID_SIZE]{};
  std::snprintf(formattedEfuseMac, sizeof(formattedEfuseMac), "%02X:%02X:%02X:%02X:%02X:%02X",
                static_cast<uint8_t>(efuseMac >> 40U),
                static_cast<uint8_t>(efuseMac >> 32U),
                static_cast<uint8_t>(efuseMac >> 24U),
                static_cast<uint8_t>(efuseMac >> 16U),
                static_cast<uint8_t>(efuseMac >> 8U),
                static_cast<uint8_t>(efuseMac));
  if (copyMac(output, capacity, formattedEfuseMac)) return true;

  const String wifiMac = WiFi.macAddress();
  if (copyMac(output, capacity, wifiMac.c_str())) return true;

  return false;
}
