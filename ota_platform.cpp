#include "ota_platform.h"

#include "config.h"

#include <cstdio>
#include <cstring>

#include <Arduino.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <esp_app_desc.h>
#include <esp_image_format.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_system.h>
#include <mbedtls/md5.h>

namespace {
constexpr std::size_t kManifestCapacity = 4096;
constexpr uint32_t kHttpConnectTimeoutMs = 10000;
constexpr uint32_t kHttpReadTimeoutMs = 15000;
constexpr uint32_t kOtaImageLimitBytes = 0x01000000U;
__attribute__((used)) const char g_productIdentityTag[] = "CT_HUB_PRODUCT:CT_HUB_16";
__attribute__((used)) const char g_boardIdentityTag[] = "CT_HUB_BOARD:ESP32_DEVKITC_V4";
__attribute__((used)) const char* g_versionIdentityTag = CT_HUB_FW_VERSION_TAG;

esp_ota_handle_t g_otaHandle = 0;
bool g_otaHandleOpen = false;
const esp_partition_t* g_targetPartition = nullptr;

void setDetail(char* detail, std::size_t capacity, const char* value) {
  if (detail == nullptr || capacity == 0) return;
  std::snprintf(detail, capacity, "%s", value == nullptr ? "UNKNOWN" : value);
}

bool fetchHttps(const char* url, uint8_t* output, std::size_t capacity,
                std::size_t& outputLength, const char* failure,
                char* detail, std::size_t detailCapacity) {
  outputLength = 0;
  if (CT_ROOT_CA[0] == '\0') { setDetail(detail, detailCapacity, "OTA_CA_UNCONFIGURED"); return false; }
  NetworkClientSecure secureClient;
  secureClient.setCACert(CT_ROOT_CA);
  HTTPClient http;
  http.setConnectTimeout(static_cast<int32_t>(kHttpConnectTimeoutMs));
  http.setTimeout(static_cast<uint16_t>(kHttpReadTimeoutMs));
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  if (!http.begin(secureClient, String(url))) { setDetail(detail, detailCapacity, "HTTPS_BEGIN_FAILED"); return false; }
  const int status = http.GET();
  if (status != HTTP_CODE_OK) {
    http.end();
    setDetail(detail, detailCapacity, status >= 300 && status < 400 ? "HTTPS_REDIRECT_REJECTED" : failure);
    return false;
  }
  const int declaredLength = http.getSize();
  if (declaredLength <= 0 || static_cast<std::size_t>(declaredLength) > capacity) {
    http.end();
    setDetail(detail, detailCapacity, "HTTPS_LENGTH_INVALID");
    return false;
  }
  NetworkClient* stream = http.getStreamPtr();
  const uint64_t startedAt = millis();
  uint64_t lastDataAt = startedAt;
  while (outputLength < static_cast<std::size_t>(declaredLength)) {
    const int available = stream->available();
    if (available > 0) {
      const std::size_t remaining = static_cast<std::size_t>(declaredLength) - outputLength;
      const std::size_t amount = static_cast<std::size_t>(available) < remaining
                                     ? static_cast<std::size_t>(available) : remaining;
      const int readCount = stream->read(output + outputLength, amount);
      if (readCount > 0) {
        outputLength += static_cast<std::size_t>(readCount);
        lastDataAt = millis();
      }
    } else if (!http.connected()) {
      break;
    }
    const uint64_t now = millis();
    if (now - lastDataAt > CT_OTA_NO_DATA_TIMEOUT_MS || now - startedAt > CT_OTA_TOTAL_TIMEOUT_MS) break;
    delay(1);
  }
  http.end();
  if (outputLength != static_cast<std::size_t>(declaredLength)) {
    setDetail(detail, detailCapacity, "HTTPS_BODY_INCOMPLETE");
    return false;
  }
  setDetail(detail, detailCapacity, "OK");
  return true;
}

bool readTag(const esp_partition_t* partition, std::size_t imageLength,
             const char* prefix, char* output, std::size_t capacity) {
  if (partition == nullptr || prefix == nullptr || output == nullptr || capacity < 2) return false;
  uint8_t buffer[512 + 48]{};
  std::size_t carry = 0;
  if (imageLength > partition->size) return false;
  for (std::size_t offset = 0; offset < imageLength; offset += 512U) {
    const std::size_t amount = imageLength - offset < 512U ? imageLength - offset : 512U;
    if (esp_partition_read(partition, offset, buffer + carry, amount) != ESP_OK) return false;
    const std::size_t length = carry + amount;
    buffer[length] = '\0';
    const std::size_t prefixLength = std::strlen(prefix);
    for (std::size_t index = 0; index + prefixLength < length; ++index) {
      if (std::memcmp(buffer + index, prefix, prefixLength) != 0) continue;
      const std::size_t valueStart = index + prefixLength;
      std::size_t end = valueStart;
      while (end < length && buffer[end] != '\0' && buffer[end] != '\n' && buffer[end] != '\r') ++end;
      if (end == length) return false;
      const std::size_t valueLength = end - valueStart;
      if (valueLength == 0 || valueLength >= capacity) return false;
      std::memcpy(output, buffer + valueStart, valueLength);
      output[valueLength] = '\0';
      return true;
    }
    carry = length < 47U ? length : 47U;
    std::memmove(buffer, buffer + length - carry, carry);
  }
  return false;
}

bool fetchManifest(OtaRequest& request, char* detail, std::size_t detailCapacity) {
  uint8_t payload[kManifestCapacity]{};
  std::size_t length = 0;
  if (!fetchHttps(request.url, payload, sizeof(payload), length, "MANIFEST_HTTP_FAILED",
                  detail, detailCapacity)) return false;
  OtaRequest resolved{};
  const char* parseDetail = nullptr;
  if (!otaParseManifest(payload, length, request, resolved, parseDetail)) {
    setDetail(detail, detailCapacity, parseDetail);
    return false;
  }
  request = resolved;
  return true;
}

bool prepare(void*, OtaRequest& request, OtaTargetInfo& target,
             char* detail, std::size_t detailCapacity) {
  if (CT_ROOT_CA[0] == '\0') { setDetail(detail, detailCapacity, "OTA_CA_UNCONFIGURED"); return false; }
  if (request.action == OtaAction::Manifest && !fetchManifest(request, detail, detailCapacity)) return false;
  const esp_partition_t* otaData = esp_partition_find_first(
      ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, nullptr);
  const esp_partition_t* running = esp_ota_get_running_partition();
  const esp_partition_t* next = esp_ota_get_next_update_partition(nullptr);
  if (otaData == nullptr || running == nullptr || next == nullptr) {
    setDetail(detail, detailCapacity, "OTA_PARTITION_LAYOUT_UNSUPPORTED");
    return false;
  }
  uint8_t appSlots = 0;
  esp_partition_iterator_t iterator = esp_partition_find(ESP_PARTITION_TYPE_APP,
                                                           ESP_PARTITION_SUBTYPE_ANY, nullptr);
  while (iterator != nullptr) {
    ++appSlots;
    iterator = esp_partition_next(iterator);
  }
  esp_partition_iterator_release(iterator);
  if (appSlots < 2 || next == running) { setDetail(detail, detailCapacity, "OTA_PARTITION_LAYOUT_UNSUPPORTED"); return false; }
  esp_app_desc_t runningDescription{};
  if (esp_ota_get_partition_description(running, &runningDescription) != ESP_OK) {
    setDetail(detail, detailCapacity, "RUNNING_IMAGE_METADATA_FAILED");
    return false;
  }
  target = {};
  target.hasOtaDataPartition = true;
  target.hasTwoAppSlots = true;
  target.rollbackSupported = esp_ota_check_rollback_is_possible();
  esp_partition_pos_t runningPosition{running->address, running->size};
  esp_image_metadata_t runningMetadata{};
  if (esp_image_verify(ESP_IMAGE_VERIFY, &runningPosition, &runningMetadata) != ESP_OK ||
      !readTag(running, runningMetadata.image_len, "CT_HUB_VER:", target.runningVersion,
               sizeof(target.runningVersion)) || !otaIsValidVersion(target.runningVersion) ||
      !readTag(running, runningMetadata.image_len, "CT_HUB_PRODUCT:", target.productId,
               sizeof(target.productId)) ||
      !readTag(running, runningMetadata.image_len, "CT_HUB_BOARD:", target.boardId,
               sizeof(target.boardId))) {
    setDetail(detail, detailCapacity, "RUNNING_IMAGE_IDENTITY_INVALID");
    return false;
  }
  std::snprintf(target.runningPartition, sizeof(target.runningPartition), "%s", running->label);
  std::snprintf(target.targetPartition, sizeof(target.targetPartition), "%s", next->label);
  if (std::strcmp(target.productId, CT_HUB_PRODUCT_ID) != 0 ||
      std::strcmp(target.boardId, CT_HUB_BOARD_ID) != 0) {
    setDetail(detail, detailCapacity, "RUNNING_IMAGE_IDENTITY_INVALID");
    return false;
  }
  if (request.action == OtaAction::Manifest) {
    std::snprintf(target.targetVersion, sizeof(target.targetVersion), "%s", request.version);
  }
  g_targetPartition = next;
  setDetail(detail, detailCapacity, "OK");
  return true;
}

bool downloadAndVerify(void*, const OtaRequest& request, OtaTargetInfo& target,
                       OtaProgressCallback progress, void* progressContext,
                       char* detail, std::size_t detailCapacity) {
  if (g_targetPartition == nullptr || CT_ROOT_CA[0] == '\0') {
    setDetail(detail, detailCapacity, "OTA_PLATFORM_NOT_READY");
    return false;
  }
  NetworkClientSecure secureClient;
  secureClient.setCACert(CT_ROOT_CA);
  HTTPClient http;
  http.setConnectTimeout(static_cast<int32_t>(kHttpConnectTimeoutMs));
  http.setTimeout(static_cast<uint16_t>(kHttpReadTimeoutMs));
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  if (!http.begin(secureClient, String(request.url))) { setDetail(detail, detailCapacity, "HTTPS_BEGIN_FAILED"); return false; }
  const int status = http.GET();
  if (status != HTTP_CODE_OK) {
    http.end();
    setDetail(detail, detailCapacity, status >= 300 && status < 400 ? "HTTPS_REDIRECT_REJECTED" : "FIRMWARE_HTTP_FAILED");
    return false;
  }
  const int contentLength = http.getSize();
  if (contentLength == 0 || contentLength > static_cast<int>(g_targetPartition->size) ||
      contentLength > static_cast<int>(kOtaImageLimitBytes) ||
      (contentLength > 0 && request.hasSizeBytes && static_cast<uint32_t>(contentLength) != request.sizeBytes)) {
    http.end();
    setDetail(detail, detailCapacity, "IMAGE_SIZE_INVALID");
    return false;
  }
  if (esp_ota_begin(g_targetPartition, contentLength > 0 ? static_cast<std::size_t>(contentLength) : OTA_SIZE_UNKNOWN,
                    &g_otaHandle) != ESP_OK) {
    http.end();
    setDetail(detail, detailCapacity, "OTA_BEGIN_FAILED");
    return false;
  }
  g_otaHandleOpen = true;
  mbedtls_md5_context md5{};
  mbedtls_md5_init(&md5);
  if (mbedtls_md5_starts(&md5) != 0) {
    mbedtls_md5_free(&md5);
    http.end();
    setDetail(detail, detailCapacity, "MD5_INIT_FAILED");
    return false;
  }
  NetworkClient* stream = http.getStreamPtr();
  uint8_t buffer[1024];
  uint32_t bytesWritten = 0;
  const uint64_t startedAt = millis();
  uint64_t lastByteAt = startedAt;
  bool transferOk = true;
  while (http.connected() || stream->available() > 0) {
    if (contentLength > 0 && bytesWritten >= static_cast<uint32_t>(contentLength)) break;
    const int available = stream->available();
    if (available > 0) {
      const std::size_t amount = static_cast<std::size_t>(available) < sizeof(buffer)
                                     ? static_cast<std::size_t>(available) : sizeof(buffer);
      const int readCount = stream->read(buffer, amount);
      if (readCount <= 0) {
        const uint64_t now = millis();
        if (now - lastByteAt > CT_OTA_NO_DATA_TIMEOUT_MS || now - startedAt > CT_OTA_TOTAL_TIMEOUT_MS) {
          transferOk = false;
          setDetail(detail, detailCapacity, now - lastByteAt > CT_OTA_NO_DATA_TIMEOUT_MS
                                              ? "OTA_NO_DATA_TIMEOUT" : "OTA_TOTAL_TIMEOUT");
          break;
        }
        delay(1);
        continue;
      }
      const std::size_t received = static_cast<std::size_t>(readCount);
      if (received > g_targetPartition->size - bytesWritten ||
          received > kOtaImageLimitBytes - bytesWritten ||
          (request.hasSizeBytes && (bytesWritten > request.sizeBytes ||
                                    received > request.sizeBytes - bytesWritten)) ||
          esp_ota_write(g_otaHandle, buffer, received) != ESP_OK ||
          mbedtls_md5_update(&md5, buffer, received) != 0) {
        transferOk = false;
        setDetail(detail, detailCapacity, "OTA_WRITE_FAILED");
        break;
      }
      bytesWritten += static_cast<uint32_t>(received);
      lastByteAt = millis();
      if (progress != nullptr) progress(progressContext, bytesWritten,
          contentLength > 0 ? static_cast<uint32_t>(contentLength) : 0U);
    } else {
      const uint64_t now = millis();
      if (now - lastByteAt > CT_OTA_NO_DATA_TIMEOUT_MS || now - startedAt > CT_OTA_TOTAL_TIMEOUT_MS) {
        transferOk = false;
        setDetail(detail, detailCapacity, now - lastByteAt > CT_OTA_NO_DATA_TIMEOUT_MS
                                            ? "OTA_NO_DATA_TIMEOUT" : "OTA_TOTAL_TIMEOUT");
        break;
      }
      delay(1);
    }
  }
  uint8_t digest[16]{};
  if (transferOk && mbedtls_md5_finish(&md5, digest) != 0) {
    transferOk = false;
    setDetail(detail, detailCapacity, "MD5_FINISH_FAILED");
  }
  mbedtls_md5_free(&md5);
  http.end();
  if (transferOk && (bytesWritten == 0 || (contentLength > 0 && bytesWritten != static_cast<uint32_t>(contentLength)) ||
                     (request.hasSizeBytes && bytesWritten != request.sizeBytes))) {
    transferOk = false;
    setDetail(detail, detailCapacity, "IMAGE_SIZE_MISMATCH");
  }
  static constexpr char kHex[] = "0123456789abcdef";
  char actualMd5[33]{};
  for (uint8_t index = 0; index < 16; ++index) {
    actualMd5[index * 2U] = kHex[digest[index] >> 4U];
    actualMd5[index * 2U + 1U] = kHex[digest[index] & 0x0fU];
  }
  bool md5Matches = true;
  if (transferOk) {
    for (uint8_t index = 0; index < 32; ++index) {
      char expected = request.md5[index];
      if (expected >= 'A' && expected <= 'F') expected = static_cast<char>(expected - 'A' + 'a');
      if (actualMd5[index] != expected) { md5Matches = false; break; }
    }
  }
  if (transferOk && !md5Matches) {
    transferOk = false;
    setDetail(detail, detailCapacity, "MD5_MISMATCH");
  }
  if (transferOk) {
    const esp_err_t endResult = esp_ota_end(g_otaHandle);
    g_otaHandleOpen = false;
    if (endResult != ESP_OK) {
      transferOk = false;
      setDetail(detail, detailCapacity, "OTA_IMAGE_INVALID");
    }
  }
  if (!transferOk) {
    if (g_otaHandleOpen) {
      (void)esp_ota_abort(g_otaHandle);
      g_otaHandleOpen = false;
    }
    return false;
  }
  esp_partition_pos_t position{g_targetPartition->address, g_targetPartition->size};
  esp_image_metadata_t metadata{};
  if (esp_image_verify(ESP_IMAGE_VERIFY, &position, &metadata) != ESP_OK ||
      metadata.image_len != bytesWritten ||
      !readTag(g_targetPartition, bytesWritten, "CT_HUB_PRODUCT:", target.productId, sizeof(target.productId)) ||
      !readTag(g_targetPartition, bytesWritten, "CT_HUB_BOARD:", target.boardId, sizeof(target.boardId)) ||
      !readTag(g_targetPartition, bytesWritten, "CT_HUB_VER:", target.targetVersion, sizeof(target.targetVersion))) {
    setDetail(detail, detailCapacity, "IMAGE_IDENTITY_TAG_INVALID");
    return false;
  }
  if (std::strcmp(target.productId, CT_HUB_PRODUCT_ID) != 0 ||
      std::strcmp(target.boardId, CT_HUB_BOARD_ID) != 0 || !otaIsValidVersion(target.targetVersion) ||
      (request.action == OtaAction::Manifest && std::strcmp(request.version, target.targetVersion) != 0)) {
    setDetail(detail, detailCapacity, "TARGET_IDENTITY_MISMATCH");
    return false;
  }
  target.imageSize = bytesWritten;
  setDetail(detail, detailCapacity, "OK");
  return true;
}

bool selectBootPartition(void*, const char* label) {
  if (label == nullptr || *label == '\0') return false;
  const esp_partition_t* partition = esp_partition_find_first(ESP_PARTITION_TYPE_APP,
                                                               ESP_PARTITION_SUBTYPE_ANY, label);
  return partition != nullptr && esp_ota_set_boot_partition(partition) == ESP_OK;
}

bool abortUpdate(void*) {
  if (!g_otaHandleOpen) return true;
  const esp_err_t result = esp_ota_abort(g_otaHandle);
  g_otaHandleOpen = false;
  return result == ESP_OK;
}

void restart(void*) { esp_restart(); }

bool markRunningImageValid(void*) {
  const esp_partition_t* running = esp_ota_get_running_partition();
  esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
  if (running != nullptr && esp_ota_get_state_partition(running, &state) == ESP_OK &&
      state == ESP_OTA_IMG_VALID) return true;
#if defined(CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE) && CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
  return esp_ota_mark_app_valid_cancel_rollback() == ESP_OK;
#else
  return true;
#endif
}

bool readRunningImage(void*, OtaTargetInfo& image) {
  const esp_partition_t* running = esp_ota_get_running_partition();
  esp_app_desc_t description{};
  if (running == nullptr || esp_ota_get_partition_description(running, &description) != ESP_OK) return false;
  image = {};
  esp_partition_pos_t position{running->address, running->size};
  esp_image_metadata_t metadata{};
  if (esp_image_verify(ESP_IMAGE_VERIFY, &position, &metadata) != ESP_OK ||
      !readTag(running, metadata.image_len, "CT_HUB_VER:", image.runningVersion,
               sizeof(image.runningVersion)) ||
      !readTag(running, metadata.image_len, "CT_HUB_PRODUCT:", image.productId,
               sizeof(image.productId)) ||
      !readTag(running, metadata.image_len, "CT_HUB_BOARD:", image.boardId,
               sizeof(image.boardId))) return false;
  std::snprintf(image.runningPartition, sizeof(image.runningPartition), "%s", running->label);
  return true;
}
}

OtaPlatformAdapter otaPlatformCreateAdapter() {
  return {nullptr, prepare, downloadAndVerify, selectBootPartition,
          abortUpdate, restart, markRunningImageValid, readRunningImage};
}
