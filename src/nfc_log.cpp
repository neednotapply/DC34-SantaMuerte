#include "nfc_log.h"

#include <LittleFS.h>
#include <cstddef>
#include <cstdio>
#include <cstring>

#include "board.h"

namespace {
constexpr char NFC_LOG_DIR[] = "/nfc_log";
constexpr char LEGACY_LOG_PATH[] = "/nfc_log.dat";
constexpr uint32_t NFC_LOG_RECORD_MAGIC = 0x524C464EUL;
constexpr uint8_t NFC_LOG_MAX_UID = 10;
constexpr size_t SCRIPT_STORAGE_RESERVE_BYTES = 4096;
struct __attribute__((packed)) NfcLogRecord {
  uint32_t magic, lastSeenId, hitCount;
  uint8_t uidLength, uid[NFC_LOG_MAX_UID];
  char tagType[NFC_LOG_TYPE_MAX];
  uint16_t contentLength;
  char content[NFC_LOG_CONTENT_MAX];
  uint32_t checksum;
};
bool logReady = false;
uint32_t nextId = 1, highestStoredId = 0;
uint16_t storedCount = 0;
uint32_t fnv1a(const uint8_t *data, size_t length) {
  uint32_t hash = 2166136261UL;
  for (size_t i = 0; i < length; ++i) { hash ^= data[i]; hash *= 16777619UL; }
  return hash;
}
uint32_t recordChecksum(const NfcLogRecord &record) {
  return fnv1a(reinterpret_cast<const uint8_t *>(&record), offsetof(NfcLogRecord, checksum));
}
bool validRecord(const NfcLogRecord &record) {
  return record.magic == NFC_LOG_RECORD_MAGIC && record.lastSeenId && record.uidLength &&
         record.uidLength <= NFC_LOG_MAX_UID && record.contentLength <= NFC_LOG_CONTENT_MAX &&
         record.checksum == recordChecksum(record);
}
void recordPath(uint32_t id, char *path, size_t capacity) {
  snprintf(path, capacity, "%s/%010lu.tag", NFC_LOG_DIR, static_cast<unsigned long>(id));
}
bool isTagPath(const char *path) { const char *dot = strrchr(path, '.'); return dot && strcmp(dot, ".tag") == 0; }
String tagEntryPath(const char *name) {
  String path(name);
  if (!path.startsWith("/")) { String full(NFC_LOG_DIR); full += '/'; full += path; return full; }
  return path;
}
bool readRecordPath(const char *path, NfcLogRecord &record) {
  File file = LittleFS.open(path, "r"); if (!file) return false;
  const bool ok = file.read(reinterpret_cast<uint8_t *>(&record), sizeof(record)) == sizeof(record);
  file.close(); return ok && validRecord(record);
}
bool writeRecordPath(const char *path, const NfcLogRecord &record) {
  File file = LittleFS.open(path, "w"); if (!file) return false;
  const bool ok = file.write(reinterpret_cast<const uint8_t *>(&record), sizeof(record)) == sizeof(record);
  file.close(); return ok;
}
bool oldestRecord(NfcLogRecord &oldest, char *path, size_t pathCapacity) {
  bool found = false; File directory = LittleFS.open(NFC_LOG_DIR);
  if (!directory || !directory.isDirectory()) return false;
  for (File entry = directory.openNextFile(); entry; entry = directory.openNextFile()) {
    if (entry.isDirectory() || !isTagPath(entry.name())) continue;
    const String candidate = tagEntryPath(entry.name()); entry.close(); NfcLogRecord record = {};
    if (!readRecordPath(candidate.c_str(), record)) continue;
    if (!found || record.lastSeenId < oldest.lastSeenId) { oldest = record; strncpy(path, candidate.c_str(), pathCapacity - 1); path[pathCapacity - 1] = '\0'; found = true; }
  }
  directory.close(); return found;
}
bool findUid(const uint8_t *uid, uint8_t uidLength, NfcLogRecord &match, char *path, size_t pathCapacity) {
  File directory = LittleFS.open(NFC_LOG_DIR);
  if (!directory || !directory.isDirectory()) return false;
  for (File entry = directory.openNextFile(); entry; entry = directory.openNextFile()) {
    if (entry.isDirectory() || !isTagPath(entry.name())) continue;
    const String candidate = tagEntryPath(entry.name()); entry.close(); NfcLogRecord record = {};
    if (!readRecordPath(candidate.c_str(), record) || record.uidLength != uidLength || memcmp(record.uid, uid, uidLength)) continue;
    match = record; strncpy(path, candidate.c_str(), pathCapacity - 1); path[pathCapacity - 1] = '\0'; directory.close(); return true;
  }
  directory.close(); return false;
}
size_t freeBytes() { const size_t total = LittleFS.totalBytes(), used = LittleFS.usedBytes(); return total > used ? total - used : 0; }
String uidToString(const uint8_t *uid, uint8_t length) {
  String out; out.reserve(length * 3); char byteText[4];
  for (uint8_t i = 0; i < length; ++i) { snprintf(byteText, sizeof(byteText), "%02X", uid[i]); if (i) out += ':'; out += byteText; }
  return out;
}
}  // namespace

bool setupNfcLog() {
  logReady = false;
  if (!LittleFS.exists(NFC_LOG_DIR) && !LittleFS.mkdir(NFC_LOG_DIR)) return false;
  // The old ring is disposable encounter history; removing it releases its
  // permanent reservation for scripts and the unbounded new log.
  if (LittleFS.exists(LEGACY_LOG_PATH)) { LittleFS.remove(LEGACY_LOG_PATH); Serial.println("[NFCLOG] Retired the legacy fixed log ring"); }
  storedCount = 0; highestStoredId = 0;
  File directory = LittleFS.open(NFC_LOG_DIR);
  for (File entry = directory.openNextFile(); entry; entry = directory.openNextFile()) {
    if (entry.isDirectory() || !isTagPath(entry.name())) continue;
    const String path = tagEntryPath(entry.name()); entry.close(); NfcLogRecord record = {};
    if (!readRecordPath(path.c_str(), record)) continue;
    ++storedCount; if (record.lastSeenId > highestStoredId) highestStoredId = record.lastSeenId;
  }
  directory.close(); nextId = highestStoredId + 1; logReady = true;
  Serial.printf("[NFCLOG] Recovered %u entries; next id %lu\r\n", static_cast<unsigned>(storedCount), static_cast<unsigned long>(nextId));
  return true;
}
bool isNfcLogReady() { return logReady; }
uint16_t nfcLogStoredCount() { return storedCount; }
uint32_t nfcLogNewestId() { return highestStoredId; }
bool reclaimNfcLogStorage(size_t bytesNeeded) {
  if (!logReady) return false;
  while (freeBytes() < bytesNeeded && storedCount) {
    NfcLogRecord oldest = {}; char path[48] = {};
    if (!oldestRecord(oldest, path, sizeof(path)) || !deleteNfcLogEntry(oldest.lastSeenId)) break;
  }
  return freeBytes() >= bytesNeeded;
}
bool nfcLogRecord(const uint8_t *uid, uint8_t uidLength, const String &tagType, const String &content) {
  if (!logReady || !uidLength || uidLength > NFC_LOG_MAX_UID) return false;
  NfcLogRecord previous = {}; char oldPath[48] = {};
  const bool existed = findUid(uid, uidLength, previous, oldPath, sizeof(oldPath));
  // Keep one script-sized reserve for future scripts, except when refreshing an
  // existing entry (which replaces a same-sized file).
  if (!existed) {
    const size_t needed = sizeof(NfcLogRecord) + SCRIPT_STORAGE_RESERVE_BYTES;
    reclaimBoardStorage(needed);
    if (!reclaimNfcLogStorage(needed)) return false;
  }
  NfcLogRecord record = {};
  record.magic = NFC_LOG_RECORD_MAGIC; record.lastSeenId = nextId++; record.hitCount = existed ? previous.hitCount + 1 : 1;
  record.uidLength = uidLength; memcpy(record.uid, uid, uidLength);
  strncpy(record.tagType, tagType.c_str(), NFC_LOG_TYPE_MAX - 1);
  record.contentLength = min(content.length(), NFC_LOG_CONTENT_MAX); memcpy(record.content, content.c_str(), record.contentLength);
  record.checksum = recordChecksum(record);
  char path[48] = {}; recordPath(record.lastSeenId, path, sizeof(path));
  if (!writeRecordPath(path, record)) return false;
  if (existed) LittleFS.remove(oldPath); else ++storedCount;
  highestStoredId = record.lastSeenId; return true;
}
bool nfcLogReadNext(uint32_t &beforeId, NfcLogEntry &entry) {
  if (!logReady) return false;
  NfcLogRecord best = {}; bool found = false; File directory = LittleFS.open(NFC_LOG_DIR);
  for (File item = directory.openNextFile(); item; item = directory.openNextFile()) {
    if (item.isDirectory() || !isTagPath(item.name())) continue;
    const String path = tagEntryPath(item.name()); item.close(); NfcLogRecord record = {};
    if (!readRecordPath(path.c_str(), record) || (beforeId && record.lastSeenId >= beforeId)) continue;
    if (!found || record.lastSeenId > best.lastSeenId) { best = record; found = true; }
  }
  directory.close(); if (!found) return false;
  entry.uid = uidToString(best.uid, best.uidLength); entry.tagType = String(best.tagType);
  entry.content = String(); entry.content.concat(best.content, best.contentLength); entry.hitCount = best.hitCount; entry.lastSeenId = best.lastSeenId; beforeId = best.lastSeenId;
  return true;
}
bool deleteNfcLogEntry(uint32_t lastSeenId) {
  if (!logReady || !lastSeenId) return false;
  char path[48] = {}; recordPath(lastSeenId, path, sizeof(path));
  NfcLogRecord record = {}; if (!readRecordPath(path, record) || !LittleFS.remove(path)) return false;
  if (storedCount) --storedCount; Serial.printf("[NFCLOG] Entry #%lu retired\r\n", static_cast<unsigned long>(lastSeenId)); return true;
}
bool clearNfcLog() {
  if (!logReady) return false;
  while (storedCount) { NfcLogRecord oldest = {}; char path[48] = {}; if (!oldestRecord(oldest, path, sizeof(path)) || !deleteNfcLogEntry(oldest.lastSeenId)) return false; }
  highestStoredId = 0; return true;
}
