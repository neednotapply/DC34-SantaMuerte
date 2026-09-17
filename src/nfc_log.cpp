#include "nfc_log.h"

#include <LittleFS.h>
#include <cstddef>

namespace {

// "NFLB" / "NFLR" -- distinct from the Field Notes board's magics so a stray
// cross-read is rejected rather than half-parsed.
constexpr uint32_t NFC_LOG_FILE_MAGIC = 0x424C464EUL;
constexpr uint32_t NFC_LOG_RECORD_MAGIC = 0x524C464EUL;
constexpr uint16_t NFC_LOG_FORMAT_VERSION = 1;
constexpr uint16_t NFC_LOG_SLOT_COUNT = 128;
constexpr uint8_t NFC_LOG_MAX_UID = 10;

const char *NFC_LOG_PATH = "/nfc_log.dat";

struct __attribute__((packed)) NfcLogFileHeader {
  uint32_t magic;
  uint16_t version;
  uint16_t slotCount;
  uint16_t slotSize;
  uint16_t reserved;
};

struct __attribute__((packed)) NfcLogRecord {
  uint32_t magic;
  uint32_t lastSeenId;
  uint32_t hitCount;
  uint8_t uidLength;
  uint8_t uid[NFC_LOG_MAX_UID];
  char tagType[NFC_LOG_TYPE_MAX];
  uint16_t contentLength;
  char content[NFC_LOG_CONTENT_MAX];
  uint32_t checksum;
};

File logFile;
bool logReady = false;
uint16_t nextSlot = 0;
uint32_t nextId = 1;
uint32_t highestStoredId = 0;
uint16_t storedCount = 0;

uint32_t fnv1a(const uint8_t *data, size_t length) {
  uint32_t hash = 2166136261UL;
  for (size_t i = 0; i < length; ++i) {
    hash ^= data[i];
    hash *= 16777619UL;
  }
  return hash;
}

uint32_t recordChecksum(const NfcLogRecord &record) {
  return fnv1a(reinterpret_cast<const uint8_t *>(&record),
               offsetof(NfcLogRecord, checksum));
}

uint32_t slotOffset(uint16_t slot) {
  return sizeof(NfcLogFileHeader) +
         static_cast<uint32_t>(slot) * sizeof(NfcLogRecord);
}

bool validRecord(const NfcLogRecord &record) {
  return record.magic == NFC_LOG_RECORD_MAGIC && record.lastSeenId != 0 &&
         record.uidLength <= NFC_LOG_MAX_UID &&
         record.contentLength <= NFC_LOG_CONTENT_MAX &&
         record.checksum == recordChecksum(record);
}

bool readRecord(uint16_t slot, NfcLogRecord &record) {
  if (!logFile || !logFile.seek(slotOffset(slot))) return false;
  return logFile.read(reinterpret_cast<uint8_t *>(&record), sizeof(record)) ==
         sizeof(record);
}

bool writeRecord(uint16_t slot, const NfcLogRecord &record) {
  if (!logFile || !logFile.seek(slotOffset(slot))) return false;
  const size_t written =
      logFile.write(reinterpret_cast<const uint8_t *>(&record), sizeof(record));
  logFile.flush();
  return written == sizeof(record);
}

bool createRingFile() {
  File file = LittleFS.open(NFC_LOG_PATH, "w");
  if (!file) {
    Serial.printf("[NFCLOG] ERROR: could not create %s\r\n", NFC_LOG_PATH);
    return false;
  }
  NfcLogFileHeader header = {};
  header.magic = NFC_LOG_FILE_MAGIC;
  header.version = NFC_LOG_FORMAT_VERSION;
  header.slotCount = NFC_LOG_SLOT_COUNT;
  header.slotSize = sizeof(NfcLogRecord);
  bool ok = file.write(reinterpret_cast<const uint8_t *>(&header),
                       sizeof(header)) == sizeof(header);

  uint8_t zeros[256] = {0};
  const uint32_t total =
      static_cast<uint32_t>(NFC_LOG_SLOT_COUNT) * sizeof(NfcLogRecord);
  for (uint32_t written = 0; ok && written < total;) {
    const size_t chunk = min(sizeof(zeros), static_cast<size_t>(total - written));
    ok = file.write(zeros, chunk) == chunk;
    written += chunk;
  }
  file.close();
  if (!ok) {
    Serial.printf("[NFCLOG] ERROR: could not preallocate %s\r\n", NFC_LOG_PATH);
    LittleFS.remove(NFC_LOG_PATH);
    return false;
  }
  Serial.printf("[NFCLOG] Created %s: %u slots of %u bytes\r\n", NFC_LOG_PATH,
                static_cast<unsigned>(NFC_LOG_SLOT_COUNT),
                static_cast<unsigned>(sizeof(NfcLogRecord)));
  return true;
}

bool headerMatches(File &file) {
  NfcLogFileHeader header = {};
  if (!file.seek(0) ||
      file.read(reinterpret_cast<uint8_t *>(&header), sizeof(header)) !=
          sizeof(header)) {
    return false;
  }
  return header.magic == NFC_LOG_FILE_MAGIC &&
         header.version == NFC_LOG_FORMAT_VERSION &&
         header.slotCount == NFC_LOG_SLOT_COUNT &&
         header.slotSize == sizeof(NfcLogRecord);
}

// Walks every slot once at boot to find the highest lastSeenId (the newest
// tag) and the count in use, so a new sighting lands on the slot after the
// newest and ids keep climbing across reboots.
void recoverCursor() {
  storedCount = 0;
  highestStoredId = 0;
  uint16_t highestSlot = 0;
  for (uint16_t slot = 0; slot < NFC_LOG_SLOT_COUNT; ++slot) {
    NfcLogRecord record;
    if (!readRecord(slot, record) || !validRecord(record)) continue;
    ++storedCount;
    if (record.lastSeenId > highestStoredId) {
      highestStoredId = record.lastSeenId;
      highestSlot = slot;
    }
  }
  nextId = highestStoredId + 1;
  nextSlot = (highestStoredId == 0) ? 0 : ((highestSlot + 1) % NFC_LOG_SLOT_COUNT);
  Serial.printf("[NFCLOG] Recovered %u/%u entries; nextId=%lu nextSlot=%u\r\n",
                static_cast<unsigned>(storedCount),
                static_cast<unsigned>(NFC_LOG_SLOT_COUNT),
                static_cast<unsigned long>(nextId),
                static_cast<unsigned>(nextSlot));
}

String uidToString(const uint8_t *uid, uint8_t length) {
  String out;
  out.reserve(length * 3);
  char byteText[4];
  for (uint8_t i = 0; i < length; ++i) {
    snprintf(byteText, sizeof(byteText), "%02X", uid[i]);
    if (i) out += ':';
    out += byteText;
  }
  return out;
}

// Finds the slot already holding this UID, or NFC_LOG_SLOT_COUNT when none does.
uint16_t findUidSlot(const uint8_t *uid, uint8_t uidLength) {
  for (uint16_t slot = 0; slot < NFC_LOG_SLOT_COUNT; ++slot) {
    NfcLogRecord record;
    if (!readRecord(slot, record) || !validRecord(record)) continue;
    if (record.uidLength == uidLength &&
        memcmp(record.uid, uid, uidLength) == 0) {
      return slot;
    }
  }
  return NFC_LOG_SLOT_COUNT;
}

void fillRecord(NfcLogRecord &record, const uint8_t *uid, uint8_t uidLength,
                const String &tagType, const String &content, uint32_t hitCount) {
  memset(&record, 0, sizeof(record));
  record.magic = NFC_LOG_RECORD_MAGIC;
  record.lastSeenId = nextId++;
  record.hitCount = hitCount;
  record.uidLength = min<uint8_t>(uidLength, NFC_LOG_MAX_UID);
  memcpy(record.uid, uid, record.uidLength);
  strncpy(record.tagType, tagType.c_str(), NFC_LOG_TYPE_MAX - 1);
  const size_t contentLen = min(content.length(), NFC_LOG_CONTENT_MAX);
  memcpy(record.content, content.c_str(), contentLen);
  record.contentLength = static_cast<uint16_t>(contentLen);
  record.checksum = recordChecksum(record);
}

}  // namespace

bool setupNfcLog() {
  logReady = false;
  if (!LittleFS.exists(NFC_LOG_PATH) && !createRingFile()) return false;

  logFile = LittleFS.open(NFC_LOG_PATH, "r+");
  if (!logFile || !headerMatches(logFile)) {
    if (logFile) logFile.close();
    Serial.println("[NFCLOG] Layout mismatch or open failure; recreating");
    if (!createRingFile()) return false;
    logFile = LittleFS.open(NFC_LOG_PATH, "r+");
    if (!logFile) return false;
  }
  recoverCursor();
  logReady = true;
  return true;
}

bool isNfcLogReady() { return logReady; }
uint16_t nfcLogStoredCount() { return storedCount; }
uint16_t nfcLogCapacity() { return NFC_LOG_SLOT_COUNT; }
uint32_t nfcLogNewestId() { return highestStoredId; }

bool nfcLogRecord(const uint8_t *uid, uint8_t uidLength, const String &tagType,
                  const String &content) {
  if (!logReady || uidLength == 0 || uidLength > NFC_LOG_MAX_UID) return false;

  const uint16_t existing = findUidSlot(uid, uidLength);
  NfcLogRecord record;
  uint16_t slot;
  uint32_t hitCount = 1;

  if (existing != NFC_LOG_SLOT_COUNT) {
    // Seen before: keep its slot, carry the count forward, move it to newest.
    NfcLogRecord previous;
    if (readRecord(existing, previous) && validRecord(previous)) {
      hitCount = previous.hitCount + 1;
    }
    slot = existing;
  } else {
    slot = nextSlot;
    NfcLogRecord evicted;
    const bool wasValid = readRecord(slot, evicted) && validRecord(evicted);
    nextSlot = (nextSlot + 1) % NFC_LOG_SLOT_COUNT;
    if (!wasValid && storedCount < NFC_LOG_SLOT_COUNT) ++storedCount;
  }

  fillRecord(record, uid, uidLength, tagType, content, hitCount);
  if (!writeRecord(slot, record)) return false;
  if (record.lastSeenId > highestStoredId) highestStoredId = record.lastSeenId;
  return true;
}

bool nfcLogReadNext(uint32_t &beforeId, NfcLogEntry &entry) {
  if (!logReady) return false;
  // Linear scan for the highest lastSeenId strictly below beforeId (0 means
  // "newest"). The ring is small enough that a scan per entry is cheap and
  // needs no in-RAM index.
  uint32_t bestId = 0;
  bool found = false;
  NfcLogRecord best;
  for (uint16_t slot = 0; slot < NFC_LOG_SLOT_COUNT; ++slot) {
    NfcLogRecord record;
    if (!readRecord(slot, record) || !validRecord(record)) continue;
    if (beforeId != 0 && record.lastSeenId >= beforeId) continue;
    if (record.lastSeenId > bestId) {
      bestId = record.lastSeenId;
      best = record;
      found = true;
    }
  }
  if (!found) return false;

  entry.uid = uidToString(best.uid, best.uidLength);
  entry.tagType = String(best.tagType);
  entry.content = String();
  if (best.contentLength) {
    entry.content.reserve(best.contentLength);
    for (uint16_t i = 0; i < best.contentLength; ++i) {
      entry.content += best.content[i];
    }
  }
  entry.hitCount = best.hitCount;
  entry.lastSeenId = best.lastSeenId;
  beforeId = best.lastSeenId;
  return true;
}

bool deleteNfcLogEntry(uint32_t lastSeenId) {
  if (!logReady || lastSeenId == 0 || storedCount == 0) return false;

  for (uint16_t slot = 0; slot < NFC_LOG_SLOT_COUNT; ++slot) {
    NfcLogRecord record = {};
    if (!readRecord(slot, record)) continue;
    if (!validRecord(record) || record.lastSeenId != lastSeenId) continue;

    // Zeroing retires the slot: every reader tests the magic and the checksum,
    // and a new sighting of this UID will simply take a slot of its own.
    const NfcLogRecord empty = {};
    if (!writeRecord(slot, empty)) return false;
    if (storedCount > 0) --storedCount;
    Serial.printf("[NFCLOG] Entry #%lu deleted from slot %u\r\n",
                  static_cast<unsigned long>(lastSeenId),
                  static_cast<unsigned>(slot));
    return true;
  }
  return false;
}

bool clearNfcLog() {
  if (!logReady) return false;
  if (logFile) logFile.close();
  LittleFS.remove(NFC_LOG_PATH);
  return setupNfcLog();
}
