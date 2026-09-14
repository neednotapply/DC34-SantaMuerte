#include "usb_drive.h"

#if !ARDUINO_USB_MODE && defined(SM_USB_DRIVE) && SM_USB_DRIVE

#include <USBMSC.h>
#include <cstring>
#include <new>

#include "board.h"
#include "usb_hid.h"

namespace {
constexpr uint16_t SECTOR_BYTES = 512;
// 2 MiB is enough for every retained JPEG and remains valid FAT12.
constexpr uint32_t SECTOR_COUNT = 4096;
constexpr uint16_t FAT_SECTORS = 12;
constexpr uint16_t ROOT_ENTRIES = 16;
constexpr uint16_t ROOT_LBA = 1 + FAT_SECTORS;
constexpr uint16_t DATA_LBA = ROOT_LBA + 1;
constexpr uint16_t END_OF_CHAIN = 0x0FFF;
constexpr uint16_t MAX_FILES = 640;
constexpr uint32_t READ_ACTIVITY_MS = 280;

constexpr char README[] =
    "SANTA MUERTE // FIELD NOTES DRIVE\r\n\r\n"
    "This virtual disk is read-only; the badge remains the only writer.\r\n"
    "NOTES contains one text file per Field Note and matching JPEG artifacts.\r\n"
    "SCRIPTS contains one text file per saved USB script.\r\n"
    "Use http://santamuerte.local/notes or /scripting to create and edit.\r\n";

enum class FileKind : uint8_t { README, DIRECTORY, NOTE, IMAGE, SCRIPT };
enum class Directory : uint8_t { ROOT, NOTES, SCRIPTS };
struct DriveFile {
  char name[11];
  FileKind kind;
  Directory parent;
  uint32_t sourceId;
  uint32_t size;
  uint16_t firstCluster;
  uint16_t clusters;
};

DriveFile files[MAX_FILES] = {};
uint16_t fileCount = 0;
bool snapshotReady = false;
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

void writeName(char to[11], const char stem[8], const char extension[3]) {
  memcpy(to, stem, 8); memcpy(to + 8, extension, 3);
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
  const String name = usbHidPayloadNameAt(static_cast<uint8_t>(file.sourceId));
  String script;
  if (name.isEmpty() || !usbHidReadPayload(name, script)) {
    emit(slice, "This USB script is no longer retained by the badge.\r\n"); return;
  }
  emit(slice, "SANTA MUERTE // USB SCRIPT // "); emit(slice, name);
  emit(slice, "\r\n\r\n"); emit(slice, script);
  if (!script.endsWith("\n")) emit(slice, "\r\n");
}
uint32_t renderedSize(DriveFile &file) {
  Slice slice = {0, 0, nullptr, 0};
  if (file.kind == FileKind::README) emit(slice, README, sizeof(README) - 1);
  else if (file.kind == FileKind::NOTE) renderNote(file, slice);
  else if (file.kind == FileKind::SCRIPT) renderScript(file, slice);
  return slice.position;
}
uint16_t directoryEntries(Directory directory) {
  uint16_t entries = 2;  // . and ..
  for (uint16_t i = 0; i < fileCount; ++i) {
    if (files[i].parent == directory && files[i].kind != FileKind::DIRECTORY) ++entries;
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
void refreshFiles() {
  fileCount = 0; snapshotReady = false; cachedImageId = cachedImageLength = 0;
  DriveFile *readme = append(FileKind::README, Directory::ROOT, 0, "README  ", "TXT");
  DriveFile *notes = append(FileKind::DIRECTORY, Directory::ROOT, 0, "NOTES   ", "   ");
  DriveFile *scripts = append(FileKind::DIRECTORY, Directory::ROOT, 0, "SCRIPTS ", "   ");
  if (!readme || !notes || !scripts) return;
  notes->sourceId = static_cast<uint32_t>(Directory::NOTES);
  scripts->sourceId = static_cast<uint32_t>(Directory::SCRIPTS);
  uint32_t cursor = 0; BoardPost post;
  while (readNextBoardPost(cursor, post)) {
    if (!appendNote(FileKind::NOTE, post.id)) break;
    if (post.hasImage) {
      DriveFile *image = appendNote(FileKind::IMAGE, post.id);
      if (!image) break;
      image->size = post.imageLength;
    }
  }
  for (uint8_t i = 0; i < usbHidPayloadCount(); ++i) {
    char stem[9] = {};
    snprintf(stem, sizeof(stem), "SCRIPT%02u", static_cast<unsigned>(i + 1));
    if (!append(FileKind::SCRIPT, Directory::SCRIPTS, i, stem, "TXT")) break;
  }
  readme->size = renderedSize(*readme);
  for (uint16_t i = 0; i < fileCount; ++i) {
    if (files[i].kind == FileKind::NOTE || files[i].kind == FileKind::SCRIPT)
      files[i].size = renderedSize(files[i]);
  }
  notes->size = static_cast<uint32_t>(directoryEntries(Directory::NOTES)) * 32;
  scripts->size = static_cast<uint32_t>(directoryEntries(Directory::SCRIPTS)) * 32;
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
  memcpy(sector + 43, "SANTA MUERTE", 11); memcpy(sector + 54, "FAT12   ", 8);
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
  memcpy(entry, file.name, sizeof(file.name)); entry[11] = file.kind == FileKind::DIRECTORY ? 0x10 : 0x21;
  write16(entry + 26, file.firstCluster); write32(entry + 28, file.size);
}
void renderRoot(uint8_t *sector) {
  memcpy(sector, "SANTAMUERTE", 11); sector[11] = 0x08; uint8_t out = 1;
  for (uint16_t i = 0; i < fileCount && out < ROOT_ENTRIES; ++i) {
    if (files[i].parent == Directory::ROOT) renderEntry(sector + out++ * 32, files[i]);
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
      if (wanted-- == 0) { renderEntry(entry, file); break; }
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
  msc = new (mscStorage) USBMSC(); msc->vendorID("SANTAMRT"); msc->productID("Field Notes"); msc->productRevision("1.1");
  msc->onRead(readDrive); msc->onWrite(rejectWrite); msc->onStartStop(startStop); msc->isWritable(false); msc->mediaPresent(true);
  configured = msc->begin(SECTOR_COUNT, SECTOR_BYTES);
}
void usbDriveRefresh() { refreshFiles(); }
bool usbDriveReadActive() { return configured && static_cast<uint32_t>(millis() - lastReadAt) < READ_ACTIVITY_MS; }
UsbDriveState getUsbDriveState() { return {configured, boardStoredCount(), usbHidPayloadCount()}; }

#else
void usbDriveConfigure(bool) {}
void usbDriveRefresh() {}
bool usbDriveReadActive() { return false; }
UsbDriveState getUsbDriveState() { return {false, 0, 0}; }
#endif
