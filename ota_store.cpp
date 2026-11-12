#include "ota_store.h"

#include <cstddef>
#include <cstdio>
#include <cstring>

#include <Preferences.h>

namespace {
constexpr uint32_t kOtaRecordCommitMarker = 0x43544f31U;
constexpr char kOtaSlotA[] = "ota_a";
constexpr char kOtaSlotB[] = "ota_b";

uint32_t calculateChecksum(const OtaPersistentRecord& record) {
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&record);
  uint32_t checksum = 2166136261U;
  for (std::size_t index = 0; index < offsetof(OtaPersistentRecord, checksum); ++index) {
    checksum ^= bytes[index];
    checksum *= 16777619U;
  }
  return checksum;
}

bool revisionIsNewer(uint32_t candidate, uint32_t current) {
  return static_cast<int32_t>(candidate - current) > 0;
}

bool readSlot(Preferences& preferences, const char* key, OtaPersistentRecord& record) {
  if (preferences.getBytesLength(key) != sizeof(record) ||
      preferences.getBytes(key, &record, sizeof(record)) != sizeof(record)) return false;
  return otaStoreRecordIsValid(record);
}

bool preferencesLoad(OtaPersistentRecord& record) {
  Preferences preferences;
  if (!preferences.begin("cthubota", true)) return false;
  OtaPersistentRecord slotA{};
  OtaPersistentRecord slotB{};
  const bool validA = readSlot(preferences, kOtaSlotA, slotA);
  const bool validB = readSlot(preferences, kOtaSlotB, slotB);
  preferences.end();
  if (!validA && !validB) {
    record = {};
    return true;
  }
  record = !validA ? slotB : (!validB || revisionIsNewer(slotA.revision, slotB.revision)) ? slotA : slotB;
  return true;
}

bool preferencesSave(const OtaPersistentRecord& record) {
  Preferences preferences;
  if (!preferences.begin("cthubota", false)) return false;
  OtaPersistentRecord slotA{};
  OtaPersistentRecord slotB{};
  const bool validA = readSlot(preferences, kOtaSlotA, slotA);
  const bool validB = readSlot(preferences, kOtaSlotB, slotB);
  const char* key = kOtaSlotA;
  if (validA && (!validB || revisionIsNewer(slotA.revision, slotB.revision))) key = kOtaSlotB;
  else if (validB) key = kOtaSlotA;
  const bool saved = preferences.putBytes(key, &record, sizeof(record)) == sizeof(record);
  preferences.end();
  return saved;
}

const char* actionName(OtaAction action) {
  switch (action) {
    case OtaAction::Manifest: return "manifest";
    case OtaAction::Direct: return "direct";
    case OtaAction::Rollback: return "rollback";
  }
  return "update";
}
}

bool otaStoreRecordIsValid(const OtaPersistentRecord& record) {
  return record.commitMarker == kOtaRecordCommitMarker &&
         record.schemaVersion == OTA_RECORD_SCHEMA_VERSION &&
         record.completedCount <= OTA_COMPLETED_HISTORY_SIZE &&
         record.completedNext < OTA_COMPLETED_HISTORY_SIZE &&
         record.phase <= OtaStoredPhase::FinalPendingPublish &&
         record.activeAction <= OtaAction::Rollback &&
         record.checksum == calculateChecksum(record);
}

bool otaStoreLoad(OtaPersistentRecord& record, const OtaPersistenceAdapter* adapter) {
  record = {};
  if (adapter != nullptr && adapter->load != nullptr) {
    if (!adapter->load(adapter->context, record)) return false;
    return record.commitMarker == 0 || otaStoreRecordIsValid(record);
  }
  return preferencesLoad(record);
}

bool otaStoreCommit(OtaPersistentRecord& record, const OtaPersistenceAdapter* adapter) {
  OtaPersistentRecord candidate = record;
  candidate.commitMarker = kOtaRecordCommitMarker;
  candidate.schemaVersion = OTA_RECORD_SCHEMA_VERSION;
  candidate.revision = record.revision == UINT32_MAX ? 1U : record.revision + 1U;
  candidate.checksum = 0;
  candidate.checksum = calculateChecksum(candidate);
  if (adapter != nullptr && adapter->save != nullptr) {
    if (!adapter->save(adapter->context, candidate)) return false;
  } else {
    if (!preferencesSave(candidate)) return false;
  }
  record = candidate;
  return true;
}

OtaDeduplication otaStoreCheckRequest(const OtaPersistentRecord& record,
                                      const OtaRequest& request,
                                      OtaFinalResult& cachedResult) {
  cachedResult = {};
  const bool requestActive = record.phase == OtaStoredPhase::Active ||
                             record.phase == OtaStoredPhase::AwaitingHealthValidation;
  if (requestActive) {
    if (request.requestId[0] != '\0' &&
        std::strcmp(record.activeRequestId, request.requestId) == 0 &&
        record.activePayloadDigest != request.payloadDigest) {
      return OtaDeduplication::RequestIdConflict;
    }
    return OtaDeduplication::ActiveDuplicate;
  }
  if (request.requestId[0] == '\0') {
    for (const OtaFinalResult& result : record.completed) {
      if (result.used && result.requestId[0] == '\0' &&
          result.payloadDigest == request.payloadDigest) {
        cachedResult = result;
        return OtaDeduplication::CompletedDuplicate;
      }
    }
    return OtaDeduplication::NewRequest;
  }
  for (const OtaFinalResult& result : record.completed) {
    if (!result.used || std::strcmp(result.requestId, request.requestId) != 0) continue;
    if (result.payloadDigest != request.payloadDigest) return OtaDeduplication::RequestIdConflict;
    cachedResult = result;
    return OtaDeduplication::CompletedDuplicate;
  }
  return OtaDeduplication::NewRequest;
}

void otaStoreRecordFinal(OtaPersistentRecord& record, const OtaRequest& request,
                         const char* status, const char* detail) {
  OtaFinalResult& result = record.completed[record.completedNext];
  result = {};
  result.used = true;
  result.payloadDigest = request.payloadDigest;
  std::snprintf(result.requestId, sizeof(result.requestId), "%s", request.requestId);
  std::snprintf(result.command, sizeof(result.command), "%s", actionName(request.action));
  std::snprintf(result.status, sizeof(result.status), "%s", status == nullptr ? "failed" : status);
  std::snprintf(result.detail, sizeof(result.detail), "%s", detail == nullptr ? "UNKNOWN" : detail);
  record.completedNext = static_cast<uint8_t>((record.completedNext + 1U) % OTA_COMPLETED_HISTORY_SIZE);
  if (record.completedCount < OTA_COMPLETED_HISTORY_SIZE) ++record.completedCount;
  std::snprintf(record.lastResultStatus, sizeof(record.lastResultStatus), "%s", result.status);
  std::snprintf(record.lastResultDetail, sizeof(record.lastResultDetail), "%s", result.detail);
}
