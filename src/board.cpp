#include <Arduino.h>
#include <LittleFS.h>
#include <cstddef>
#include <cstdio>
#include <cstring>

#include "board.h"
#include "badge_settings.h"
#include "usb_console.h"

namespace {

constexpr char BOARD_PATH[] = "/board.dat";
constexpr char BOARD_LEGACY_IMAGE_PATH[] = "/board_img.dat";
constexpr uint32_t BOARD_FILE_MAGIC = 0x424D5342UL;    // "BMSB"
constexpr uint32_t BOARD_RECORD_MAGIC = 0x50535442UL;  // "PSTB"
constexpr uint32_t BOARD_IMAGE_RECORD_MAGIC = 0x494D4752UL;  // "IMGR"
constexpr uint16_t BOARD_FORMAT_VERSION = 3;

// The post ring is preallocated so text posts never grow the filesystem. Image
// slots are short sequential files: that avoids the ESP32-S3 LittleFS panic
// triggered by seeking/overwriting a large preallocated image ring.
constexpr uint16_t BOARD_SLOT_COUNT = 512;
constexpr uint16_t BOARD_IMAGE_SLOT_COUNT = 76;

struct __attribute__((packed)) BoardFileHeader {
  uint32_t magic;
  uint16_t version;
  uint16_t slotCount;
  uint16_t slotSize;
  uint16_t reserved;
};

// Every field before the text is grouped at the front so a lookup can read a
// 20-byte prefix instead of the whole record.
struct __attribute__((packed)) BoardRecord {
  uint32_t magic;
  uint32_t id;
  uint32_t createdAt;
  uint16_t textLength;
  uint16_t imageSlot;  // BOARD_NO_IMAGE when the post carries no picture
  uint16_t imageLength;
  uint16_t authorId; // Previously the zero upper half of imageLength.
  char text[BOARD_MAX_TEXT_LENGTH];
  uint32_t checksum;
};

static_assert(sizeof(BoardRecord) == 304, "Unexpected BoardRecord packing");
static_assert(offsetof(BoardRecord, text) == 20,
              "Unexpected BoardRecord prefix size");

// The image slot names the post it belongs to. A post whose slot has since
// been claimed by a newer post has simply lost its picture, and says so
// rather than serving somebody else's.
struct __attribute__((packed)) BoardImageHeader {
  uint32_t magic;
  uint32_t postId;
  uint32_t length;
  uint32_t checksum;
};

static_assert(sizeof(BoardImageHeader) == 16,
              "Unexpected BoardImageHeader packing");

File boardFile;
bool boardReady = false;
uint16_t nextSlot = 0;
uint32_t nextId = 1;
uint32_t highestStoredId = 0;

// Records the first number not yet used, before the record that uses it is
// written. Losing power between the two skips a number, which is harmless;
// doing it the other way round could hand the same number out twice, which is
// the one thing this must never do. Notes arrive at human pace and NVS
// wear-levels, so writing on each one costs nothing worth saving.
uint16_t storedCount = 0;
uint16_t nextImageSlot = 0;

void imageSlotPath(uint16_t slot, char *path, size_t capacity) {
  snprintf(path, capacity, "/board_img_%u.dat", static_cast<unsigned>(slot));
}

uint32_t fnv1aUpdate(uint32_t hash, const uint8_t *data, size_t length) {
  for (size_t i = 0; i < length; ++i) {
    hash ^= data[i];
    hash *= 16777619UL;
  }
  return hash;
}

uint32_t fnv1a(const uint8_t *data, size_t length) {
  return fnv1aUpdate(2166136261UL, data, length);
}

uint32_t recordChecksum(const BoardRecord &record) {
  return fnv1a(reinterpret_cast<const uint8_t *>(&record),
               offsetof(BoardRecord, checksum));
}

uint32_t slotOffset(uint16_t slot) {
  return sizeof(BoardFileHeader) +
         static_cast<uint32_t>(slot) * sizeof(BoardRecord);
}

bool validRecord(const BoardRecord &record) {
  return record.magic == BOARD_RECORD_MAGIC && record.id != 0 &&
         record.textLength <= BOARD_MAX_TEXT_LENGTH &&
         record.checksum == recordChecksum(record);
}

bool readRecord(uint16_t slot, BoardRecord &record) {
  if (!boardFile || !boardFile.seek(slotOffset(slot))) return false;
  return boardFile.read(reinterpret_cast<uint8_t *>(&record),
                        sizeof(record)) == sizeof(record);
}

// commit=false leaves the sync to the caller. One post must be durable the
// moment it is accepted, but a bulk rewrite that flushes every record forces a
// LittleFS sync per slot, and 512 of those block the Arduino loop for minutes
// -- no HTTP, no TUI, frozen LEDs, while lwIP keeps answering pings.
bool writeRecord(uint16_t slot, const BoardRecord &record, bool commit = true) {
  if (!boardFile || !boardFile.seek(slotOffset(slot))) return false;
  const size_t written = boardFile.write(
      reinterpret_cast<const uint8_t *>(&record), sizeof(record));
  if (commit) boardFile.flush();
  return written == sizeof(record);
}

// A fresh ring is written once, in full, so that every later write is an
// in-place overwrite of an existing slot rather than an append that could fail
// halfway through for want of space.
bool createRingFile(const char *path, uint16_t slotCount, uint16_t slotSize) {
  File file = LittleFS.open(path, "w");
  if (!file) {
    Serial.printf("[BOARD] ERROR: Could not create %s\r\n", path);
    return false;
  }

  BoardFileHeader header = {};
  header.magic = BOARD_FILE_MAGIC;
  header.version = BOARD_FORMAT_VERSION;
  header.slotCount = slotCount;
  header.slotSize = slotSize;

  bool ok = file.write(reinterpret_cast<const uint8_t *>(&header),
                       sizeof(header)) == sizeof(header);

  // Written a kilobyte at a time: an image slot is far too large to put on the
  // stack, and the whole point of this pass is that it never allocates.
  uint8_t zeros[256] = {0};
  for (uint32_t written = 0; ok && written < static_cast<uint32_t>(slotCount) *
                                                slotSize;) {
    const size_t chunk =
        min(sizeof(zeros),
            static_cast<size_t>(static_cast<uint32_t>(slotCount) * slotSize -
                                written));
    ok = file.write(zeros, chunk) == chunk;
    written += chunk;
  }
  file.close();

  if (!ok) {
    Serial.printf("[BOARD] ERROR: Could not preallocate %s\r\n", path);
    LittleFS.remove(path);
    return false;
  }

  Serial.printf("[BOARD] Created %s: %u slots of %u bytes\r\n", path,
                static_cast<unsigned>(slotCount),
                static_cast<unsigned>(slotSize));
  return true;
}

bool headerMatches(File &file, uint16_t slotCount, uint16_t slotSize) {
  BoardFileHeader header = {};
  if (!file.seek(0) ||
      file.read(reinterpret_cast<uint8_t *>(&header), sizeof(header)) !=
          sizeof(header)) {
    return false;
  }

  return header.magic == BOARD_FILE_MAGIC &&
         header.version == BOARD_FORMAT_VERSION &&
         header.slotCount == slotCount && header.slotSize == slotSize;
}

// Opens a ring, recreating it when it is missing or was written by a build
// with a different layout.
bool openRing(File &file, const char *path, uint16_t slotCount,
              uint16_t slotSize) {
  // On a filesystem that has never held this ring, exists() logs one VFS error
  // before returning false. That single line at first boot is expected.
  if (!LittleFS.exists(path) && !createRingFile(path, slotCount, slotSize)) {
    return false;
  }

  file = LittleFS.open(path, "r+");
  if (file && headerMatches(file, slotCount, slotSize)) return true;

  Serial.printf("[BOARD] %s uses a different layout; starting a new one\r\n",
                path);
  if (file) file.close();
  LittleFS.remove(path);
  if (!createRingFile(path, slotCount, slotSize)) return false;

  file = LittleFS.open(path, "r+");
  return file && headerMatches(file, slotCount, slotSize);
}

// Cheap probe: does this image slot still belong to this post? The stored
// checksum covers the payload too, but verifying that means reading twelve
// kilobytes, so a listing checks only ownership and readBoardImage() does the
// full check when the bytes are actually served.
bool imageSlotHoldsPost(uint16_t slot, uint32_t postId, uint32_t &length) {
  if (slot >= BOARD_IMAGE_SLOT_COUNT) return false;

  char path[24] = {};
  imageSlotPath(slot, path, sizeof(path));
  if (!LittleFS.exists(path)) return false;
  File file = LittleFS.open(path, "r");
  if (!file) return false;

  BoardImageHeader header = {};
  const bool readHeader =
      file.read(reinterpret_cast<uint8_t *>(&header), sizeof(header)) ==
      sizeof(header);
  file.close();
  if (!readHeader) {
    return false;
  }

  if (header.magic != BOARD_IMAGE_RECORD_MAGIC || header.postId != postId ||
      header.length == 0 || header.length > BOARD_MAX_IMAGE_BYTES) {
    return false;
  }

  length = header.length;
  return true;
}

bool writeImageSlot(uint16_t slot, uint32_t postId, const uint8_t *image,
                    size_t length) {
  if (slot >= BOARD_IMAGE_SLOT_COUNT) return false;

  char path[24] = {};
  imageSlotPath(slot, path, sizeof(path));
  File file = LittleFS.open(path, "w");
  if (!file) return false;

  BoardImageHeader header = {};
  header.magic = BOARD_IMAGE_RECORD_MAGIC;
  header.postId = postId;
  header.length = length;
  header.checksum = fnv1aUpdate(
      fnv1a(reinterpret_cast<const uint8_t *>(&header),
            offsetof(BoardImageHeader, checksum)),
      image, length);

  if (file.write(reinterpret_cast<const uint8_t *>(&header),
                 sizeof(header)) != sizeof(header)) {
    file.close();
    return false;
  }
  if (file.write(image, length) != length) {
    file.close();
    return false;
  }

  // Images are written as short, sequential files. The previous in-place
  // image ring made the next seek/flush enter LittleFS's allocator with a
  // malformed state on the ESP32-S3 and panic with IntegerDivideByZero.
  file.close();
  return true;
}

// The write cursor is recovered by scanning rather than stored in NVS: one
// post would otherwise cost one NVS write, and the scan only touches the first
// twelve bytes of each slot.
void recoverCursor() {
  uint32_t highestId = 0;
  uint16_t highestSlot = 0;
  uint32_t highestImagePost = 0;
  uint16_t highestImageSlot = 0;
  uint16_t referencedImages = 0;
  storedCount = 0;

  for (uint16_t slot = 0; slot < BOARD_SLOT_COUNT; ++slot) {
    BoardRecord record = {};
    if (!readRecord(slot, record) || !validRecord(record)) continue;

    ++storedCount;
    if (record.id > highestId) {
      highestId = record.id;
      highestSlot = slot;
    }

    if (record.imageSlot != BOARD_NO_IMAGE &&
        record.imageSlot < BOARD_IMAGE_SLOT_COUNT && record.imageLength > 0) {
      ++referencedImages;
      if (record.id > highestImagePost) {
        highestImagePost = record.id;
        highestImageSlot = record.imageSlot;
      }
    }
  }

  // Never below what has already been handed out. The ring only knows the ids
  // it currently holds, which says nothing about the ones pruned, cleared, or
  // lost to a filesystem re-flash.
  highestStoredId = highestId;
  nextId = highestId + 1;
  const uint32_t used = getPersistentBoardIdWatermark();
  if (used > nextId) nextId = used;
  nextSlot = (highestId == 0) ? 0 : ((highestSlot + 1) % BOARD_SLOT_COUNT);

  nextImageSlot = (highestImagePost == 0)
                      ? 0
                      : ((highestImageSlot + 1) % BOARD_IMAGE_SLOT_COUNT);

  Serial.printf(
      "[BOARD] %u of %u posts and %u of %u images in use; next id %lu\r\n",
      static_cast<unsigned>(storedCount),
      static_cast<unsigned>(BOARD_SLOT_COUNT),
      static_cast<unsigned>(referencedImages),
      static_cast<unsigned>(BOARD_IMAGE_SLOT_COUNT),
      static_cast<unsigned long>(nextId));
}

bool isPrintableOrNewline(char character) {
  return character == '\n' || (character >= 0x20 && character != 0x7F);
}

String sanitizeText(const String &value, size_t limit) {
  String cleaned;
  cleaned.reserve(min(value.length(), limit));

  for (size_t i = 0; i < value.length() && cleaned.length() < limit; ++i) {
    const char character = value[i];
    if (character == '\r') continue;
    if (isPrintableOrNewline(character)) cleaned += character;
  }

  return cleaned;
}

bool hasVisibleText(const String &value) {
  for (size_t i = 0; i < value.length(); ++i) {
    if (value[i] != ' ' && value[i] != '\n') return true;
  }
  return false;
}

}  // namespace

bool setupBoard() {
  boardReady = false;

  if (!openRing(boardFile, BOARD_PATH, BOARD_SLOT_COUNT,
                sizeof(BoardRecord))) {
    Serial.println("[BOARD] ERROR: The post ring is unavailable");
    return false;
  }

  if (LittleFS.exists(BOARD_LEGACY_IMAGE_PATH) &&
      LittleFS.remove(BOARD_LEGACY_IMAGE_PATH)) {
    Serial.println("[BOARD] Retired the old in-place image ring");
  }

  recoverCursor();
  boardReady = true;
  return true;
}

bool isBoardReady() { return boardReady; }

uint16_t boardStoredCount() { return storedCount; }

uint16_t boardCapacity() { return BOARD_SLOT_COUNT; }

uint16_t boardImageCapacity() { return BOARD_IMAGE_SLOT_COUNT; }

// The newest id actually on the board, which is not nextId - 1: nextId may have
// jumped forward to clear a reservation without any post carrying those numbers.
uint32_t boardNewestId() { return highestStoredId; }

bool addBoardPost(const String &text,
                  uint32_t createdAt,
                  const uint8_t *image,
                  size_t imageLength,
                  String &error,
                  uint16_t authorId,
                  bool textInImage) {
  error = String();

  if (!boardReady) {
    error = F("The board is unavailable.");
    return false;
  }

  const String cleanText = sanitizeText(text, BOARD_MAX_TEXT_LENGTH);
  const bool hasImage = image != nullptr && imageLength > 0;

  if (!hasVisibleText(cleanText) && !hasImage) {
    error = F("Draw, write, or do both.");
    return false;
  }

  if (hasImage && imageLength > BOARD_MAX_IMAGE_BYTES) {
    error = F("The drawing is too big for the badge.");
    return false;
  }

  // Images are JPEGs, checked by signature rather than by the browser's
  // claimed MIME type so the stored bytes can always be served as an image.
  if (hasImage && (imageLength < 4 || image[0] != 0xFF || image[1] != 0xD8 ||
                   image[2] != 0xFF)) {
    error = F("The image must be a JPEG.");
    return false;
  }

  BoardRecord record = {};
  record.magic = BOARD_RECORD_MAGIC;
  record.id = nextId;
  setPersistentBoardIdWatermark(nextId + 1);
  record.createdAt = createdAt;
  // This used to accept only the browser range, which silently discarded
  // NFC_CAPTURE_AUTHOR_ID (1) -- so reader captures were stored unattributed
  // and the board's "(NFC)" tag never once appeared.
  record.authorId = isStorableAuthorId(authorId) ? authorId : 0;
  if (hasImage && textInImage) record.authorId |= 0x8000;
  record.textLength = cleanText.length();
  record.imageSlot = BOARD_NO_IMAGE;
  record.imageLength = 0;
  memcpy(record.text, cleanText.c_str(), cleanText.length());

  // The image goes down first. If it fails the post is refused outright,
  // rather than stored with a reference to bytes that were never written.
  if (hasImage) {
    if (!writeImageSlot(nextImageSlot, record.id, image, imageLength)) {
      error = F("Could not save the drawing.");
      return false;
    }
    record.imageSlot = nextImageSlot;
    record.imageLength = static_cast<uint16_t>(imageLength);
    nextImageSlot = (nextImageSlot + 1) % BOARD_IMAGE_SLOT_COUNT;
  }

  record.checksum = recordChecksum(record);

  BoardRecord replaced = {};
  const bool overwritingPost =
      readRecord(nextSlot, replaced) && validRecord(replaced);

  if (!writeRecord(nextSlot, record)) {
    error = F("The note could not be saved.");
    return false;
  }

  if (!overwritingPost && storedCount < BOARD_SLOT_COUNT) ++storedCount;
  nextSlot = (nextSlot + 1) % BOARD_SLOT_COUNT;
  highestStoredId = record.id;
  ++nextId;

  Serial.printf("[BOARD] Post %lu stored (%u text, %u image bytes)%s\r\n",
                static_cast<unsigned long>(record.id),
                static_cast<unsigned>(record.textLength),
                static_cast<unsigned>(record.imageLength),
                overwritingPost ? "; oldest post pruned" : "");
  return true;
}

bool readNextBoardPost(uint32_t &beforeId, BoardPost &post) {
  if (!boardReady || storedCount == 0) return false;

  // Slots are filled in order, so walking backwards from the write cursor
  // visits posts newest first without needing an index in RAM.
  for (uint16_t step = 0; step < BOARD_SLOT_COUNT; ++step) {
    const uint16_t slot =
        (nextSlot + BOARD_SLOT_COUNT - 1 - step) % BOARD_SLOT_COUNT;

    BoardRecord record = {};
    if (!readRecord(slot, record) || !validRecord(record)) continue;
    if (beforeId != 0 && record.id >= beforeId) continue;

    post.id = record.id;
    post.createdAt = record.createdAt;
    post.authorId = record.authorId & 0x7FFF;
    post.textInImage = (record.authorId & 0x8000) != 0;
    post.text = String();
    post.text.concat(record.text, record.textLength);

    uint32_t imageLength = 0;
    const bool hasPayload = record.imageSlot != BOARD_NO_IMAGE &&
                            imageSlotHoldsPost(record.imageSlot, record.id,
                                               imageLength);
    post.hasImage = hasPayload;
    post.imageLength = hasPayload ? imageLength : 0;

    beforeId = record.id;
    return true;
  }

  return false;
}

size_t readBoardImage(uint32_t postId, uint8_t *buffer, size_t capacity) {
  if (!boardReady || !buffer || postId == 0) return 0;

  // Ids and slots advance together, one per post, so post N is written to slot
  // (N-1) mod the ring length. Checking that slot first turns the usual case
  // into a single read; the scan below still covers anything that ever breaks
  // the correspondence, so correctness never rests on it.
  uint16_t foundSlot = BOARD_SLOT_COUNT;
  const uint16_t guess = (postId - 1) % BOARD_SLOT_COUNT;

  for (uint16_t attempt = 0; attempt <= BOARD_SLOT_COUNT; ++attempt) {
    const uint16_t slot = (attempt == 0) ? guess : (attempt - 1);
    if (attempt > 0 && slot == guess) continue;
    if (!boardFile.seek(slotOffset(slot))) continue;

    // Only the 24-byte prefix is read while hunting, not the full record.
    BoardRecord prefix = {};
    if (boardFile.read(reinterpret_cast<uint8_t *>(&prefix),
                       offsetof(BoardRecord, text)) !=
        offsetof(BoardRecord, text)) {
      continue;
    }

    if (prefix.magic == BOARD_RECORD_MAGIC && prefix.id == postId) {
      foundSlot = slot;
      break;
    }
  }

  if (foundSlot == BOARD_SLOT_COUNT) return 0;

  BoardRecord record = {};
  if (!readRecord(foundSlot, record) || !validRecord(record)) return 0;
  if (record.imageSlot == BOARD_NO_IMAGE) return 0;

  char path[24] = {};
  imageSlotPath(record.imageSlot, path, sizeof(path));
  if (!LittleFS.exists(path)) return 0;
  File file = LittleFS.open(path, "r");
  if (!file) return 0;

  BoardImageHeader stored = {};
  if (file.read(reinterpret_cast<uint8_t *>(&stored), sizeof(stored)) !=
          sizeof(stored) ||
      stored.magic != BOARD_IMAGE_RECORD_MAGIC || stored.postId != postId ||
      stored.length == 0 || stored.length > BOARD_MAX_IMAGE_BYTES ||
      stored.length > capacity || file.read(buffer, stored.length) != stored.length) {
    file.close();
    return 0;
  }

  const uint32_t expected = fnv1aUpdate(
      fnv1a(reinterpret_cast<const uint8_t *>(&stored),
            offsetof(BoardImageHeader, checksum)),
      buffer, stored.length);
  file.close();

  if (stored.checksum != expected) {
    Serial.printf("[BOARD] WARNING: Image for post %lu failed its checksum\r\n",
                  static_cast<unsigned long>(postId));
    return 0;
  }

  return stored.length;
}

bool deleteBoardPost(uint32_t id) {
  if (!boardReady || id == 0 || storedCount == 0) return false;

  for (uint16_t slot = 0; slot < BOARD_SLOT_COUNT; ++slot) {
    BoardRecord record = {};
    if (!readRecord(slot, record)) continue;
    if (!validRecord(record) || record.id != id) continue;

    // The picture goes first. If the slot were emptied first and the image
    // write then failed, the post would be gone while its drawing stayed on
    // the filesystem with nothing left to reclaim it.
    if (record.imageSlot != BOARD_NO_IMAGE &&
        record.imageSlot < BOARD_IMAGE_SLOT_COUNT) {
      uint32_t length = 0;
      if (imageSlotHoldsPost(record.imageSlot, record.id, length)) {
        char path[24] = {};
        imageSlotPath(record.imageSlot, path, sizeof(path));
        if (LittleFS.exists(path)) LittleFS.remove(path);
      }
    }

    // Zeroing the slot is enough to retire it: every reader tests the magic
    // and the checksum, and the write cursor walks the ring by position rather
    // than looking for a free slot, so the hole simply waits its turn.
    const BoardRecord empty = {};
    if (!writeRecord(slot, empty)) return false;
    if (storedCount > 0) --storedCount;
    Serial.printf("[BOARD] Post #%lu deleted from slot %u\r\n",
                  static_cast<unsigned long>(id), static_cast<unsigned>(slot));
    return true;
  }
  return false;
}

bool clearBoard() {
  if (!boardReady) return false;

  BoardRecord empty = {};
  for (uint16_t slot = 0; slot < BOARD_SLOT_COUNT; ++slot) {
    if (!writeRecord(slot, empty, false)) return false;
  }
  if (boardFile) boardFile.flush();   // one sync for the whole wipe

  for (uint16_t slot = 0; slot < BOARD_IMAGE_SLOT_COUNT; ++slot) {
    char path[24] = {};
    imageSlotPath(slot, path, sizeof(path));
    if (LittleFS.exists(path)) LittleFS.remove(path);
  }
  if (LittleFS.exists(BOARD_LEGACY_IMAGE_PATH)) {
    LittleFS.remove(BOARD_LEGACY_IMAGE_PATH);
  }

  nextSlot = 0;
  // nextId deliberately survives: clearing the wall does not un-write the
  // notes that were on it, and a number must never be handed out twice.
  setPersistentBoardIdWatermark(nextId);
  highestStoredId = 0;
  storedCount = 0;
  nextImageSlot = 0;
  Serial.println("[BOARD] Every post and picture was cleared");
  return true;
}
