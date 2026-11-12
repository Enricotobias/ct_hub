#pragma once

#include "ct_types.h"

#include <cstddef>
#include <cstdint>

struct OtaFinalResult;
enum class OtaDeduplication : uint8_t;

constexpr std::size_t OTA_REQUEST_ID_CAPACITY = 40;
constexpr std::size_t OTA_URL_CAPACITY = 256;
constexpr std::size_t OTA_MD5_CAPACITY = 33;
constexpr std::size_t OTA_VERSION_CAPACITY = 24;
constexpr std::size_t OTA_PRODUCT_CAPACITY = 24;
constexpr std::size_t OTA_BOARD_CAPACITY = 32;

enum class OtaAction : uint8_t { Manifest, Direct, Rollback };

struct OtaRequest {
  OtaAction action;
  char requestId[OTA_REQUEST_ID_CAPACITY];
  char url[OTA_URL_CAPACITY];
  char md5[OTA_MD5_CAPACITY];
  char version[OTA_VERSION_CAPACITY];
  char productId[OTA_PRODUCT_CAPACITY];
  char boardId[OTA_BOARD_CAPACITY];
  uint64_t payloadDigest;
  uint32_t sizeBytes;
  bool hasSizeBytes;
  bool rollbackConfirmed;
  bool manifestResolved;
};

struct OtaParseResult {
  bool accepted;
  OtaRequest request;
  const char* detail;
};

enum class OtaPhase : uint8_t {
  Idle,
  Validating,
  WaitingSensorPause,
  WaitingNetworkMaintenance,
  Preparing,
  Downloading,
  Verifying,
  PersistingReboot,
  AwaitingReboot,
  AwaitingHealthValidation,
  SuccessPendingPublish,
  CleaningUp,
  Resuming,
  RollbackPending,
  FailedPendingPublish,
};

struct OtaTargetInfo {
  uint32_t imageSize;
  bool hasOtaDataPartition;
  bool hasTwoAppSlots;
  bool rollbackSupported;
  char runningVersion[OTA_VERSION_CAPACITY];
  char targetVersion[OTA_VERSION_CAPACITY];
  char productId[OTA_PRODUCT_CAPACITY];
  char boardId[OTA_BOARD_CAPACITY];
  char runningPartition[18];
  char targetPartition[18];
};

using OtaProgressCallback = void (*)(void* context, uint32_t bytes, uint32_t totalBytes);

struct OtaPlatformAdapter {
  void* context;
  bool (*prepare)(void* context, OtaRequest& request, OtaTargetInfo& target,
                  char* detail, std::size_t detailCapacity);
  bool (*downloadAndVerify)(void* context, const OtaRequest& request,
                            OtaTargetInfo& target, OtaProgressCallback progress,
                            void* progressContext, char* detail, std::size_t detailCapacity);
  bool (*selectBootPartition)(void* context, const char* partitionLabel);
  bool (*abortUpdate)(void* context);
  void (*restart)(void* context);
  bool (*markRunningImageValid)(void* context);
  bool (*readRunningImage)(void* context, OtaTargetInfo& image);
};

struct OtaStepResult {
  OtaPhase phase;
  bool phaseChanged;
  bool requestActive;
};

OtaParseResult otaParseRequest(const uint8_t* payload, std::size_t length);
bool otaParseManifest(const uint8_t* payload, std::size_t length,
                      const OtaRequest& envelope, OtaRequest& resolved,
                      const char*& detail);
bool otaIsValidHttpsUrl(const char* url);
bool otaIsValidMd5(const char* md5);
bool otaIsValidVersion(const char* version);
int otaCompareVersions(const char* left, const char* right);
void otaConfigure(const OtaPlatformAdapter& platform, const struct OtaPersistenceAdapter* persistence = nullptr);
bool otaStart(const OtaRequest& request, uint64_t nowMonoMs);
bool otaCompleteHealthValidation(uint64_t nowMonoMs);
bool otaCheckRunningExpectedImage(bool& matches);
OtaDeduplication otaCheckRequest(const OtaRequest& request, OtaFinalResult& cachedResult);
void otaAcknowledgeFinalPublished();
OtaStepResult otaStep(uint64_t nowMonoMs);
OtaPhase otaCurrentPhase();
void TaskOTA(void* parameter);

