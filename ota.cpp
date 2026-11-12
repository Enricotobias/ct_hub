#include "ota.h"

#include "debug_log.h"
#include "config.h"
#include "ota_store.h"
#include "runtime_state.h"
#include "task_network.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <Arduino.h>

namespace {
constexpr std::size_t kOtaPayloadLimit = 2048;

enum class JsonValueType : uint8_t { String, Number, Boolean, Null, Other };

struct OtaFields {
  char action[16]{};
  char requestId[OTA_REQUEST_ID_CAPACITY]{};
  char manifestUrl[OTA_URL_CAPACITY]{};
  char binaryUrl[OTA_URL_CAPACITY]{};
  char downloadUrl[OTA_URL_CAPACITY]{};
  char url[OTA_URL_CAPACITY]{};
  char md5[OTA_MD5_CAPACITY]{};
  char version[OTA_VERSION_CAPACITY]{};
  char productId[OTA_PRODUCT_CAPACITY]{};
  char boardId[OTA_BOARD_CAPACITY]{};
  uint64_t sizeBytes = 0;
  bool hasSizeBytes = false;
  bool hasConfirm = false;
  bool confirm = false;
  bool hasAction = false;
  bool hasRequestId = false;
  bool hasManifestUrl = false;
  bool hasBinaryUrl = false;
  bool hasDownloadUrl = false;
  bool hasUrl = false;
  bool hasMd5 = false;
  bool hasVersion = false;
  bool hasProductId = false;
  bool hasBoardId = false;
};

bool isHex(char value) {
  return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f') ||
         (value >= 'A' && value <= 'F');
}

bool equalIgnoreCase(const char* left, const char* right) {
  if (left == nullptr || right == nullptr) return false;
  while (*left != '\0' && *right != '\0') {
    char a = *left++;
    char b = *right++;
    if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
    if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
    if (a != b) return false;
  }
  return *left == '\0' && *right == '\0';
}

bool copyText(char* output, std::size_t capacity, const char* value) {
  if (output == nullptr || capacity == 0 || value == nullptr) return false;
  const std::size_t length = std::strlen(value);
  if (length == 0 || length >= capacity) return false;
  std::memcpy(output, value, length + 1U);
  return true;
}

class JsonScanner {
 public:
  JsonScanner(const uint8_t* input, std::size_t size) : input_(input), size_(size) {}

  bool parse(OtaFields& fields) {
    skipWhitespace();
    if (!consume('{')) return false;
    skipWhitespace();
    if (consume('}')) return atEnd();
    for (;;) {
      char key[40]{};
      if (!parseString(key, sizeof(key))) return false;
      skipWhitespace();
      if (!consume(':')) return false;
      skipWhitespace();
      if (!parseField(key, fields)) return false;
      skipWhitespace();
      if (consume('}')) return atEnd();
      if (!consume(',')) return false;
      skipWhitespace();
    }
  }

 private:
  const uint8_t* input_;
  std::size_t size_;
  std::size_t position_ = 0;

  bool atEnd() {
    skipWhitespace();
    return position_ == size_;
  }

  void skipWhitespace() {
    while (position_ < size_ && (input_[position_] == ' ' || input_[position_] == '\t' ||
           input_[position_] == '\r' || input_[position_] == '\n')) ++position_;
  }

  bool consume(char expected) {
    if (position_ >= size_ || input_[position_] != static_cast<uint8_t>(expected)) return false;
    ++position_;
    return true;
  }

  bool parseString(char* output, std::size_t capacity) {
    if (!consume('"')) return false;
    std::size_t written = 0;
    while (position_ < size_) {
      uint8_t value = input_[position_++];
      if (value == '"') {
        if (output != nullptr) {
          if (written >= capacity) return false;
          output[written] = '\0';
        }
        return true;
      }
      if (value < 0x20U) return false;
      if (value == '\\') {
        if (position_ >= size_) return false;
        const uint8_t escaped = input_[position_++];
        if (escaped == '"' || escaped == '\\' || escaped == '/') value = escaped;
        else if (escaped == 'b') value = '\b';
        else if (escaped == 'f') value = '\f';
        else if (escaped == 'n') value = '\n';
        else if (escaped == 'r') value = '\r';
        else if (escaped == 't') value = '\t';
        else if (escaped == 'u') {
          if (position_ + 4U > size_) return false;
          uint32_t codepoint = 0;
          for (uint8_t digit = 0; digit < 4; ++digit) {
            const char hex = static_cast<char>(input_[position_++]);
            if (!isHex(hex)) return false;
            codepoint = (codepoint << 4U) | static_cast<uint32_t>(hex <= '9' ? hex - '0' :
                        (hex >= 'a' ? hex - 'a' + 10 : hex - 'A' + 10));
          }
          if (codepoint > 0x7fU) return false;
          value = static_cast<uint8_t>(codepoint);
        } else return false;
      }
      if (output != nullptr) {
        if (written + 1U >= capacity) return false;
        output[written] = static_cast<char>(value);
      }
      ++written;
    }
    return false;
  }

  bool parseUnsigned(uint64_t& output) {
    const std::size_t start = position_;
    if (position_ >= size_ || input_[position_] < '0' || input_[position_] > '9') return false;
    if (input_[position_] == '0') ++position_;
    else while (position_ < size_ && input_[position_] >= '0' && input_[position_] <= '9') ++position_;
    if (position_ < size_ && (input_[position_] == '.' || input_[position_] == 'e' || input_[position_] == 'E')) return false;
    char buffer[24]{};
    const std::size_t length = position_ - start;
    if (length >= sizeof(buffer)) return false;
    std::memcpy(buffer, input_ + start, length);
    char* end = nullptr;
    errno = 0;
    output = std::strtoull(buffer, &end, 10);
    return errno != ERANGE && end != buffer && *end == '\0';
  }

  bool skipValue(uint8_t depth = 0) {
    if (depth > 8 || position_ >= size_) return false;
    if (input_[position_] == '"') return parseString(nullptr, 0);
    if (input_[position_] == '{') {
      ++position_;
      skipWhitespace();
      if (consume('}')) return true;
      for (;;) {
        if (!parseString(nullptr, 0)) return false;
        skipWhitespace();
        if (!consume(':')) return false;
        skipWhitespace();
        if (!skipValue(static_cast<uint8_t>(depth + 1U))) return false;
        skipWhitespace();
        if (consume('}')) return true;
        if (!consume(',')) return false;
        skipWhitespace();
      }
    }
    if (input_[position_] == '[') {
      ++position_;
      skipWhitespace();
      if (consume(']')) return true;
      for (;;) {
        if (!skipValue(static_cast<uint8_t>(depth + 1U))) return false;
        skipWhitespace();
        if (consume(']')) return true;
        if (!consume(',')) return false;
        skipWhitespace();
      }
    }
    const std::size_t start = position_;
    if (position_ < size_ && input_[position_] == '-') ++position_;
    if (position_ >= size_) return false;
    if (input_[position_] == '0') {
      ++position_;
      if (position_ < size_ && input_[position_] >= '0' && input_[position_] <= '9') return false;
    } else if (input_[position_] >= '1' && input_[position_] <= '9') {
      while (position_ < size_ && input_[position_] >= '0' && input_[position_] <= '9') ++position_;
    } else {
      position_ = start;
      const char* literals[] = {"true", "false", "null"};
      const std::size_t lengths[] = {4, 5, 4};
      for (uint8_t index = 0; index < 3; ++index) {
        if (position_ + lengths[index] <= size_ &&
            std::memcmp(input_ + position_, literals[index], lengths[index]) == 0) {
          position_ += lengths[index];
          return true;
        }
      }
      return false;
    }
    if (position_ < size_ && input_[position_] == '.') {
      ++position_;
      const std::size_t fractionStart = position_;
      while (position_ < size_ && input_[position_] >= '0' && input_[position_] <= '9') ++position_;
      if (position_ == fractionStart) return false;
    }
    if (position_ < size_ && (input_[position_] == 'e' || input_[position_] == 'E')) {
      ++position_;
      if (position_ < size_ && (input_[position_] == '+' || input_[position_] == '-')) ++position_;
      const std::size_t exponentStart = position_;
      while (position_ < size_ && input_[position_] >= '0' && input_[position_] <= '9') ++position_;
      if (position_ == exponentStart) return false;
    }
    return position_ > start;
  }

  bool parseStringField(char* output, std::size_t capacity, bool& present) {
    if (present) return false;
    if (!parseString(output, capacity)) return false;
    present = true;
    return true;
  }

  bool parseField(const char* key, OtaFields& fields) {
    char* destination = nullptr;
    std::size_t capacity = 0;
    bool* present = nullptr;
    if (std::strcmp(key, "action") == 0) { destination = fields.action; capacity = sizeof(fields.action); present = &fields.hasAction; }
    else if (std::strcmp(key, "request_id") == 0) { destination = fields.requestId; capacity = sizeof(fields.requestId); present = &fields.hasRequestId; }
    else if (std::strcmp(key, "manifest_url") == 0) { destination = fields.manifestUrl; capacity = sizeof(fields.manifestUrl); present = &fields.hasManifestUrl; }
    else if (std::strcmp(key, "binary_url") == 0) { destination = fields.binaryUrl; capacity = sizeof(fields.binaryUrl); present = &fields.hasBinaryUrl; }
    else if (std::strcmp(key, "download_url") == 0) { destination = fields.downloadUrl; capacity = sizeof(fields.downloadUrl); present = &fields.hasDownloadUrl; }
    else if (std::strcmp(key, "url") == 0) { destination = fields.url; capacity = sizeof(fields.url); present = &fields.hasUrl; }
    else if (std::strcmp(key, "md5") == 0) { destination = fields.md5; capacity = sizeof(fields.md5); present = &fields.hasMd5; }
    else if (std::strcmp(key, "version") == 0) { destination = fields.version; capacity = sizeof(fields.version); present = &fields.hasVersion; }
    else if (std::strcmp(key, "product_id") == 0) { destination = fields.productId; capacity = sizeof(fields.productId); present = &fields.hasProductId; }
    else if (std::strcmp(key, "board_id") == 0) { destination = fields.boardId; capacity = sizeof(fields.boardId); present = &fields.hasBoardId; }

    if (destination != nullptr) return parseStringField(destination, capacity, *present);
    if (std::strcmp(key, "size_bytes") == 0) {
      uint64_t value = 0;
      if (!parseUnsigned(value)) return false;
      if (fields.hasSizeBytes) return false;
      fields.sizeBytes = value;
      fields.hasSizeBytes = true;
      return true;
    }
    if (std::strcmp(key, "confirm") == 0) {
      if (fields.hasConfirm) return false;
      if (position_ + 4U <= size_ && std::memcmp(input_ + position_, "true", 4) == 0) {
        fields.confirm = true;
        position_ += 4;
      } else if (position_ + 5U <= size_ && std::memcmp(input_ + position_, "false", 5) == 0) {
        fields.confirm = false;
        position_ += 5;
      } else return false;
      fields.hasConfirm = true;
      return true;
    }
    return skipValue();
  }
};

bool parseObject(const uint8_t* payload, std::size_t length, OtaFields& fields,
                 std::size_t maximumLength = kOtaPayloadLimit) {
  if (payload == nullptr || length == 0 || length > maximumLength) return false;
  return JsonScanner(payload, length).parse(fields);
}

bool chooseAlias(const char* first, bool hasFirst, const char* second, bool hasSecond,
                 const char* third, bool hasThird, char* output, std::size_t capacity,
                 const char*& detail) {
  const char* selected = nullptr;
  const char* values[] = {first, second, third};
  const bool present[] = {hasFirst, hasSecond, hasThird};
  for (uint8_t index = 0; index < 3; ++index) {
    if (!present[index]) continue;
    if (selected != nullptr && std::strcmp(selected, values[index]) != 0) {
      detail = "CONFLICTING_URL_FIELDS";
      return false;
    }
    selected = values[index];
  }
  if (selected == nullptr || !copyText(output, capacity, selected)) {
    detail = "URL_REQUIRED_OR_TOO_LONG";
    return false;
  }
  return true;
}

uint64_t calculatePayloadDigest(const uint8_t* payload, std::size_t length) {
  uint64_t digest = 14695981039346656037ULL;
  for (std::size_t index = 0; index < length; ++index) {
    digest ^= payload[index];
    digest *= 1099511628211ULL;
  }
  return digest;
}

bool copyManifestField(char* destination, std::size_t capacity, const char* value,
                       bool present, const char* missingDetail, const char*& detail) {
  if (!present || !copyText(destination, capacity, value)) {
    detail = missingDetail;
    return false;
  }
  return true;
}
}

bool otaIsValidHttpsUrl(const char* url) {
  if (url == nullptr) return false;
  const std::size_t length = std::strlen(url);
  if (length < 9 || length >= OTA_URL_CAPACITY) return false;
  const char* schemeEnd = std::strstr(url, "://");
  if (schemeEnd == nullptr || static_cast<std::size_t>(schemeEnd - url) != 5) return false;
  const char scheme[6] = {url[0], url[1], url[2], url[3], url[4], '\0'};
  if (!equalIgnoreCase(scheme, "https")) return false;
  const char* host = url + 8;
  if (*host == '\0' || *host == '/' || *host == '?' || *host == '#') return false;
  const char* hostEnd = host;
  while (*hostEnd != '\0' && *hostEnd != '/' && *hostEnd != '?' && *hostEnd != '#') {
    if (*hostEnd == '@' || *hostEnd <= 0x20 || *hostEnd == '\\') return false;
    if (static_cast<unsigned char>(*hostEnd) >= 0x7fU) return false;
    ++hostEnd;
  }
  if (hostEnd == host || std::strchr(url, '#') != nullptr) return false;
  const char* colon = nullptr;
  for (const char* cursor = host; cursor < hostEnd; ++cursor) {
    if (*cursor == ':') {
      if (colon != nullptr) return false;
      colon = cursor;
    }
  }
  const char* hostnameEnd = colon == nullptr ? hostEnd : colon;
  if (hostnameEnd == host || host[0] == '.' || host[0] == '-' ||
      hostnameEnd[-1] == '.' || hostnameEnd[-1] == '-') return false;
  for (const char* cursor = host; cursor < hostnameEnd; ++cursor) {
    const char value = *cursor;
    if (!((value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z') ||
          (value >= '0' && value <= '9') || value == '.' || value == '-')) return false;
  }
  if (colon != nullptr) {
    if (colon + 1 == hostEnd) return false;
    uint32_t port = 0;
    for (const char* cursor = colon + 1; cursor < hostEnd; ++cursor) {
      if (*cursor < '0' || *cursor > '9') return false;
      const uint32_t digit = static_cast<uint32_t>(*cursor - '0');
      if (port > (65535U - digit) / 10U) return false;
      port = port * 10U + digit;
    }
    if (port == 0) return false;
  }
  for (const char* cursor = url; *cursor != '\0'; ++cursor) {
    if (static_cast<unsigned char>(*cursor) < 0x20U ||
        static_cast<unsigned char>(*cursor) >= 0x7fU || *cursor == ' ' || *cursor == '\\') return false;
  }
  return true;
}

bool otaIsValidMd5(const char* md5) {
  if (md5 == nullptr || std::strlen(md5) != 32) return false;
  for (uint8_t index = 0; index < 32; ++index) if (!isHex(md5[index])) return false;
  return true;
}

bool otaIsValidVersion(const char* version) {
  if (version == nullptr) return false;
  if (std::strlen(version) >= OTA_VERSION_CAPACITY) return false;
  uint8_t components = 0;
  bool hasDigit = false;
  uint64_t componentValue = 0;
  for (const char* cursor = version; ; ++cursor) {
    if (*cursor >= '0' && *cursor <= '9') {
      hasDigit = true;
      const uint64_t digit = static_cast<uint64_t>(*cursor - '0');
      if (componentValue > (static_cast<uint64_t>(UINT32_MAX) - digit) / 10U) return false;
      componentValue = componentValue * 10U + digit;
    }
    else if (*cursor == '.' && hasDigit && components < 2) {
      ++components;
      hasDigit = false;
      componentValue = 0;
    }
    else if (*cursor == '\0') return components == 2 && hasDigit;
    else return false;
  }
}

int otaCompareVersions(const char* left, const char* right) {
  if (!otaIsValidVersion(left) || !otaIsValidVersion(right)) return 0;
  const char* a = left;
  const char* b = right;
  for (uint8_t component = 0; component < 3; ++component) {
    char* endA = nullptr;
    char* endB = nullptr;
    const unsigned long valueA = std::strtoul(a, &endA, 10);
    const unsigned long valueB = std::strtoul(b, &endB, 10);
    if (valueA < valueB) return -1;
    if (valueA > valueB) return 1;
    a = *endA == '.' ? endA + 1 : endA;
    b = *endB == '.' ? endB + 1 : endB;
  }
  return 0;
}

OtaParseResult otaParseRequest(const uint8_t* payload, std::size_t length) {
  OtaParseResult result{};
  result.detail = "MALFORMED_JSON";
  if (payload == nullptr || length == 0 || length > kOtaPayloadLimit) {
    result.detail = "PAYLOAD_TOO_LARGE";
    return result;
  }
  OtaFields fields{};
  if (!parseObject(payload, length, fields)) return result;
  OtaRequest& request = result.request;
  if (fields.hasRequestId && !copyText(request.requestId, sizeof(request.requestId), fields.requestId)) {
    result.detail = "REQUEST_ID_TOO_LONG";
    return result;
  }
  if (fields.hasSizeBytes && (fields.sizeBytes == 0 || fields.sizeBytes > 0xffffffffULL)) {
    result.detail = "INVALID_SIZE_BYTES";
    return result;
  }

  request.action = OtaAction::Manifest;
  if (fields.hasAction) {
    if (equalIgnoreCase(fields.action, "manifest")) request.action = OtaAction::Manifest;
    else if (equalIgnoreCase(fields.action, "direct")) request.action = OtaAction::Direct;
    else if (equalIgnoreCase(fields.action, "rollback")) request.action = OtaAction::Rollback;
    else { result.detail = "UNSUPPORTED_ACTION"; return result; }
  }
  request.hasSizeBytes = fields.hasSizeBytes;
  request.sizeBytes = static_cast<uint32_t>(fields.sizeBytes);
  request.payloadDigest = calculatePayloadDigest(payload, length);

  if (request.action == OtaAction::Manifest) {
    if (!chooseAlias(fields.manifestUrl, fields.hasManifestUrl, fields.url, fields.hasUrl,
                     nullptr, false, request.url, sizeof(request.url), result.detail)) return result;
  } else if (request.action == OtaAction::Direct) {
    if (!chooseAlias(fields.binaryUrl, fields.hasBinaryUrl, fields.downloadUrl, fields.hasDownloadUrl,
                     fields.url, fields.hasUrl, request.url, sizeof(request.url), result.detail)) return result;
    if (!fields.hasMd5 || !copyText(request.md5, sizeof(request.md5), fields.md5) ||
        !otaIsValidMd5(request.md5)) { result.detail = "INVALID_MD5"; return result; }
  } else {
    if (!fields.hasConfirm || !fields.confirm) { result.detail = "ROLLBACK_CONFIRM_REQUIRED"; return result; }
    request.rollbackConfirmed = true;
  }
  if (request.action != OtaAction::Rollback && !otaIsValidHttpsUrl(request.url)) {
    result.detail = "HTTPS_URL_REQUIRED";
    return result;
  }
  result.accepted = true;
  result.detail = "ACCEPTED";
  return result;
}

bool otaParseManifest(const uint8_t* payload, std::size_t length,
                      const OtaRequest& envelope, OtaRequest& resolved,
                      const char*& detail) {
  detail = "MALFORMED_MANIFEST";
  if (payload == nullptr || length == 0 || length > 4096) return false;
  resolved = envelope;
  OtaFields fields{};
  if (!parseObject(payload, length, fields, 4096)) return false;
  if (!copyManifestField(resolved.version, sizeof(resolved.version), fields.version,
                         fields.hasVersion, "MANIFEST_VERSION_REQUIRED", detail) ||
      !copyManifestField(resolved.productId, sizeof(resolved.productId), fields.productId,
                         fields.hasProductId, "MANIFEST_PRODUCT_REQUIRED", detail) ||
      !copyManifestField(resolved.boardId, sizeof(resolved.boardId), fields.boardId,
                         fields.hasBoardId, "MANIFEST_BOARD_REQUIRED", detail) ||
      !copyManifestField(resolved.md5, sizeof(resolved.md5), fields.md5,
                         fields.hasMd5, "MANIFEST_MD5_REQUIRED", detail)) return false;
  if (!chooseAlias(fields.url, fields.hasUrl, nullptr, false, nullptr, false,
                   resolved.url, sizeof(resolved.url), detail)) return false;
  if (!otaIsValidVersion(resolved.version)) { detail = "INVALID_VERSION"; return false; }
  if (!otaIsValidMd5(resolved.md5)) { detail = "INVALID_MD5"; return false; }
  if (!otaIsValidHttpsUrl(resolved.url)) { detail = "HTTPS_URL_REQUIRED"; return false; }
  if (std::strcmp(resolved.productId, CT_HUB_PRODUCT_ID) != 0 ||
      std::strcmp(resolved.boardId, CT_HUB_BOARD_ID) != 0) {
    detail = "TARGET_IDENTITY_MISMATCH";
    return false;
  }
  if (fields.hasSizeBytes && (fields.sizeBytes == 0 || fields.sizeBytes > 0xffffffffULL)) {
    detail = "INVALID_SIZE_BYTES";
    return false;
  }
  resolved.action = OtaAction::Manifest;
  std::snprintf(resolved.requestId, sizeof(resolved.requestId), "%s", envelope.requestId);
  resolved.payloadDigest = envelope.payloadDigest;
  resolved.hasSizeBytes = fields.hasSizeBytes;
  resolved.sizeBytes = static_cast<uint32_t>(fields.sizeBytes);
  resolved.manifestResolved = true;
  detail = "MANIFEST_VALID";
  return true;
}

namespace {
OtaPlatformAdapter g_platform{};
OtaPersistenceAdapter g_persistence{};
bool g_hasPersistenceAdapter = false;
OtaPersistentRecord g_persistentRecord{};
OtaRequest g_activeRequest{};
OtaTargetInfo g_targetInfo{};
OtaPhase g_phase = OtaPhase::Idle;
uint32_t g_maintenanceToken = 0;
uint32_t g_nextToken = 1;
uint32_t g_downloadedBytes = 0;
uint32_t g_totalBytes = 0;
uint64_t g_lastProgressAtMs = 0;
uint64_t g_startedAtMs = 0;
uint64_t g_phaseStartedAtMs = 0;
bool g_sensorPaused = false;
bool g_networkRequested = false;
bool g_networkGranted = false;
bool g_updateStarted = false;
bool g_runningImageCheckKnown = false;
bool g_runningImageMatchesExpected = false;
char g_failureStatus[16]{};
char g_failureDetail[48]{};

uint64_t otaNowMs() {
  return millis();
}

const char* phaseName(OtaPhase phase) {
  switch (phase) {
    case OtaPhase::Idle: return "idle";
    case OtaPhase::Validating: return "validating";
    case OtaPhase::WaitingSensorPause: return "waiting_sensor_pause";
    case OtaPhase::WaitingNetworkMaintenance: return "waiting_network_maintenance";
    case OtaPhase::Preparing: return "preparing";
    case OtaPhase::Downloading: return "downloading";
    case OtaPhase::Verifying: return "verifying";
    case OtaPhase::PersistingReboot: return "persisting_reboot";
    case OtaPhase::AwaitingReboot: return "awaiting_reboot";
    case OtaPhase::AwaitingHealthValidation: return "awaiting_health_validation";
    case OtaPhase::SuccessPendingPublish: return "success_pending_publish";
    case OtaPhase::CleaningUp: return "cleaning_up";
    case OtaPhase::Resuming: return "resuming";
    case OtaPhase::RollbackPending: return "rollback_pending";
    case OtaPhase::FailedPendingPublish: return "failed_pending_publish";
  }
  return "unknown";
}

void changePhase(OtaPhase phase, uint64_t nowMonoMs) {
  const OtaPhase previousPhase = g_phase;
  g_phase = phase;
  g_phaseStartedAtMs = nowMonoMs;
  runtimeMarkOtaProgress(static_cast<uint8_t>(phase), g_downloadedBytes, nowMonoMs);
  if (previousPhase != phase) {
    ctDebugLog("INFO", "OTA", "phase %s -> %s", phaseName(previousPhase), phaseName(phase));
  }
}

void publishOtaEvent(const char* status, const char* detail, uint64_t nowMonoMs) {
  RuntimeEvent event{};
  event.type = RuntimeEventType::OtaFinished;
  event.token = g_maintenanceToken;
  event.monotonicMs = nowMonoMs;
  std::snprintf(event.requestId, sizeof(event.requestId), "%s", g_activeRequest.requestId);
  std::snprintf(event.command, sizeof(event.command), "update");
  std::snprintf(event.status, sizeof(event.status), "%s", status == nullptr ? "failed" : status);
  std::snprintf(event.detail, sizeof(event.detail), "%s", detail == nullptr ? "UNKNOWN" : detail);
  (void)runtimePostOtaResult(event);
}

bool commitCandidate(OtaPersistentRecord& candidate) {
  if (!otaStoreCommit(candidate, g_hasPersistenceAdapter ? &g_persistence : nullptr)) return false;
  g_persistentRecord = candidate;
  return true;
}

bool persistFinalResult(const OtaRequest& request, const char* status, const char* detail) {
  OtaPersistentRecord candidate = g_persistentRecord;
  otaStoreRecordFinal(candidate, request, status, detail);
  candidate.phase = OtaStoredPhase::FinalPendingPublish;
  candidate.expectedReboot = false;
  candidate.activeRequestId[0] = '\0';
  candidate.activePayloadDigest = 0;
  return commitCandidate(candidate);
}

void beginFailure(const char* status, const char* detail, uint64_t nowMonoMs) {
  if (g_phase == OtaPhase::CleaningUp || g_phase == OtaPhase::Resuming ||
      g_phase == OtaPhase::FailedPendingPublish) return;
  std::snprintf(g_failureStatus, sizeof(g_failureStatus), "%s", status == nullptr ? "failed" : status);
  std::snprintf(g_failureDetail, sizeof(g_failureDetail), "%s", detail == nullptr ? "UNKNOWN" : detail);
  ctDebugLog("ERROR", "OTA", "request failed; status=%s detail=%s",
             g_failureStatus, g_failureDetail);
  changePhase(OtaPhase::CleaningUp, nowMonoMs);
}

void finalizeFailure(uint64_t nowMonoMs) {
  const bool saved = persistFinalResult(g_activeRequest, g_failureStatus, g_failureDetail);
  changePhase(OtaPhase::FailedPendingPublish, nowMonoMs);
  publishOtaEvent(saved ? g_failureStatus : "failed",
                  saved ? g_failureDetail : "OTA_RESULT_PERSIST_FAILED", nowMonoMs);
}

void otaProgress(void*, uint32_t bytes, uint32_t totalBytes) {
  const uint64_t now = otaNowMs();
  g_downloadedBytes = bytes;
  g_totalBytes = totalBytes;
  if (bytes > 0) runtimeMarkOtaProgress(static_cast<uint8_t>(OtaPhase::Downloading), bytes, now);
  if (g_lastProgressAtMs != 0 && now - g_lastProgressAtMs < CT_OTA_PROGRESS_INTERVAL_MS) return;
  g_lastProgressAtMs = now;
  RuntimeEvent event{};
  event.type = RuntimeEventType::OtaProgress;
  event.monotonicMs = now;
  event.progressPercent = totalBytes == 0 ? 0 : static_cast<uint8_t>(
      (static_cast<uint64_t>(bytes) * 100U) / totalBytes);
  std::snprintf(event.requestId, sizeof(event.requestId), "%s", g_activeRequest.requestId);
  std::snprintf(event.command, sizeof(event.command), "update");
  std::snprintf(event.status, sizeof(event.status), "in_progress");
  std::snprintf(event.detail, sizeof(event.detail), "download=%lu/%lu",
                static_cast<unsigned long>(bytes), static_cast<unsigned long>(totalBytes));
  (void)runtimePostOtaProgress(event);
}

void clearCurrentRequest() {
  g_activeRequest = {};
  g_targetInfo = {};
  g_maintenanceToken = 0;
  g_downloadedBytes = 0;
  g_totalBytes = 0;
  g_lastProgressAtMs = 0;
  g_startedAtMs = 0;
  g_phaseStartedAtMs = 0;
  g_sensorPaused = false;
  g_networkRequested = false;
  g_networkGranted = false;
  g_updateStarted = false;
  g_failureStatus[0] = '\0';
  g_failureDetail[0] = '\0';
}
}

void otaConfigure(const OtaPlatformAdapter& platform, const OtaPersistenceAdapter* persistence) {
  g_platform = platform;
  g_hasPersistenceAdapter = persistence != nullptr && persistence->load != nullptr && persistence->save != nullptr;
  g_persistence = g_hasPersistenceAdapter ? *persistence : OtaPersistenceAdapter{};
  g_runningImageCheckKnown = false;
  g_runningImageMatchesExpected = false;
  (void)otaStoreLoad(g_persistentRecord, g_hasPersistenceAdapter ? &g_persistence : nullptr);
  if (g_persistentRecord.phase == OtaStoredPhase::Active ||
      g_persistentRecord.phase == OtaStoredPhase::AwaitingHealthValidation) {
    OtaRequest interrupted{};
    interrupted.action = g_persistentRecord.activeAction;
    interrupted.payloadDigest = g_persistentRecord.activePayloadDigest;
    std::snprintf(interrupted.requestId, sizeof(interrupted.requestId), "%s",
                  g_persistentRecord.activeRequestId);
    OtaTargetInfo running{};
    const bool runningReadable = g_persistentRecord.expectedReboot &&
        g_platform.readRunningImage != nullptr &&
        g_platform.readRunningImage(g_platform.context, running);
    const bool runningMatchesTarget = runningReadable &&
        std::strcmp(running.runningVersion, g_persistentRecord.targetVersion) == 0;
    g_runningImageCheckKnown = runningReadable;
    g_runningImageMatchesExpected = runningMatchesTarget &&
        std::strcmp(running.productId, CT_HUB_PRODUCT_ID) == 0 &&
        std::strcmp(running.boardId, CT_HUB_BOARD_ID) == 0;
    if (g_persistentRecord.phase == OtaStoredPhase::Active) {
      (void)persistFinalResult(interrupted, "failed", "OTA_INTERRUPTED");
      g_phase = OtaPhase::Idle;
    } else if (!runningReadable) {
      g_phase = OtaPhase::AwaitingHealthValidation;
    } else if (!runningMatchesTarget) {
      (void)persistFinalResult(interrupted, "failed", "OTA_ROLLED_BACK");
      g_phase = OtaPhase::Idle;
    } else if (g_persistentRecord.validationBootAttempts > 0) {
      if (g_persistentRecord.rollbackSupported && g_persistentRecord.sourcePartition[0] != '\0' &&
          g_platform.selectBootPartition != nullptr &&
          g_platform.selectBootPartition(g_platform.context, g_persistentRecord.sourcePartition)) {
        if (g_platform.restart != nullptr) g_platform.restart(g_platform.context);
      }
      (void)persistFinalResult(interrupted, "failed", "OTA_HEALTH_GATE_REBOOT_LIMIT");
      g_phase = OtaPhase::Idle;
    } else {
      OtaPersistentRecord candidate = g_persistentRecord;
      candidate.validationBootAttempts = 1;
      (void)commitCandidate(candidate);
      g_phase = OtaPhase::AwaitingHealthValidation;
    }
  } else if (g_persistentRecord.phase == OtaStoredPhase::FinalPendingPublish &&
             g_persistentRecord.completedCount > 0) {
    g_phase = OtaPhase::Idle;
  } else {
    g_phase = OtaPhase::Idle;
  }
  if (g_persistentRecord.phase == OtaStoredPhase::FinalPendingPublish &&
      g_persistentRecord.completedCount > 0) {
    const uint8_t index = static_cast<uint8_t>(
        (g_persistentRecord.completedNext + OTA_COMPLETED_HISTORY_SIZE - 1U) % OTA_COMPLETED_HISTORY_SIZE);
    const OtaFinalResult& result = g_persistentRecord.completed[index];
    if (result.used) {
      g_activeRequest = {};
      std::snprintf(g_activeRequest.requestId, sizeof(g_activeRequest.requestId), "%s", result.requestId);
      publishOtaEvent(result.status, result.detail, otaNowMs());
    }
  }
}

bool otaCompleteHealthValidation(uint64_t nowMonoMs) {
  if (g_phase != OtaPhase::AwaitingHealthValidation) return false;
  OtaRequest request{};
  request.action = g_persistentRecord.activeAction;
  request.payloadDigest = g_persistentRecord.activePayloadDigest;
  std::snprintf(request.requestId, sizeof(request.requestId), "%s",
                g_persistentRecord.activeRequestId);
  if (g_platform.markRunningImageValid == nullptr ||
      !g_platform.markRunningImageValid(g_platform.context)) return false;
  if (!persistFinalResult(request, "completed", "HEALTH_VALIDATED")) return false;
  std::snprintf(g_activeRequest.requestId, sizeof(g_activeRequest.requestId), "%s", request.requestId);
  g_activeRequest.action = request.action;
  g_phase = OtaPhase::SuccessPendingPublish;
  runtimeMarkOtaProgress(static_cast<uint8_t>(g_phase), g_downloadedBytes, nowMonoMs);
  publishOtaEvent("completed", "HEALTH_VALIDATED", nowMonoMs);
  return true;
}

bool otaCheckRunningExpectedImage(bool& matches) {
  matches = false;
  if (g_platform.readRunningImage == nullptr ||
      g_persistentRecord.phase != OtaStoredPhase::AwaitingHealthValidation) return false;
  if (g_runningImageCheckKnown) {
    matches = g_runningImageMatchesExpected;
    return true;
  }
  OtaTargetInfo running{};
  if (!g_platform.readRunningImage(g_platform.context, running)) return false;
  matches = std::strcmp(running.runningVersion, g_persistentRecord.targetVersion) == 0 &&
         std::strcmp(running.productId, CT_HUB_PRODUCT_ID) == 0 &&
         std::strcmp(running.boardId, CT_HUB_BOARD_ID) == 0;
  g_runningImageCheckKnown = true;
  g_runningImageMatchesExpected = matches;
  return true;
}

OtaDeduplication otaCheckRequest(const OtaRequest& request, OtaFinalResult& cachedResult) {
  return otaStoreCheckRequest(g_persistentRecord, request, cachedResult);
}

void otaAcknowledgeFinalPublished() {
  if (g_phase == OtaPhase::SuccessPendingPublish || g_phase == OtaPhase::FailedPendingPublish) {
    g_phase = OtaPhase::Idle;
  }
}

bool otaStart(const OtaRequest& request, uint64_t nowMonoMs) {
  if (g_phase != OtaPhase::Idle) return false;
  OtaFinalResult cached{};
  const OtaDeduplication dedup = otaStoreCheckRequest(g_persistentRecord, request, cached);
  if (dedup != OtaDeduplication::NewRequest) return false;
  g_activeRequest = request;
  g_maintenanceToken = static_cast<uint32_t>(request.payloadDigest ^ nowMonoMs ^ g_nextToken++);
  if (g_maintenanceToken == 0) g_maintenanceToken = g_nextToken++;
  g_startedAtMs = nowMonoMs;
  g_phaseStartedAtMs = nowMonoMs;
  OtaPersistentRecord candidate = g_persistentRecord;
  candidate.phase = OtaStoredPhase::Active;
  candidate.activeAction = request.action;
  candidate.expectedReboot = false;
  candidate.activePayloadDigest = request.payloadDigest;
  std::snprintf(candidate.activeRequestId, sizeof(candidate.activeRequestId), "%s", request.requestId);
  if (!commitCandidate(candidate)) {
    clearCurrentRequest();
    g_phase = OtaPhase::Idle;
    return false;
  }
  changePhase(OtaPhase::Validating, nowMonoMs);
  return true;
}

OtaStepResult otaStep(uint64_t nowMonoMs) {
  const OtaPhase before = g_phase;
  if (g_phase == OtaPhase::Idle) return {g_phase, false, false};
  if (g_phase == OtaPhase::FailedPendingPublish) {
    clearCurrentRequest();
    g_phase = OtaPhase::Idle;
    return {g_phase, true, false};
  }
  if (g_phase != OtaPhase::AwaitingHealthValidation &&
      g_phase != OtaPhase::SuccessPendingPublish &&
      nowMonoMs - g_startedAtMs > CT_OTA_TOTAL_TIMEOUT_MS) {
    beginFailure("failed", "OTA_TOTAL_TIMEOUT", nowMonoMs);
  }

  switch (g_phase) {
    case OtaPhase::Idle:
    case OtaPhase::SuccessPendingPublish:
    case OtaPhase::RollbackPending:
    case OtaPhase::FailedPendingPublish:
    case OtaPhase::AwaitingHealthValidation:
      break;
    case OtaPhase::Validating: {
      if (!runtimeRequestSensorPause(g_maintenanceToken)) {
        beginFailure("failed", "SENSOR_PAUSE_REQUEST_FAILED", nowMonoMs);
        break;
      }
      changePhase(OtaPhase::WaitingSensorPause, nowMonoMs);
      break;
    }
    case OtaPhase::WaitingSensorPause: {
      SensorMaintenanceState sensor{};
      runtimeCopySensorMaintenance(sensor);
      if (sensor.token == g_maintenanceToken && sensor.paused) {
        g_sensorPaused = true;
        if (!networkRequestMaintenance(g_maintenanceToken)) {
          beginFailure("failed", "NETWORK_MAINTENANCE_REQUEST_FAILED", nowMonoMs);
          break;
        }
        g_networkRequested = true;
        changePhase(OtaPhase::WaitingNetworkMaintenance, nowMonoMs);
      } else if (nowMonoMs - g_phaseStartedAtMs > CT_OTA_NO_DATA_TIMEOUT_MS) {
        beginFailure("failed", "SENSOR_PAUSE_TIMEOUT", nowMonoMs);
      }
      break;
    }
    case OtaPhase::WaitingNetworkMaintenance: {
      NetworkMaintenanceState network{};
      runtimeCopyNetworkMaintenance(network);
      if (network.token == g_maintenanceToken && network.granted) {
        g_networkGranted = true;
        changePhase(OtaPhase::Preparing, nowMonoMs);
      } else if (nowMonoMs - g_phaseStartedAtMs > CT_OTA_NO_DATA_TIMEOUT_MS) {
        beginFailure("failed", "NETWORK_MAINTENANCE_TIMEOUT", nowMonoMs);
      }
      break;
    }
    case OtaPhase::Preparing: {
      char detail[48] = "PLATFORM_PREPARE_UNAVAILABLE";
      if (g_platform.prepare == nullptr ||
          !g_platform.prepare(g_platform.context, g_activeRequest, g_targetInfo,
                             detail, sizeof(detail))) {
        beginFailure("failed", detail, nowMonoMs);
        break;
      }
      if (!g_targetInfo.hasOtaDataPartition || !g_targetInfo.hasTwoAppSlots ||
          g_targetInfo.targetPartition[0] == '\0' ||
          std::strcmp(g_targetInfo.runningPartition, g_targetInfo.targetPartition) == 0) {
        beginFailure("failed", "OTA_PARTITION_LAYOUT_UNSUPPORTED", nowMonoMs);
        break;
      }
      if (std::strcmp(g_targetInfo.productId, CT_HUB_PRODUCT_ID) != 0 ||
          std::strcmp(g_targetInfo.boardId, CT_HUB_BOARD_ID) != 0) {
        beginFailure("failed", "TARGET_IDENTITY_MISMATCH", nowMonoMs);
        break;
      }
      if (g_activeRequest.action == OtaAction::Manifest &&
          (!g_activeRequest.manifestResolved ||
           std::strcmp(g_activeRequest.productId, CT_HUB_PRODUCT_ID) != 0 ||
           std::strcmp(g_activeRequest.boardId, CT_HUB_BOARD_ID) != 0 ||
           otaCompareVersions(g_targetInfo.targetVersion, g_targetInfo.runningVersion) <= 0)) {
        beginFailure("failed", "MANIFEST_VERSION_NOT_NEWER", nowMonoMs);
        break;
      }
      if (g_activeRequest.action == OtaAction::Rollback) {
        if (!g_targetInfo.rollbackSupported || g_persistentRecord.sourcePartition[0] == '\0' ||
            std::strcmp(g_persistentRecord.sourcePartition, g_targetInfo.runningPartition) == 0 ||
            !otaIsValidVersion(g_persistentRecord.sourceVersion)) {
          beginFailure("failed", "ROLLBACK_UNSUPPORTED", nowMonoMs);
          break;
        }
        std::snprintf(g_targetInfo.targetPartition, sizeof(g_targetInfo.targetPartition), "%s",
                      g_persistentRecord.sourcePartition);
        std::snprintf(g_targetInfo.targetVersion, sizeof(g_targetInfo.targetVersion), "%s",
                      g_persistentRecord.sourceVersion);
        changePhase(OtaPhase::PersistingReboot, nowMonoMs);
        break;
      }
      changePhase(OtaPhase::Downloading, nowMonoMs);
      break;
    }
    case OtaPhase::Downloading: {
      char detail[48] = "OTA_TRANSFER_FAILED";
      g_updateStarted = true;
      const bool downloaded = g_platform.downloadAndVerify != nullptr &&
          g_platform.downloadAndVerify(g_platform.context, g_activeRequest, g_targetInfo,
                                       otaProgress, nullptr, detail, sizeof(detail));
      if (!downloaded) {
        beginFailure("failed", detail, nowMonoMs);
        break;
      }
      if (g_totalBytes > 0 && g_downloadedBytes != g_totalBytes) {
        beginFailure("failed", "IMAGE_SIZE_MISMATCH", nowMonoMs);
        break;
      }
      changePhase(OtaPhase::Verifying, nowMonoMs);
      break;
    }
    case OtaPhase::Verifying:
      if (g_targetInfo.targetVersion[0] == '\0' || !otaIsValidVersion(g_targetInfo.targetVersion)) {
        beginFailure("failed", "IMAGE_VERSION_TAG_INVALID", nowMonoMs);
      } else if (g_activeRequest.action == OtaAction::Direct &&
                 otaCompareVersions(g_targetInfo.targetVersion, g_targetInfo.runningVersion) <= 0) {
        beginFailure("failed", "IMAGE_VERSION_NOT_NEWER", nowMonoMs);
      } else {
        changePhase(OtaPhase::PersistingReboot, nowMonoMs);
      }
      break;
    case OtaPhase::PersistingReboot: {
      OtaPersistentRecord candidate = g_persistentRecord;
      candidate.phase = OtaStoredPhase::AwaitingHealthValidation;
      candidate.expectedReboot = true;
      candidate.validationBootAttempts = 0;
      candidate.rollbackSupported = g_targetInfo.rollbackSupported;
      std::snprintf(candidate.sourceVersion, sizeof(candidate.sourceVersion), "%s", g_targetInfo.runningVersion);
      std::snprintf(candidate.targetVersion, sizeof(candidate.targetVersion), "%s", g_targetInfo.targetVersion);
      std::snprintf(candidate.sourcePartition, sizeof(candidate.sourcePartition), "%s", g_targetInfo.runningPartition);
      std::snprintf(candidate.targetPartition, sizeof(candidate.targetPartition), "%s", g_targetInfo.targetPartition);
      if (!commitCandidate(candidate)) {
        beginFailure("failed", "OTA_EXPECTED_REBOOT_PERSIST_FAILED", nowMonoMs);
        break;
      }
      if (g_platform.selectBootPartition == nullptr ||
          !g_platform.selectBootPartition(g_platform.context, g_targetInfo.targetPartition)) {
        OtaPersistentRecord rollbackRecord = g_persistentRecord;
        rollbackRecord.phase = OtaStoredPhase::Active;
        rollbackRecord.expectedReboot = false;
        (void)commitCandidate(rollbackRecord);
        beginFailure("failed", "OTA_BOOT_SELECTION_FAILED", nowMonoMs);
        break;
      }
      changePhase(OtaPhase::AwaitingReboot, nowMonoMs);
      break;
    }
    case OtaPhase::AwaitingReboot: {
      if (g_platform.restart != nullptr) g_platform.restart(g_platform.context);
      if (g_platform.selectBootPartition != nullptr && g_targetInfo.runningPartition[0] != '\0') {
        (void)g_platform.selectBootPartition(g_platform.context, g_targetInfo.runningPartition);
      }
      OtaPersistentRecord rollbackRecord = g_persistentRecord;
      rollbackRecord.phase = OtaStoredPhase::Active;
      rollbackRecord.expectedReboot = false;
      (void)commitCandidate(rollbackRecord);
      beginFailure("failed", "OTA_RESTART_RETURNED", nowMonoMs);
      break;
    }
    case OtaPhase::CleaningUp:
      if (g_updateStarted && g_platform.abortUpdate != nullptr) {
        (void)g_platform.abortUpdate(g_platform.context);
      }
      if (g_networkRequested) {
        (void)networkReleaseMaintenance(g_maintenanceToken);
        g_networkRequested = false;
        g_networkGranted = false;
      }
      if (g_sensorPaused) {
        (void)runtimeRequestSensorResume(g_maintenanceToken);
        changePhase(OtaPhase::Resuming, nowMonoMs);
      } else {
        SensorMaintenanceState sensor{};
        runtimeCopySensorMaintenance(sensor);
        if (sensor.token == g_maintenanceToken && sensor.pauseRequested) {
          runtimeCancelSensorPause(g_maintenanceToken);
        }
        finalizeFailure(nowMonoMs);
      }
      break;
    case OtaPhase::Resuming: {
      SensorMaintenanceState sensor{};
      runtimeCopySensorMaintenance(sensor);
      if (sensor.token == g_maintenanceToken && sensor.paused && !sensor.resumeRequested) {
        (void)runtimeRequestSensorResume(g_maintenanceToken);
      } else if (sensor.token != g_maintenanceToken) {
        finalizeFailure(nowMonoMs);
      } else if (nowMonoMs - g_phaseStartedAtMs > CT_OTA_NO_DATA_TIMEOUT_MS) {
        std::snprintf(g_failureStatus, sizeof(g_failureStatus), "failed");
        std::snprintf(g_failureDetail, sizeof(g_failureDetail), "SENSOR_RESUME_TIMEOUT");
        finalizeFailure(nowMonoMs);
      }
      break;
    }
  }
  return {g_phase, before != g_phase, g_phase != OtaPhase::Idle && g_phase != OtaPhase::FailedPendingPublish};
}

OtaPhase otaCurrentPhase() { return g_phase; }

void TaskOTA(void* parameter) {
  (void)parameter;
  ctDebugLog("INFO", "OTA", "TaskOTA started; phase=%s", phaseName(g_phase));
  for (;;) {
    if (g_phase == OtaPhase::Idle) {
      OtaRequest request{};
      if (runtimeTakeOtaRequest(request)) (void)otaStart(request, millis());
    }
    (void)otaStep(millis());
    delay(10);
  }
}
