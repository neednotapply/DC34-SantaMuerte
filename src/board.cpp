#include <Arduino.h>
#include <LittleFS.h>
#include <cstddef>
#include <cstring>

#include "board.h"

namespace {

constexpr char BOARD_PATH[] = "/board.dat";
constexpr char BOARD_IMAGE_PATH[] = "/board_img.dat";
constexpr uint32_t BOARD_FILE_MAGIC = 0x424D5342UL;    // "BMSB"
constexpr uint32_t BOARD_RECORD_MAGIC = 0x50535442UL;  // "PSTB"
constexpr uint32_t BOARD_IMAGE_FILE_MAGIC = 0x494D5342UL;    // "IMSB"
constexpr uint32_t BOARD_IMAGE_RECORD_MAGIC = 0x494D4752UL;  // "IMGR"
constexpr uint16_t BOARD_FORMAT_VERSION = 2;

// Together the two rings come to about 1.05 MB of the 1.5 MB filesystem,
// leaving roughly a quarter of it free for the pages and for the spare blocks
// LittleFS needs to move its own metadata around.
constexpr uint16_t BOARD_SLOT_COUNT = 512;
constexpr uint16_t BOARD_IMAGE_SLOT_COUNT = 64;

constexpr uint8_t BOARD_MAX_TAGS = 8;

struct __attribute__((packed)) BoardFileHeader {
  uint32_t magic;
  uint16_t version;
  uint16_t slotCount;
  uint16_t slotSize;
  uint16_t reserved;
};

// Every field before the variable-length text is grouped at the front so a
// lookup can read a 24-byte prefix instead of the whole 596-byte record.
struct __attribute__((packed)) BoardRecord {
  uint32_t magic;
  uint32_t id;
  uint32_t createdAt;
  uint16_t textLength;
  uint16_t linkLength;
  uint16_t tagsLength;
  uint16_t imageSlot;  // BOARD_NO_IMAGE when the post carries no picture
  uint32_t imageLength;
  char text[BOARD_MAX_TEXT_LENGTH];
  char link[BOARD_MAX_LINK_LENGTH];
  char tags[BOARD_MAX_TAGS_LENGTH];
  uint32_t checksum;
};

static_assert(sizeof(BoardRecord) == 596, "Unexpected BoardRecord packing");
static_assert(offsetof(BoardRecord, text) == 24,
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

constexpr uint32_t BOARD_IMAGE_SLOT_SIZE =
    sizeof(BoardImageHeader) + BOARD_MAX_IMAGE_BYTES;

File boardFile;
File imageFile;
bool boardReady = false;
uint16_t nextSlot = 0;
uint32_t nextId = 1;
uint16_t storedCount = 0;
uint16_t nextImageSlot = 0;

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

uint32_t imageSlotOffset(uint16_t slot) {
  return sizeof(BoardFileHeader) +
         static_cast<uint32_t>(slot) * BOARD_IMAGE_SLOT_SIZE;
}

bool validRecord(const BoardRecord &record) {
  return record.magic == BOARD_RECORD_MAGIC && record.id != 0 &&
         record.textLength <= BOARD_MAX_TEXT_LENGTH &&
         record.linkLength <= BOARD_MAX_LINK_LENGTH &&
         record.tagsLength <= BOARD_MAX_TAGS_LENGTH &&
         record.checksum == recordChecksum(record);
}

bool readRecord(uint16_t slot, BoardRecord &record) {
  if (!boardFile || !boardFile.seek(slotOffset(slot))) return false;
  return boardFile.read(reinterpret_cast<uint8_t *>(&record),
                        sizeof(record)) == sizeof(record);
}

bool writeRecord(uint16_t slot, const BoardRecord &record) {
  if (!boardFile || !boardFile.seek(slotOffset(slot))) return false;
  const size_t written = boardFile.write(
      reinterpret_cast<const uint8_t *>(&record), sizeof(record));
  boardFile.flush();
  return written == sizeof(record);
}

// A fresh ring is written once, in full, so that every later write is an
// in-place overwrite of an existing slot rather than an append that could fail
// halfway through for want of space.
bool createRingFile(const char *path, uint16_t slotCount, uint16_t slotSize) {
  File file = LittleFS.open(path, "w");
  if (!file) {
    Serial.printf("[BOARD] ERROR: Could not create %s\n", path);
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
    Serial.printf("[BOARD] ERROR: Could not preallocate %s\n", path);
    LittleFS.remove(path);
    return false;
  }

  Serial.printf("[BOARD] Created %s: %u slots of %u bytes\n", path,
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

  Serial.printf("[BOARD] %s uses a different layout; starting a new one\n",
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
  if (!imageFile || slot >= BOARD_IMAGE_SLOT_COUNT) return false;
  if (!imageFile.seek(imageSlotOffset(slot))) return false;

  BoardImageHeader header = {};
  if (imageFile.read(reinterpret_cast<uint8_t *>(&header), sizeof(header)) !=
      sizeof(header)) {
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
  if (!imageFile || slot >= BOARD_IMAGE_SLOT_COUNT) return false;

  BoardImageHeader header = {};
  header.magic = BOARD_IMAGE_RECORD_MAGIC;
  header.postId = postId;
  header.length = length;
  header.checksum = fnv1aUpdate(
      fnv1a(reinterpret_cast<const uint8_t *>(&header),
            offsetof(BoardImageHeader, checksum)),
      image, length);

  if (!imageFile.seek(imageSlotOffset(slot))) return false;
  if (imageFile.write(reinterpret_cast<const uint8_t *>(&header),
                      sizeof(header)) != sizeof(header)) {
    return false;
  }
  if (imageFile.write(image, length) != length) return false;

  imageFile.flush();
  return true;
}

// The write cursor is recovered by scanning rather than stored in NVS: one
// post would otherwise cost one NVS write, and the scan only touches the first
// twelve bytes of each slot.
void recoverCursor() {
  uint32_t highestId = 0;
  uint16_t highestSlot = 0;
  storedCount = 0;

  for (uint16_t slot = 0; slot < BOARD_SLOT_COUNT; ++slot) {
    BoardRecord record = {};
    if (!readRecord(slot, record) || !validRecord(record)) continue;

    ++storedCount;
    if (record.id > highestId) {
      highestId = record.id;
      highestSlot = slot;
    }
  }

  nextId = highestId + 1;
  nextSlot = (highestId == 0) ? 0 : ((highestSlot + 1) % BOARD_SLOT_COUNT);

  uint32_t highestImagePost = 0;
  uint16_t highestImageSlot = 0;
  uint16_t storedImages = 0;

  for (uint16_t slot = 0; slot < BOARD_IMAGE_SLOT_COUNT; ++slot) {
    if (!imageFile || !imageFile.seek(imageSlotOffset(slot))) continue;

    BoardImageHeader header = {};
    if (imageFile.read(reinterpret_cast<uint8_t *>(&header), sizeof(header)) !=
            sizeof(header) ||
        header.magic != BOARD_IMAGE_RECORD_MAGIC || header.postId == 0) {
      continue;
    }

    ++storedImages;
    if (header.postId > highestImagePost) {
      highestImagePost = header.postId;
      highestImageSlot = slot;
    }
  }

  nextImageSlot = (highestImagePost == 0)
                      ? 0
                      : ((highestImageSlot + 1) % BOARD_IMAGE_SLOT_COUNT);

  Serial.printf(
      "[BOARD] %u of %u posts and %u of %u images in use; next id %lu\n",
      static_cast<unsigned>(storedCount),
      static_cast<unsigned>(BOARD_SLOT_COUNT),
      static_cast<unsigned>(storedImages),
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

  cleaned.trim();
  return cleaned;
}

// Only http and https survive. The board renders links as media, so a
// javascript: or data: URL reaching a viewer's browser would be an injection
// vector rather than a post.
String sanitizeLink(const String &value) {
  String link = sanitizeText(value, BOARD_MAX_LINK_LENGTH);
  if (link.length() == 0) return link;

  String lowered = link;
  lowered.toLowerCase();
  if (!lowered.startsWith("http://") && !lowered.startsWith("https://")) {
    return String();
  }

  // A link cannot contain whitespace, so anything past the first space was
  // never part of the URL.
  const int space = link.indexOf(' ');
  if (space >= 0) link = link.substring(0, space);
  return link;
}

String sanitizeTags(const String &value) {
  String normalized;
  normalized.reserve(BOARD_MAX_TAGS_LENGTH);

  String current;
  uint8_t tagCount = 0;

  auto commit = [&]() {
    if (current.length() == 0 || tagCount >= BOARD_MAX_TAGS) {
      current = String();
      return;
    }
    if (normalized.length() + current.length() + 1 > BOARD_MAX_TAGS_LENGTH) {
      current = String();
      return;
    }
    if (normalized.length() > 0) normalized += ' ';
    normalized += current;
    ++tagCount;
    current = String();
  };

  for (size_t i = 0; i < value.length(); ++i) {
    const char character = tolower(value[i]);
    if ((character >= 'a' && character <= 'z') ||
        (character >= '0' && character <= '9') || character == '_' ||
        character == '-') {
      if (current.length() < 24) current += character;
    } else {
      commit();
    }
  }
  commit();

  return normalized;
}

bool tagsMatchFilter(const char *tags, uint16_t tagsLength,
                     const String &filter) {
  if (filter.length() == 0) return true;
  if (tagsLength == 0) return false;

  // Padding both sides turns a substring search into a whole-tag search, so
  // filtering on "bone" does not also match "bonepile".
  String haystack = " ";
  haystack.concat(tags, tagsLength);
  haystack += ' ';

  String needle = " " + filter + " ";
  return haystack.indexOf(needle) >= 0;
}

}  // namespace

bool setupBoard() {
  boardReady = false;

  if (!openRing(boardFile, BOARD_PATH, BOARD_SLOT_COUNT,
                sizeof(BoardRecord))) {
    Serial.println("[BOARD] ERROR: The post ring is unavailable");
    return false;
  }

  if (!openRing(imageFile, BOARD_IMAGE_PATH, BOARD_IMAGE_SLOT_COUNT,
                BOARD_IMAGE_SLOT_SIZE)) {
    Serial.println("[BOARD] ERROR: The image ring is unavailable");
    return false;
  }

  recoverCursor();
  boardReady = true;
  return true;
}

bool isBoardReady() { return boardReady; }

uint16_t boardStoredCount() { return storedCount; }

uint16_t boardCapacity() { return BOARD_SLOT_COUNT; }

uint16_t boardImageCapacity() { return BOARD_IMAGE_SLOT_COUNT; }

uint32_t boardNewestId() { return nextId > 1 ? nextId - 1 : 0; }

bool addBoardPost(const String &text,
                  const String &link,
                  const String &tags,
                  uint32_t createdAt,
                  const uint8_t *image,
                  size_t imageLength,
                  String &error) {
  error = String();

  if (!boardReady) {
    error = F("The board storage is unavailable.");
    return false;
  }

  const String cleanText = sanitizeText(text, BOARD_MAX_TEXT_LENGTH);
  const String cleanLink = sanitizeLink(link);
  const String cleanTags = sanitizeTags(tags);
  const bool hasImage = image != nullptr && imageLength > 0;

  if (cleanText.length() == 0 && cleanLink.length() == 0 && !hasImage) {
    error = F("Write something, add a picture, or attach a link.");
    return false;
  }

  if (link.length() > 0 && cleanLink.length() == 0) {
    error = F("Links must start with http:// or https://");
    return false;
  }

  if (hasImage && imageLength > BOARD_MAX_IMAGE_BYTES) {
    error = F("That picture is too large for the badge.");
    return false;
  }

  // JPEG only, checked by its own signature rather than by what the browser
  // claimed, so that what is stored can always be served as image/jpeg.
  if (hasImage && (imageLength < 4 || image[0] != 0xFF || image[1] != 0xD8 ||
                   image[2] != 0xFF)) {
    error = F("Pictures must be JPEG.");
    return false;
  }

  BoardRecord record = {};
  record.magic = BOARD_RECORD_MAGIC;
  record.id = nextId;
  record.createdAt = createdAt;
  record.textLength = cleanText.length();
  record.linkLength = cleanLink.length();
  record.tagsLength = cleanTags.length();
  record.imageSlot = BOARD_NO_IMAGE;
  record.imageLength = 0;
  memcpy(record.text, cleanText.c_str(), cleanText.length());
  memcpy(record.link, cleanLink.c_str(), cleanLink.length());
  memcpy(record.tags, cleanTags.c_str(), cleanTags.length());

  // The image goes down first. If it fails the post is refused outright,
  // rather than stored with a reference to bytes that were never written.
  if (hasImage) {
    if (!writeImageSlot(nextImageSlot, record.id, image, imageLength)) {
      error = F("The picture could not be written to storage.");
      return false;
    }
    record.imageSlot = nextImageSlot;
    record.imageLength = imageLength;
    nextImageSlot = (nextImageSlot + 1) % BOARD_IMAGE_SLOT_COUNT;
  }

  record.checksum = recordChecksum(record);

  BoardRecord replaced = {};
  const bool overwritingPost =
      readRecord(nextSlot, replaced) && validRecord(replaced);

  if (!writeRecord(nextSlot, record)) {
    error = F("The post could not be written to storage.");
    return false;
  }

  if (!overwritingPost && storedCount < BOARD_SLOT_COUNT) ++storedCount;
  nextSlot = (nextSlot + 1) % BOARD_SLOT_COUNT;
  ++nextId;

  Serial.printf("[BOARD] Post %lu stored (%u text, %u link, %u tag, %u image "
                "bytes)%s\n",
                static_cast<unsigned long>(record.id),
                static_cast<unsigned>(record.textLength),
                static_cast<unsigned>(record.linkLength),
                static_cast<unsigned>(record.tagsLength),
                static_cast<unsigned>(record.imageLength),
                overwritingPost ? "; oldest post pruned" : "");
  return true;
}

bool readNextBoardPost(uint32_t &beforeId,
                       const String &tagFilter,
                       BoardPost &post) {
  if (!boardReady || storedCount == 0) return false;

  // Slots are filled in order, so walking backwards from the write cursor
  // visits posts newest first without needing an index in RAM.
  for (uint16_t step = 0; step < BOARD_SLOT_COUNT; ++step) {
    const uint16_t slot =
        (nextSlot + BOARD_SLOT_COUNT - 1 - step) % BOARD_SLOT_COUNT;

    BoardRecord record = {};
    if (!readRecord(slot, record) || !validRecord(record)) continue;
    if (beforeId != 0 && record.id >= beforeId) continue;
    if (!tagsMatchFilter(record.tags, record.tagsLength, tagFilter)) continue;

    post.id = record.id;
    post.createdAt = record.createdAt;
    post.text = String();
    post.text.concat(record.text, record.textLength);
    post.link = String();
    post.link.concat(record.link, record.linkLength);
    post.tags = String();
    post.tags.concat(record.tags, record.tagsLength);

    uint32_t imageLength = 0;
    post.hasImage = record.imageSlot != BOARD_NO_IMAGE &&
                    imageSlotHoldsPost(record.imageSlot, record.id,
                                       imageLength);

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

  uint32_t length = 0;
  if (!imageSlotHoldsPost(record.imageSlot, postId, length)) return 0;
  if (length > capacity) return 0;

  // seek past the header that imageSlotHoldsPost() just validated.
  if (!imageFile.seek(imageSlotOffset(record.imageSlot) +
                      sizeof(BoardImageHeader))) {
    return 0;
  }
  if (imageFile.read(buffer, length) != length) return 0;

  BoardImageHeader header = {};
  header.magic = BOARD_IMAGE_RECORD_MAGIC;
  header.postId = postId;
  header.length = length;
  const uint32_t expected = fnv1aUpdate(
      fnv1a(reinterpret_cast<const uint8_t *>(&header),
            offsetof(BoardImageHeader, checksum)),
      buffer, length);

  if (!imageFile.seek(imageSlotOffset(record.imageSlot))) return 0;
  BoardImageHeader stored = {};
  if (imageFile.read(reinterpret_cast<uint8_t *>(&stored), sizeof(stored)) !=
          sizeof(stored) ||
      stored.checksum != expected) {
    Serial.printf("[BOARD] WARNING: Image for post %lu failed its checksum\n",
                  static_cast<unsigned long>(postId));
    return 0;
  }

  return length;
}

bool clearBoard() {
  if (!boardReady) return false;

  BoardRecord empty = {};
  for (uint16_t slot = 0; slot < BOARD_SLOT_COUNT; ++slot) {
    if (!writeRecord(slot, empty)) return false;
  }

  BoardImageHeader emptyImage = {};
  for (uint16_t slot = 0; slot < BOARD_IMAGE_SLOT_COUNT; ++slot) {
    if (!imageFile.seek(imageSlotOffset(slot))) return false;
    if (imageFile.write(reinterpret_cast<const uint8_t *>(&emptyImage),
                        sizeof(emptyImage)) != sizeof(emptyImage)) {
      return false;
    }
  }
  imageFile.flush();

  nextSlot = 0;
  nextId = 1;
  storedCount = 0;
  nextImageSlot = 0;
  Serial.println("[BOARD] Every post and picture was cleared");
  return true;
}
