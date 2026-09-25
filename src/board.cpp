#include "board.h"

#include <LittleFS.h>
#include <cstddef>
#include <cstdio>
#include <cstring>

#include "badge_settings.h"
#include "nfc_log.h"

// See board.h for why this one buffer is shared across the loop-task image
// handlers instead of each keeping its own.
uint8_t boardImageShared[BOARD_MAX_IMAGE_BYTES];

namespace {
constexpr char BOARD_DIR[] = "/field_notes";
constexpr char LEGACY_BOARD_PATH[] = "/board.dat";
constexpr char LEGACY_IMAGE_PATH[] = "/board_img.dat";
constexpr uint32_t BOARD_RECORD_MAGIC = 0x50535442UL;
constexpr uint32_t BOARD_IMAGE_RECORD_MAGIC = 0x494D4752UL;
constexpr size_t SCRIPT_STORAGE_RESERVE_BYTES = 8192 + 512;

struct __attribute__((packed)) BoardRecord {
  uint32_t magic, id, createdAt;
  uint16_t textLength, imageSlot, imageLength, authorId;
  char text[BOARD_MAX_TEXT_LENGTH];
  uint32_t checksum;
};
struct __attribute__((packed)) BoardImageHeader {
  uint32_t magic, postId, length, checksum;
};
static_assert(sizeof(BoardRecord) == 304, "Unexpected BoardRecord packing");

bool boardReady = false;
uint32_t nextId = 1, highestStoredId = 0;
uint16_t storedCount = 0;

uint32_t fnv1aUpdate(uint32_t hash, const uint8_t *data, size_t length) {
  for (size_t i = 0; i < length; ++i) { hash ^= data[i]; hash *= 16777619UL; }
  return hash;
}
uint32_t fnv1a(const uint8_t *data, size_t length) { return fnv1aUpdate(2166136261UL, data, length); }
uint32_t recordChecksum(const BoardRecord &record) {
  return fnv1a(reinterpret_cast<const uint8_t *>(&record), offsetof(BoardRecord, checksum));
}
bool validRecord(const BoardRecord &record) {
  return record.magic == BOARD_RECORD_MAGIC && record.id &&
         record.textLength <= BOARD_MAX_TEXT_LENGTH && record.checksum == recordChecksum(record);
}
void postPath(uint32_t id, char *path, size_t capacity) {
  snprintf(path, capacity, "%s/%010lu.note", BOARD_DIR, static_cast<unsigned long>(id));
}
void imagePath(uint32_t id, char *path, size_t capacity) {
  snprintf(path, capacity, "%s/%010lu.jpg", BOARD_DIR, static_cast<unsigned long>(id));
}
bool isNotePath(const char *path) {
  const char *dot = strrchr(path, '.'); return dot && strcmp(dot, ".note") == 0;
}
String noteEntryPath(const char *name) {
  String path(name);
  if (!path.startsWith("/")) { String full(BOARD_DIR); full += '/'; full += path; return full; }
  return path;
}
bool readRecordPath(const char *path, BoardRecord &record) {
  File file = LittleFS.open(path, "r");
  if (!file) return false;
  const bool ok = file.read(reinterpret_cast<uint8_t *>(&record), sizeof(record)) == sizeof(record);
  file.close();
  return ok && validRecord(record);
}
bool writeRecordPath(const char *path, const BoardRecord &record) {
  File file = LittleFS.open(path, "w");
  if (!file) return false;
  const bool ok = file.write(reinterpret_cast<const uint8_t *>(&record), sizeof(record)) == sizeof(record);
  file.close();
  return ok;
}
bool writeImage(uint32_t id, const uint8_t *image, size_t length) {
  char path[48] = {}; imagePath(id, path, sizeof(path));
  File file = LittleFS.open(path, "w");
  if (!file) return false;
  BoardImageHeader header = {};
  header.magic = BOARD_IMAGE_RECORD_MAGIC; header.postId = id; header.length = length;
  header.checksum = fnv1aUpdate(fnv1a(reinterpret_cast<const uint8_t *>(&header), offsetof(BoardImageHeader, checksum)), image, length);
  const bool ok = file.write(reinterpret_cast<const uint8_t *>(&header), sizeof(header)) == sizeof(header) && file.write(image, length) == length;
  file.close();
  if (!ok) LittleFS.remove(path);
  return ok;
}
bool hasVisibleText(const String &value) {
  for (size_t i = 0; i < value.length(); ++i) if (value[i] != ' ' && value[i] != '\n') return true;
  return false;
}
String sanitizeText(const String &value, size_t limit) {
  String cleaned; cleaned.reserve(min(value.length(), limit));
  for (size_t i = 0; i < value.length() && cleaned.length() < limit; ++i) {
    const char c = value[i];
    if (c != '\r' && (c == '\n' || (c >= 0x20 && c != 0x7F))) cleaned += c;
  }
  return cleaned;
}
bool findRecord(uint32_t wantedId, BoardRecord &record, char *path, size_t pathCapacity) {
  File directory = LittleFS.open(BOARD_DIR);
  if (!directory || !directory.isDirectory()) return false;
  for (File entry = directory.openNextFile(); entry; entry = directory.openNextFile()) {
    if (entry.isDirectory() || !isNotePath(entry.name())) continue;
    const String candidate = noteEntryPath(entry.name()); entry.close();
    if (!readRecordPath(candidate.c_str(), record) || record.id != wantedId) continue;
    strncpy(path, candidate.c_str(), pathCapacity - 1); path[pathCapacity - 1] = '\0'; directory.close(); return true;
  }
  directory.close(); return false;
}
bool oldestRecord(BoardRecord &oldest, char *path, size_t pathCapacity) {
  bool found = false;
  File directory = LittleFS.open(BOARD_DIR);
  if (!directory || !directory.isDirectory()) return false;
  for (File entry = directory.openNextFile(); entry; entry = directory.openNextFile()) {
    if (entry.isDirectory() || !isNotePath(entry.name())) continue;
    const String candidate = noteEntryPath(entry.name()); entry.close(); BoardRecord record = {};
    if (!readRecordPath(candidate.c_str(), record)) continue;
    if (!found || record.id < oldest.id) { oldest = record; strncpy(path, candidate.c_str(), pathCapacity - 1); path[pathCapacity - 1] = '\0'; found = true; }
  }
  directory.close(); return found;
}
size_t freeBytes() {
  const size_t total = LittleFS.totalBytes(), used = LittleFS.usedBytes(); return total > used ? total - used : 0;
}
}  // namespace

bool setupBoard() {
  boardReady = false;
  if (!LittleFS.exists(BOARD_DIR) && !LittleFS.mkdir(BOARD_DIR)) {
    Serial.println("[BOARD] ERROR: Could not create Field Notes storage"); return false;
  }
  // Field Notes are disposable: remove the upgrade's permanently-reserved rings
  // rather than preserving a fixed allocation that can block script storage.
  if (LittleFS.exists(LEGACY_BOARD_PATH)) {
    LittleFS.remove(LEGACY_BOARD_PATH);
    for (uint16_t slot = 0; slot < 76; ++slot) { char path[32]; snprintf(path, sizeof(path), "/board_img_%u.dat", slot); if (LittleFS.exists(path)) LittleFS.remove(path); }
    if (LittleFS.exists(LEGACY_IMAGE_PATH)) LittleFS.remove(LEGACY_IMAGE_PATH);
    Serial.println("[BOARD] Retired the legacy fixed post rings");
  }
  storedCount = 0; highestStoredId = 0;
  File directory = LittleFS.open(BOARD_DIR);
  for (File entry = directory.openNextFile(); entry; entry = directory.openNextFile()) {
    if (entry.isDirectory() || !isNotePath(entry.name())) continue;
    const String path = noteEntryPath(entry.name()); entry.close(); BoardRecord record = {};
    if (!readRecordPath(path.c_str(), record)) continue;
    ++storedCount; if (record.id > highestStoredId) highestStoredId = record.id;
  }
  directory.close();
  nextId = highestStoredId + 1;
  const uint32_t watermark = getPersistentBoardIdWatermark(); if (watermark > nextId) nextId = watermark;
  boardReady = true;
  Serial.printf("[BOARD] Recovered %u Field Notes; next id %lu\r\n", static_cast<unsigned>(storedCount), static_cast<unsigned long>(nextId));
  return true;
}
bool isBoardReady() { return boardReady; }
uint16_t boardStoredCount() { return storedCount; }
uint32_t boardNewestId() { return highestStoredId; }
bool reclaimBoardStorage(size_t bytesNeeded) {
  if (!boardReady) return false;
  while (freeBytes() < bytesNeeded && storedCount) {
    BoardRecord oldest = {}; char path[48] = {};
    if (!oldestRecord(oldest, path, sizeof(path)) || !deleteBoardPost(oldest.id)) break;
  }
  return freeBytes() >= bytesNeeded;
}
bool addBoardPost(const String &text, uint32_t createdAt, const uint8_t *image, size_t imageLength, String &error, uint16_t authorId, bool textInImage) {
  error = String();
  if (!boardReady) { error = F("The board is unavailable."); return false; }
  const String cleanText = sanitizeText(text, BOARD_MAX_TEXT_LENGTH); const bool hasImage = image && imageLength;
  if (!hasVisibleText(cleanText) && !hasImage) { error = F("Draw, write, or do both."); return false; }
  if (hasImage && imageLength > BOARD_MAX_IMAGE_BYTES) { error = F("The drawing is too big for the badge."); return false; }
  if (hasImage && (imageLength < 4 || image[0] != 0xFF || image[1] != 0xD8 || image[2] != 0xFF)) { error = F("The image must be a JPEG."); return false; }
  const size_t needed = sizeof(BoardRecord) + (hasImage ? sizeof(BoardImageHeader) + imageLength : 0) + SCRIPT_STORAGE_RESERVE_BYTES;
  reclaimBoardStorage(needed);
  reclaimNfcLogStorage(needed);
  if (freeBytes() < needed) { error = F("Not enough storage after retiring old Field Notes and NFC log entries."); return false; }
  BoardRecord record = {};
  record.magic = BOARD_RECORD_MAGIC; record.id = nextId; record.createdAt = createdAt;
  record.authorId = isStorableAuthorId(authorId) ? authorId : 0; if (hasImage && textInImage) record.authorId |= 0x8000;
  record.textLength = cleanText.length(); record.imageSlot = hasImage ? 0 : BOARD_NO_IMAGE; record.imageLength = hasImage ? imageLength : 0;
  memcpy(record.text, cleanText.c_str(), cleanText.length()); record.checksum = recordChecksum(record);
  char path[48] = {}; postPath(record.id, path, sizeof(path));
  if (hasImage && !writeImage(record.id, image, imageLength)) { error = F("Could not save the drawing."); return false; }
  if (!writeRecordPath(path, record)) { char imageFile[48] = {}; imagePath(record.id, imageFile, sizeof(imageFile)); if (hasImage) LittleFS.remove(imageFile); error = F("The note could not be saved."); return false; }
  setPersistentBoardIdWatermark(record.id + 1); ++nextId; ++storedCount; highestStoredId = record.id;
  Serial.printf("[BOARD] Post %lu stored (%u text, %u image bytes)\r\n", static_cast<unsigned long>(record.id), static_cast<unsigned>(record.textLength), static_cast<unsigned>(record.imageLength));
  return true;
}
bool readNextBoardPost(uint32_t &beforeId, BoardPost &post) {
  if (!boardReady) return false;
  BoardRecord best = {}; bool found = false; File directory = LittleFS.open(BOARD_DIR);
  for (File entry = directory.openNextFile(); entry; entry = directory.openNextFile()) {
    if (entry.isDirectory() || !isNotePath(entry.name())) continue;
    const String path = noteEntryPath(entry.name()); entry.close(); BoardRecord record = {};
    if (!readRecordPath(path.c_str(), record) || (beforeId && record.id >= beforeId)) continue;
    if (!found || record.id > best.id) { best = record; found = true; }
  }
  directory.close(); if (!found) return false;
  post.id = best.id; post.createdAt = best.createdAt; post.authorId = best.authorId & 0x7FFF; post.textInImage = (best.authorId & 0x8000) != 0;
  post.text = String(); post.text.concat(best.text, best.textLength);
  char imageFile[48] = {}; imagePath(best.id, imageFile, sizeof(imageFile)); File image = LittleFS.open(imageFile, "r"); BoardImageHeader header = {};
  post.hasImage = image && image.read(reinterpret_cast<uint8_t *>(&header), sizeof(header)) == sizeof(header) && header.magic == BOARD_IMAGE_RECORD_MAGIC && header.postId == best.id && header.length <= BOARD_MAX_IMAGE_BYTES;
  post.imageLength = post.hasImage ? header.length : 0; if (image) image.close(); beforeId = best.id; return true;
}
size_t readBoardImage(uint32_t postId, uint8_t *buffer, size_t capacity) {
  if (!boardReady || !buffer || !postId) return 0;
  char imageFile[48] = {}; imagePath(postId, imageFile, sizeof(imageFile)); File file = LittleFS.open(imageFile, "r"); BoardImageHeader header = {};
  if (!file || file.read(reinterpret_cast<uint8_t *>(&header), sizeof(header)) != sizeof(header) || header.magic != BOARD_IMAGE_RECORD_MAGIC || header.postId != postId || !header.length || header.length > BOARD_MAX_IMAGE_BYTES || header.length > capacity || file.read(buffer, header.length) != header.length) { if (file) file.close(); return 0; }
  file.close(); const uint32_t checksum = fnv1aUpdate(fnv1a(reinterpret_cast<const uint8_t *>(&header), offsetof(BoardImageHeader, checksum)), buffer, header.length);
  return checksum == header.checksum ? header.length : 0;
}
bool deleteBoardPost(uint32_t id) {
  if (!boardReady || !id) return false;
  BoardRecord record = {}; char path[48] = {}; if (!findRecord(id, record, path, sizeof(path))) return false;
  char imageFile[48] = {}; imagePath(id, imageFile, sizeof(imageFile)); if (LittleFS.exists(imageFile)) LittleFS.remove(imageFile);
  if (!LittleFS.remove(path)) return false; if (storedCount) --storedCount;
  Serial.printf("[BOARD] Post #%lu retired\r\n", static_cast<unsigned long>(id)); return true;
}
bool clearBoard() {
  if (!boardReady) return false;
  while (storedCount) { BoardRecord oldest = {}; char path[48] = {}; if (!oldestRecord(oldest, path, sizeof(path)) || !deleteBoardPost(oldest.id)) return false; }
  highestStoredId = 0; setPersistentBoardIdWatermark(nextId); return true;
}
