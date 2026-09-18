#include "usb_drive.h"

#if !ARDUINO_USB_MODE && defined(SM_USB_DRIVE) && SM_USB_DRIVE

#include <USBMSC.h>
#include <cstring>
#include <new>

#include "board.h"
#include "nfc_log.h"
#include "usb_hid.h"
#include "usb_badusb.h"

namespace {
constexpr uint16_t SECTOR_BYTES = 512;
// 2 MiB is enough for every retained JPEG and remains valid FAT12.
constexpr uint32_t SECTOR_COUNT = 4096;
constexpr uint16_t FAT_SECTORS = 12;
constexpr uint16_t ROOT_ENTRIES = 16;
constexpr uint16_t ROOT_LBA = 1 + FAT_SECTORS;
constexpr uint16_t DATA_LBA = ROOT_LBA + 1;
// FAT12 volume labels are limited to 11 bytes, so the on-disk label omits the
// space. The USB product name below retains the requested display name.
constexpr char VOLUME_LABEL[] = "SANTAMUERTE";
constexpr uint16_t END_OF_CHAIN = 0x0FFF;
// One entry per Field Note, per retained drawing, per saved DuckyScript, per
// saved BadUSB script and per logged tag, plus the readme and the four
// folders. This is a read-only USB snapshot bound, not a storage limit: the
// portal continues to retain scripts beyond what one FAT12 image can export.
constexpr uint16_t MAX_FILES = 768;
constexpr uint32_t READ_ACTIVITY_MS = 280;

constexpr char README[] =
    "SANTA MUERTE // BADGE DRIVE\r\n\r\n"
    "This virtual disk is read-only; the badge remains the only writer.\r\n"
    "Each folder is one page of the portal:\r\n\r\n"
    "  Field Notes  /notes      one text file per note, with its JPEG\r\n"
    "  DuckyScript  /ducky      one text file per saved DuckyScript\r\n"
    "  BadUSB       /badusb     one text file per saved BadUSB script\r\n"
    "  NFC Log      /nfc-log    one text file per tag the reader met\r\n\r\n"
    "Open http://santamuerte.local to write notes and scripts; the log\r\n"
    "fills itself whenever Auto-scan is on.\r\n";

enum class FileKind : uint8_t { README, DIRECTORY, NOTE, IMAGE, SCRIPT, TAG };
enum class Directory : uint8_t { ROOT, NOTES, SCRIPTS, BADUSB, NFCLOG };
struct DriveFile {
  char name[11];
  // The folders carry the page's own name. It does not fit 8.3, so each one is
  // also written as a VFAT long name; `name` stays the short alias a reader
  // without long-name support falls back to. Empty for everything else.
  const char *longName;
  FileKind kind;
  Directory parent;
  uint32_t sourceId;
  uint32_t size;
  uint16_t firstCluster;
  uint16_t clusters;
};

DriveFile files[MAX_FILES] = {};
uint16_t fileCount = 0;
// Script names are user-provided, whereas the virtual FAT snapshot stores
// pointers for VFAT long names. Keep a snapshot-local pool for every file the
// finite FAT12 image can export; it does not cap script storage itself.
constexpr uint16_t MAX_SCRIPT_FILE_NAMES = MAX_FILES;
constexpr size_t SCRIPT_FILE_NAME_CAPACITY = USB_HID_MAX_NAME_LENGTH + sizeof(".txt");
char scriptFileNames[MAX_SCRIPT_FILE_NAMES][SCRIPT_FILE_NAME_CAPACITY] = {};
uint16_t scriptFileNameCount = 0;
// Read from the TinyUSB task, written by the Arduino task that builds the
// snapshot.
volatile bool snapshotReady = false;
uint8_t imageCache[BOARD_MAX_IMAGE_BYTES] = {};
uint32_t cachedImageId = 0;
size_t cachedImageLength = 0;
volatile uint32_t lastReadAt = 0;

struct Slice { uint32_t position, start; uint8_t *destination; uint32_t capacity; };

void emit(Slice &slice, const char *text, size_t length) {
  const uint32_t begin = slice.position, end = begin + length;
  const uint32_t wantedEnd = slice.start + slice.capacity;
  if (slice.destination && end > slice.start && begin < wantedEnd) {
    const uint32_t copyStart = begin > slice.start ? begin : slice.start;
    const uint32_t copyEnd = end < wantedEnd ? end : wantedEnd;
    memcpy(slice.destination + copyStart - slice.start, text + copyStart - begin,
           copyEnd - copyStart);
  }
  slice.position = end;
}
void emit(Slice &slice, const char *text) { emit(slice, text, strlen(text)); }
void emit(Slice &slice, const String &text) { emit(slice, text.c_str(), text.length()); }
void write16(uint8_t *to, uint16_t value) { to[0] = value; to[1] = value >> 8; }
void write32(uint8_t *to, uint32_t value) {
  to[0] = value; to[1] = value >> 8; to[2] = value >> 16; to[3] = value >> 24;
}

// An 8.3 name field is space-padded, never NUL-padded: copying a short stem
// straight in left its terminator sitting in the name, which a host reads as
// part of the filename. Stems shorter than eight characters are padded here so
// callers can hand over a plain "TAG0001".
void writeName(char to[11], const char stem[8], const char extension[3]) {
  memset(to, ' ', 8);
  uint8_t length = 0;
  while (length < 8 && stem[length]) ++length;
  memcpy(to, stem, length);
  memcpy(to + 8, extension, 3);
}
void writeNoteName(char to[11], uint32_t id, const char extension[3]) {
  char stem[11] = {};
  // Match the portal: #0001, #0002, and so on (without the # in a filename).
  snprintf(stem, sizeof(stem), "%04lu", static_cast<unsigned long>(id));
  memset(to, ' ', 8);
  memcpy(to, stem, min(strlen(stem), static_cast<size_t>(8)));
  memcpy(to + 8, extension, 3);
}
DriveFile *append(FileKind kind, Directory parent, uint32_t sourceId,
                  const char stem[8], const char extension[3]) {
  if (fileCount == MAX_FILES) return nullptr;
  DriveFile &file = files[fileCount++];
  memset(&file, 0, sizeof(file));
  writeName(file.name, stem, extension);
  file.kind = kind; file.parent = parent; file.sourceId = sourceId;
  return &file;
}
DriveFile *appendNote(FileKind kind, uint32_t id) {
  if (fileCount == MAX_FILES) return nullptr;
  DriveFile &file = files[fileCount++];
  memset(&file, 0, sizeof(file));
  writeNoteName(file.name, id, kind == FileKind::IMAGE ? "JPG" : "TXT");
  file.kind = kind; file.parent = Directory::NOTES; file.sourceId = id;
  return &file;
}

const char *rememberScriptFileName(const String &scriptName) {
  if (scriptFileNameCount == MAX_SCRIPT_FILE_NAMES) return nullptr;
  char *filename = scriptFileNames[scriptFileNameCount++];
  const size_t length = min(scriptName.length(), SCRIPT_FILE_NAME_CAPACITY - sizeof(".txt"));
  memcpy(filename, scriptName.c_str(), length);
  memcpy(filename + length, ".txt", sizeof(".txt"));
  return filename;
}

DriveFile *appendScript(Directory directory, uint16_t index, const char stem[8],
                        const String &scriptName) {
  DriveFile *file = append(FileKind::SCRIPT, directory, index, stem, "TXT");
  if (!file) return nullptr;
  const char *filename = rememberScriptFileName(scriptName);
  if (!filename) {
    --fileCount;
    return nullptr;
  }
  file->longName = filename;
  return file;
}

bool findPost(uint32_t id, BoardPost &found) {
  uint32_t cursor = 0; BoardPost post;
  while (readNextBoardPost(cursor, post)) {
    if (post.id == id) { found = post; return true; }
  }
  return false;
}
void renderNote(DriveFile &file, Slice &slice) {
  BoardPost post;
  if (!findPost(file.sourceId, post)) {
    emit(slice, "This Field Note is no longer retained by the badge.\r\n"); return;
  }
  emit(slice, "SANTA MUERTE // FIELD NOTE "); emit(slice, String(post.id));
  emit(slice, "\r\nAUTHOR: "); emit(slice, String(post.authorId));
  emit(slice, "\r\nCREATED: "); emit(slice, String(post.createdAt));
  if (post.hasImage) {
    char imageName[16] = {};
    snprintf(imageName, sizeof(imageName), "%04lu.JPG", static_cast<unsigned long>(post.id));
    emit(slice, "\r\nIMAGE: "); emit(slice, imageName);
  }
  emit(slice, "\r\n\r\n");
  if (post.text.length()) emit(slice, post.text);
  else emit(slice, "[image-only Field Note]\r\n");
  if (!post.text.endsWith("\n")) emit(slice, "\r\n");
}
bool loadImage(uint32_t postId) {
  if (cachedImageId == postId && cachedImageLength) return true;
  cachedImageId = 0;
  cachedImageLength = readBoardImage(postId, imageCache, sizeof(imageCache));
  if (!cachedImageLength) return false;
  cachedImageId = postId;
  return true;
}
void renderScript(DriveFile &file, Slice &slice) {
  const bool isBadUsb = file.parent == Directory::BADUSB;
  const String name = isBadUsb ? usbBadUSBPayloadNameAt(static_cast<uint16_t>(file.sourceId))
                                : usbHidPayloadNameAt(static_cast<uint16_t>(file.sourceId));
  String script;
  const bool found = !name.isEmpty() &&
                      (isBadUsb ? usbBadUSBReadPayload(name, script) : usbHidReadPayload(name, script));
  if (!found) {
    emit(slice, "This USB script is no longer retained by the badge.\r\n"); return;
  }
  emit(slice, isBadUsb ? "SANTA MUERTE // BADUSB SCRIPT // " : "SANTA MUERTE // DUCKYSCRIPT // ");
  emit(slice, name);
  emit(slice, "\r\n\r\n"); emit(slice, script);
  if (!script.endsWith("\n")) emit(slice, "\r\n");
}
// sourceId is the tag's position in the newest-first walk, the same way a
// script file is addressed by its index: the log has no stable per-tag id a
// filename could carry, and the drive is a snapshot taken at refresh anyway.
bool findTag(uint32_t index, NfcLogEntry &found) {
  uint32_t cursor = 0; NfcLogEntry entry;
  for (uint32_t seen = 0; nfcLogReadNext(cursor, entry); ++seen) {
    if (seen == index) { found = entry; return true; }
  }
  return false;
}
void renderTag(DriveFile &file, Slice &slice) {
  NfcLogEntry entry;
  if (!findTag(file.sourceId, entry)) {
    emit(slice, "This tag is no longer in the NFC Log.\r\n"); return;
  }
  emit(slice, "SANTA MUERTE // NFC LOG // "); emit(slice, entry.uid);
  emit(slice, "\r\nTYPE: "); emit(slice, entry.tagType.length() ? entry.tagType : String("ISO14443A"));
  emit(slice, "\r\nSEEN: "); emit(slice, String(entry.hitCount));
  emit(slice, entry.hitCount == 1 ? " time" : " times");
  emit(slice, "\r\n\r\n");
  if (entry.content.length()) {
    emit(slice, entry.content);
    if (!entry.content.endsWith("\n")) emit(slice, "\r\n");
  } else {
    emit(slice, "[UID only -- no readable content]\r\n");
  }
}
uint32_t renderedSize(DriveFile &file) {
  Slice slice = {0, 0, nullptr, 0};
  if (file.kind == FileKind::README) emit(slice, README, sizeof(README) - 1);
  else if (file.kind == FileKind::NOTE) renderNote(file, slice);
  else if (file.kind == FileKind::SCRIPT) renderScript(file, slice);
  else if (file.kind == FileKind::TAG) renderTag(file, slice);
  return slice.position;
}
uint8_t entrySlots(const DriveFile &file);
uint16_t directoryEntries(Directory directory) {
  uint16_t entries = 2;  // . and ..
  for (uint16_t i = 0; i < fileCount; ++i) {
    if (files[i].parent == directory && files[i].kind != FileKind::DIRECTORY)
      entries += entrySlots(files[i]);
  }
  return entries;
}
void allocateClusters() {
  uint16_t next = 2;
  const uint16_t last = static_cast<uint16_t>(SECTOR_COUNT - DATA_LBA + 1);
  for (uint16_t i = 0; i < fileCount; ++i) {
    DriveFile &file = files[i];
    file.clusters = static_cast<uint16_t>((file.size + SECTOR_BYTES - 1) / SECTOR_BYTES);
    file.firstCluster = file.clusters ? next : 0;
    if (!file.clusters) continue;
    if (next + file.clusters - 1 > last) {
      file.size = file.firstCluster = file.clusters = 0; continue;
    }
    next += file.clusters;
  }
}
// One folder per portal page that keeps anything, named the way the page is.
DriveFile *appendFolder(Directory directory, const char stem[8], const char *pageName) {
  DriveFile *folder = append(FileKind::DIRECTORY, Directory::ROOT, 0, stem, "   ");
  if (!folder) return nullptr;
  folder->sourceId = static_cast<uint32_t>(directory);
  folder->longName = pageName;
  return folder;
}
void refreshFiles() {
  fileCount = scriptFileNameCount = 0; snapshotReady = false; cachedImageId = cachedImageLength = 0;
  DriveFile *readme = append(FileKind::README, Directory::ROOT, 0, "README  ", "TXT");
  DriveFile *notes = appendFolder(Directory::NOTES, "FIELDNTS", "Field Notes");
  DriveFile *scripts = appendFolder(Directory::SCRIPTS, "SCRIPTNG", "DuckyScript");
  DriveFile *badusb = appendFolder(Directory::BADUSB, "BADUSB  ", "BadUSB");
  DriveFile *tags = appendFolder(Directory::NFCLOG, "NFCLOG  ", "NFC Log");
  if (!readme || !notes || !scripts || !badusb || !tags) return;
  uint32_t cursor = 0; BoardPost post;
  while (readNextBoardPost(cursor, post)) {
    if (!appendNote(FileKind::NOTE, post.id)) break;
    if (post.hasImage) {
      DriveFile *image = appendNote(FileKind::IMAGE, post.id);
      if (!image) break;
      image->size = post.imageLength;
    }
  }
  for (uint16_t i = 0; i < usbHidPayloadCount(); ++i) {
    char stem[9] = {};
    snprintf(stem, sizeof(stem), "D%07u", static_cast<unsigned>(i + 1));
    const String name = usbHidPayloadNameAt(i);
    if (!appendScript(Directory::SCRIPTS, i, stem, name)) break;
  }
  for (uint16_t i = 0; i < usbBadUSBPayloadCount(); ++i) {
    char stem[9] = {};
    snprintf(stem, sizeof(stem), "B%07u", static_cast<unsigned>(i + 1));
    const String name = usbBadUSBPayloadNameAt(i);
    if (!appendScript(Directory::BADUSB, i, stem, name)) break;
  }
  uint32_t tagCursor = 0, tagIndex = 0; NfcLogEntry entry;
  while (nfcLogReadNext(tagCursor, entry)) {
    char stem[9] = {};
    snprintf(stem, sizeof(stem), "TAG%04lu", static_cast<unsigned long>(tagIndex + 1));
    if (!append(FileKind::TAG, Directory::NFCLOG, tagIndex, stem, "TXT")) break;
    ++tagIndex;
  }
  readme->size = renderedSize(*readme);
  for (uint16_t i = 0; i < fileCount; ++i) {
    if (files[i].kind == FileKind::NOTE || files[i].kind == FileKind::SCRIPT ||
        files[i].kind == FileKind::TAG)
      files[i].size = renderedSize(files[i]);
  }
  notes->size = static_cast<uint32_t>(directoryEntries(Directory::NOTES)) * 32;
  scripts->size = static_cast<uint32_t>(directoryEntries(Directory::SCRIPTS)) * 32;
  badusb->size = static_cast<uint32_t>(directoryEntries(Directory::BADUSB)) * 32;
  tags->size = static_cast<uint32_t>(directoryEntries(Directory::NFCLOG)) * 32;
  allocateClusters(); snapshotReady = true;
}

void renderBoot(uint8_t *sector) {
  sector[0] = 0xEB; sector[1] = 0x3C; sector[2] = 0x90;
  memcpy(sector + 3, "SMDRIVE ", 8); write16(sector + 11, SECTOR_BYTES);
  sector[13] = 1; write16(sector + 14, 1); sector[16] = 1;
  write16(sector + 17, ROOT_ENTRIES); write16(sector + 19, SECTOR_COUNT);
  sector[21] = 0xF8; write16(sector + 22, FAT_SECTORS);
  write16(sector + 24, 1); write16(sector + 26, 1);
  sector[36] = 0x00; sector[38] = 0x29; write32(sector + 39, 0x534D3334);
  // Eleven bytes, and it has to be the same string the root directory's label
  // entry carries (renderRoot) -- a mismatch is what fsck.fat reports as a
  // damaged label.
  memcpy(sector + 43, VOLUME_LABEL, sizeof(VOLUME_LABEL) - 1);
  memcpy(sector + 54, "FAT12   ", 8);
  sector[510] = 0x55; sector[511] = 0xAA;
}
void fatByte(uint8_t *sector, uint32_t base, uint32_t offset, uint8_t value, bool high = false) {
  if (offset < base || offset >= base + SECTOR_BYTES) return;
  uint8_t &byte = sector[offset - base];
  byte = high ? static_cast<uint8_t>((byte & 0x0F) | (value << 4)) : value;
}
void fatEntry(uint8_t *sector, uint32_t base, uint16_t cluster, uint16_t value) {
  const uint32_t offset = cluster + cluster / 2;
  if (cluster & 1) {
    fatByte(sector, base, offset, value & 0x0F, true); fatByte(sector, base, offset + 1, value >> 4);
  } else {
    fatByte(sector, base, offset, value); fatByte(sector, base, offset + 1, value >> 8);
  }
}
void renderFat(uint32_t lba, uint8_t *sector) {
  const uint32_t base = (lba - 1) * SECTOR_BYTES;
  fatEntry(sector, base, 0, 0xFF8); fatEntry(sector, base, 1, END_OF_CHAIN);
  for (uint16_t i = 0; i < fileCount; ++i) {
    const DriveFile &file = files[i];
    for (uint16_t c = 0; c < file.clusters; ++c)
      fatEntry(sector, base, file.firstCluster + c, c + 1 == file.clusters ? END_OF_CHAIN : file.firstCluster + c + 1);
  }
}
void renderEntry(uint8_t *entry, const DriveFile &file) {
  const bool directory = file.kind == FileKind::DIRECTORY;
  memcpy(entry, file.name, sizeof(file.name)); entry[11] = directory ? 0x10 : 0x21;
  write16(entry + 26, file.firstCluster);
  // A directory's length is its cluster chain, and the spec says its size field
  // reads zero. The folders' own `size` is still what sizes that chain; writing
  // it here as well is what made fsck.fat offer to repair every folder.
  write32(entry + 28, directory ? 0 : file.size);
}

// VFAT long names. A long name is carried by the run of 0x0F entries sitting
// immediately BEFORE its 8.3 entry, thirteen UTF-16 characters each, numbered
// from 1 and stored last chunk first, with 0x40 marking the one that is last in
// the name and therefore first on disk. Every chunk repeats a checksum of the
// 8.3 alias, which is how a reader knows the run belongs to the entry that
// follows it -- and how it detects an editor that renamed the alias behind a
// long name it did not understand.
constexpr uint8_t LONG_NAME_CHARS = 13;
uint8_t longNameEntries(const DriveFile &file) {
  if (!file.longName) return 0;
  const size_t length = strlen(file.longName);
  return static_cast<uint8_t>((length + LONG_NAME_CHARS - 1) / LONG_NAME_CHARS);
}
uint8_t shortNameChecksum(const char name[11]) {
  uint8_t sum = 0;
  for (uint8_t i = 0; i < 11; ++i)
    sum = static_cast<uint8_t>(((sum & 1) << 7) + (sum >> 1) + static_cast<uint8_t>(name[i]));
  return sum;
}
// Writes chunk `sequence` (1-based) of the name into one 32-byte entry.
void renderLongNameEntry(uint8_t *entry, const DriveFile &file, uint8_t sequence) {
  // The 13 characters are split across three runs inside the entry, around the
  // fields a pre-VFAT reader expects to find in an 8.3 entry.
  static constexpr uint8_t offsets[LONG_NAME_CHARS] = {1,  3,  5,  7,  9,  14, 16,
                                                       18, 20, 22, 24, 28, 30};
  const size_t length = strlen(file.longName);
  const size_t first = static_cast<size_t>(sequence - 1) * LONG_NAME_CHARS;
  memset(entry, 0, 32);
  entry[0] = sequence;
  if (sequence == longNameEntries(file)) entry[0] |= 0x40;
  entry[11] = 0x0F;
  entry[13] = shortNameChecksum(file.name);
  for (uint8_t i = 0; i < LONG_NAME_CHARS; ++i) {
    const size_t index = first + i;
    // One NUL terminates the name; anything past it is padding.
    const uint16_t value = index < length ? static_cast<uint8_t>(file.longName[index])
                                          : (index == length ? 0x0000 : 0xFFFF);
    write16(entry + offsets[i], value);
  }
}
// Entries this file occupies in a directory: its own, plus its long name's.
uint8_t entrySlots(const DriveFile &file) {
  return static_cast<uint8_t>(1 + longNameEntries(file));
}
void renderRoot(uint8_t *sector) {
  memcpy(sector, VOLUME_LABEL, sizeof(VOLUME_LABEL) - 1); sector[11] = 0x08; uint8_t out = 1;
  for (uint16_t i = 0; i < fileCount; ++i) {
    const DriveFile &file = files[i];
    if (file.parent != Directory::ROOT) continue;
    if (out + entrySlots(file) > ROOT_ENTRIES) break;
    for (uint8_t sequence = longNameEntries(file); sequence >= 1; --sequence)
      renderLongNameEntry(sector + out++ * 32, file, sequence);
    renderEntry(sector + out++ * 32, file);
  }
}
void renderDot(uint8_t *entry, bool parent, uint16_t cluster) {
  memset(entry, ' ', 11); entry[0] = '.'; if (parent) entry[1] = '.'; entry[11] = 0x10; write16(entry + 26, cluster);
}
void renderDirectory(const DriveFile &directory, uint32_t offset, uint8_t *sector) {
  const uint32_t first = offset / 32;
  for (uint8_t local = 0; local < 16; ++local) {
    const uint32_t wantedEntry = first + local; uint8_t *entry = sector + local * 32;
    if (wantedEntry == 0) { renderDot(entry, false, directory.firstCluster); continue; }
    if (wantedEntry == 1) { renderDot(entry, true, 0); continue; }
    uint32_t wanted = wantedEntry - 2;
    for (uint16_t i = 0; i < fileCount; ++i) {
      const DriveFile &file = files[i];
      if (file.parent != static_cast<Directory>(directory.sourceId) ||
          file.kind == FileKind::DIRECTORY) continue;
      const uint8_t longEntries = longNameEntries(file);
      if (wanted < longEntries) {
        // Long-name entries are written last chunk first, then the 8.3 alias.
        renderLongNameEntry(entry, file, longEntries - wanted);
        break;
      }
      wanted -= longEntries;
      if (wanted == 0) { renderEntry(entry, file); break; }
      --wanted;
    }
  }
}
void renderFile(DriveFile &file, uint32_t offset, uint8_t *sector) {
  Slice slice = {0, offset, sector, SECTOR_BYTES};
  switch (file.kind) {
    case FileKind::README: emit(slice, README, sizeof(README) - 1); break;
    case FileKind::DIRECTORY: renderDirectory(file, offset, sector); break;
    case FileKind::NOTE: renderNote(file, slice); break;
    case FileKind::IMAGE: if (loadImage(file.sourceId)) emit(slice, reinterpret_cast<const char *>(imageCache), cachedImageLength); break;
    case FileKind::SCRIPT: renderScript(file, slice); break;
    case FileKind::TAG: renderTag(file, slice); break;
  }
}
void renderSector(uint32_t lba, uint8_t *sector) {
  if (lba == 0) renderBoot(sector);
  else if (lba <= FAT_SECTORS) renderFat(lba, sector);
  else if (lba == ROOT_LBA) renderRoot(sector);
  else if (lba >= DATA_LBA) {
    const uint16_t cluster = lba - DATA_LBA + 2;
    for (uint16_t i = 0; i < fileCount; ++i) {
      DriveFile &file = files[i];
      if (file.clusters && cluster >= file.firstCluster && cluster < file.firstCluster + file.clusters) {
        renderFile(file, static_cast<uint32_t>(cluster - file.firstCluster) * SECTOR_BYTES, sector); break;
      }
    }
  }
}
int32_t readDrive(uint32_t lba, uint32_t offset, void *buffer, uint32_t size) {
  if (!buffer || !snapshotReady || lba >= SECTOR_COUNT || offset >= SECTOR_BYTES) return -1;
  lastReadAt = millis();
  auto *to = static_cast<uint8_t *>(buffer); uint32_t remaining = size, currentLba = lba, currentOffset = offset;
  while (remaining) {
    if (currentLba >= SECTOR_COUNT) return -1;
    uint8_t sector[SECTOR_BYTES] = {}; renderSector(currentLba, sector);
    const uint32_t count = min(remaining, static_cast<uint32_t>(SECTOR_BYTES - currentOffset));
    memcpy(to, sector + currentOffset, count); to += count; remaining -= count; ++currentLba; currentOffset = 0;
  }
  return static_cast<int32_t>(size);
}
int32_t rejectWrite(uint32_t, uint32_t, uint8_t *, uint32_t) { return -1; }
bool startStop(uint8_t, bool, bool) { return true; }
alignas(USBMSC) uint8_t mscStorage[sizeof(USBMSC)]; USBMSC *msc = nullptr; bool configured = false;
}  // namespace

void usbDriveConfigure(bool enabled) {
  if (!enabled || configured) return;
  msc = new (mscStorage) USBMSC(); msc->vendorID("SANTAMRT"); msc->productID("Santa Muerte"); msc->productRevision("1.2");
  msc->onRead(readDrive); msc->onWrite(rejectWrite); msc->onStartStop(startStop); msc->isWritable(false);
  // Deliberately no mediaPresent(true) here. The snapshot cannot be built until
  // setup() has mounted LittleFS and walked the board and log rings, seconds
  // after the controller enumerates -- and claiming a medium that early made
  // every read in between fail, because readDrive has nothing to answer with.
  // A host that gets an I/O error reading LBA 0 abandons the device instead of
  // retrying, which is why the drive only turned up when something (a profile
  // switch, with its unfamiliar product id and a fresh driver bind) happened to
  // delay that first read past the snapshot. An absent medium is a state hosts
  // do poll out of, so the drive now admits it has nothing until it has
  // something. usbDriveRefresh puts the medium in.
  configured = msc->begin(SECTOR_COUNT, SECTOR_BYTES);
}
void usbDriveRefresh() {
  refreshFiles();
  if (!configured || !msc) return;
  msc->mediaPresent(snapshotReady);
  if (snapshotReady) {
    Serial.printf("[USBMSC] Medium ready: %u files // %u notes // %u ducky // %u badusb // %u tags\r\n",
                  fileCount, boardStoredCount(), usbHidPayloadCount(),
                  usbBadUSBPayloadCount(), nfcLogStoredCount());
  } else {
    Serial.println("[USBMSC] WARNING: snapshot unavailable; the drive stays empty");
  }
}
bool usbDriveReadActive() { return configured && static_cast<uint32_t>(millis() - lastReadAt) < READ_ACTIVITY_MS; }
UsbDriveState getUsbDriveState() {
  return {configured, snapshotReady, boardStoredCount(), usbHidPayloadCount(),
          usbBadUSBPayloadCount(), nfcLogStoredCount()};
}

#else
void usbDriveConfigure(bool) {}
void usbDriveRefresh() {}
bool usbDriveReadActive() { return false; }
UsbDriveState getUsbDriveState() { return {false, false, 0, 0, 0, 0}; }
#endif
