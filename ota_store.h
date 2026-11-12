#pragma once

#include "ota.h"

#include <cstdint>

constexpr uint8_t OTA_COMPLETED_HISTORY_SIZE = 8;
constexpr uint16_t OTA_RECORD_SCHEMA_VERSION = 1;

enum class OtaStoredPhase : uint8_t {
  Empty,
  Active,
  AwaitingHealthValidation,
  FinalPendingPublish,
};

struct OtaFinalResult {
  bool used;
  uint64_t payloadDigest;
  char requestId[OTA_REQUEST_ID_CAPACITY];
  char command[20];
  char status[16];
  char detail[48];
};

struct OtaPersistentRecord {
  uint32_t commitMarker;
  uint16_t schemaVersion;
  uint16_t reserved;
  uint32_t revision;
  OtaStoredPhase phase;
  uint8_t validationBootAttempts;
  OtaAction activeAction;
  bool expectedReboot;
  bool rollbackSupported;
  uint8_t completedCount;
  uint8_t completedNext;
  uint64_t activePayloadDigest;
  char activeRequestId[OTA_REQUEST_ID_CAPACITY];
  char sourceVersion[OTA_VERSION_CAPACITY];
  char targetVersion[OTA_VERSION_CAPACITY];
  char sourcePartition[18];
  char targetPartition[18];
  char lastResultStatus[16];
  char lastResultDetail[48];
  OtaFinalResult completed[OTA_COMPLETED_HISTORY_SIZE];
  uint32_t checksum;
};

struct OtaPersistenceAdapter {
  void* context;
  bool (*load)(void* context, OtaPersistentRecord& record);
  bool (*save)(void* context, const OtaPersistentRecord& record);
};

enum class OtaDeduplication : uint8_t { NewRequest, ActiveDuplicate, CompletedDuplicate, RequestIdConflict };

bool otaStoreLoad(OtaPersistentRecord& record, const OtaPersistenceAdapter* adapter = nullptr);
bool otaStoreCommit(OtaPersistentRecord& record, const OtaPersistenceAdapter* adapter = nullptr);
OtaDeduplication otaStoreCheckRequest(const OtaPersistentRecord& record,
                                      const OtaRequest& request,
                                      OtaFinalResult& cachedResult);
void otaStoreRecordFinal(OtaPersistentRecord& record, const OtaRequest& request,
                         const char* status, const char* detail);
bool otaStoreRecordIsValid(const OtaPersistentRecord& record);

