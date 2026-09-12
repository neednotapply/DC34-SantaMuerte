#include "usb_drive.h"

#if !ARDUINO_USB_MODE && defined(SM_USB_DRIVE) && SM_USB_DRIVE

#include <USBMSC.h>
#include <cstring>
#include <new>

#include "board.h"
#include "usb_hid.h"

namespace {

constexpr uint16_t SECTOR_BYTES = 512;
constexpr uint32_t SECTOR_COUNT = 512;  // 256 KiB virtual FAT12 volume.
constexpr uint16_t FAT_SECTORS = 2;
constexpr uint16_t ROOT_ENTRIES = 16;
constexpr uint16_t ROOT_SECTORS = ROOT_ENTRIES * 32 / SECTOR_BYTES;
constexpr uint16_t ROOT_LBA = 1 + FAT_SECTORS;
constexpr uint16_t DATA_LBA = ROOT_LBA + ROOT_SECTORS;
constexpr uint16_t END_OF_CHAIN = 0x0FFF;

constexpr char README[] =
    "SANTA MUERTE // FIELD NOTES DRIVE\r\n"
    "\r\n"
    "This virtual disk is read-only. The badge remains the only writer.\r\n"
    "NOTES.TXT exports Field Notes. SCRIPTS.TXT exports saved USB scripts.\r\n"
    "Use http://santamuerte.local/notes or /scripting to create and edit.\r\n";

enum class FileKind : uint8_t { README, NOTES, SCRIPTS };
struct DriveFile {
  const char name[11];
  FileKind kind;
  uint32_t size;
  uint16_t firstCluster;
  uint16_t clusters;
};

DriveFile files[] = {
    {{'R','E','A','D','M','E',' ',' ','T','X','T'}, FileKind::README, 0, 0, 0},
    {{'N','O','T','E','S',' ',' ',' ','T','X','T'}, FileKind::NOTES, 0, 0, 0},
    {{'S','C','R','I','P','T','S',' ','T','X','T'}, FileKind::SCRIPTS, 0, 0, 0},
};

struct Slice {
  uint32_t position;
  uint32_t start;
  uint8_t *destination;
  uint32_t capacity;
};

void emit(Slice &slice, const char *text, size_t length) {
  const uint32_t begin = slice.position;
  const uint32_t end = begin + length;
  const uint32_t wantedEnd = slice.start + slice.capacity;
  if (slice.destination && end > slice.start && begin < wantedEnd) {
    const uint32_t copyStart = begin > slice.start ? begin : slice.start;
    const uint32_t copyEnd = end < wantedEnd ? end : wantedEnd;
    memcpy(slice.destination + copyStart - slice.start, text + copyStart - begin,
           copyEnd - copyStart);
  }
  slice.position = end;
}

void emit(Slice &slice, const String &text) { emit(slice, text.c_str(), text.length()); }
void emit(Slice &slice, const char *text) { emit(slice, text, strlen(text)); }

void renderNotes(Slice &slice) {
  emit(slice, "SANTA MUERTE // FIELD NOTES\r\n\r\n");
  uint32_t cursor = 0;
  BoardPost post;
  while (readNextBoardPost(cursor, post)) {
    const String heading = String("NOTE ") + String(post.id) +
                           " // author " + String(post.authorId) +
                           " // created " + String(post.createdAt) + "\r\n";
    emit(slice, heading);
    if (post.hasImage) emit(slice, "[image attached; view it in the portal]\r\n");
    emit(slice, post.text);
    emit(slice, "\r\n\r\n");
  }
}

void renderScripts(Slice &slice) {
  emit(slice, "SANTA MUERTE // USB SCRIPTS\r\n\r\n");
  for (uint8_t index = 0; index < usbHidPayloadCount(); ++index) {
    const String name = usbHidPayloadNameAt(index);
    String script;
    if (!usbHidReadPayload(name, script)) continue;
    emit(slice, String("SCRIPT // ") + name + "\r\n");
    emit(slice, script);
    if (!script.endsWith("\n")) emit(slice, "\r\n");
    emit(slice, "\r\n");
  }
}

uint32_t renderedSize(FileKind kind) {
  Slice slice = {0, 0, nullptr, 0};
  if (kind == FileKind::README) emit(slice, README, sizeof(README) - 1);
  else if (kind == FileKind::NOTES) renderNotes(slice);
  else renderScripts(slice);
  return slice.position;
}

void refreshFiles() {
  uint16_t nextCluster = 2;
  for (DriveFile &file : files) {
    file.size = renderedSize(file.kind);
    file.clusters = static_cast<uint16_t>((file.size + SECTOR_BYTES - 1) / SECTOR_BYTES);
    file.firstCluster = file.clusters ? nextCluster : 0;
    nextCluster += file.clusters;
  }
}

void write16(uint8_t *destination, uint16_t value) {
  destination[0] = value & 0xFF;
  destination[1] = value >> 8;
}
void write32(uint8_t *destination, uint32_t value) {
  destination[0] = value & 0xFF;
  destination[1] = (value >> 8) & 0xFF;
  destination[2] = (value >> 16) & 0xFF;
  destination[3] = value >> 24;
}

void renderBoot(uint8_t *sector) {
  sector[0] = 0xEB; sector[1] = 0x3C; sector[2] = 0x90;
  memcpy(sector + 3, "SMDRIVE ", 8);
  write16(sector + 11, SECTOR_BYTES);
  sector[13] = 1; write16(sector + 14, 1); sector[16] = 1;
  write16(sector + 17, ROOT_ENTRIES); write16(sector + 19, SECTOR_COUNT);
  sector[21] = 0xF8; write16(sector + 22, FAT_SECTORS);
  write16(sector + 24, 1); write16(sector + 26, 1);
  sector[36] = 0x80; sector[38] = 0x29; write32(sector + 39, 0x534D3334);
  memcpy(sector + 43, "SANTA MUERTE", 11);
  memcpy(sector + 54, "FAT12   ", 8);
  sector[510] = 0x55; sector[511] = 0xAA;
}

void fatByte(uint8_t *sector, uint32_t base, uint32_t offset, uint8_t value,
             bool highNibble = false) {
  if (offset < base || offset >= base + SECTOR_BYTES) return;
  uint8_t &byte = sector[offset - base];
  byte = highNibble ? static_cast<uint8_t>((byte & 0x0F) | (value << 4)) : value;
}

void fatEntry(uint8_t *sector, uint32_t base, uint16_t cluster, uint16_t value) {
  const uint32_t offset = cluster + cluster / 2;
  if (cluster & 1) {
    fatByte(sector, base, offset, static_cast<uint8_t>(value & 0x0F), true);
    fatByte(sector, base, offset + 1, static_cast<uint8_t>(value >> 4));
  } else {
    fatByte(sector, base, offset, static_cast<uint8_t>(value));
    fatByte(sector, base, offset + 1, static_cast<uint8_t>(value >> 8), false);
  }
}

void renderFat(uint32_t lba, uint8_t *sector) {
  const uint32_t base = (lba - 1) * SECTOR_BYTES;
  fatEntry(sector, base, 0, 0xFF8);
  fatEntry(sector, base, 1, END_OF_CHAIN);
  for (const DriveFile &file : files) {
    for (uint16_t index = 0; index < file.clusters; ++index) {
      const uint16_t cluster = file.firstCluster + index;
      fatEntry(sector, base, cluster,
               index + 1 == file.clusters ? END_OF_CHAIN : cluster + 1);
    }
  }
}

void renderRoot(uint8_t *sector) {
  memcpy(sector, "SANTAMUERTE", 11); sector[11] = 0x08;
  for (size_t index = 0; index < sizeof(files) / sizeof(files[0]); ++index) {
    const DriveFile &file = files[index];
    uint8_t *entry = sector + (index + 1) * 32;
    memcpy(entry, file.name, 11); entry[11] = 0x21;  // read-only + archive
    write16(entry + 26, file.firstCluster);
    write32(entry + 28, file.size);
  }
}

void renderFile(FileKind kind, uint32_t offset, uint8_t *sector) {
  Slice slice = {0, offset, sector, SECTOR_BYTES};
  if (kind == FileKind::README) emit(slice, README, sizeof(README) - 1);
  else if (kind == FileKind::NOTES) renderNotes(slice);
  else renderScripts(slice);
}

int32_t readDrive(uint32_t lba, uint32_t offset, void *buffer, uint32_t size) {
  if (!buffer || offset + size > SECTOR_BYTES || lba >= SECTOR_COUNT) return -1;
  refreshFiles();
  uint8_t sector[SECTOR_BYTES] = {};
  if (lba == 0) renderBoot(sector);
  else if (lba >= 1 && lba <= FAT_SECTORS) renderFat(lba, sector);
  else if (lba == ROOT_LBA) renderRoot(sector);
  else if (lba >= DATA_LBA) {
    const uint16_t cluster = static_cast<uint16_t>(lba - DATA_LBA + 2);
    for (const DriveFile &file : files) {
      if (file.clusters && cluster >= file.firstCluster &&
          cluster < file.firstCluster + file.clusters) {
        renderFile(file.kind, static_cast<uint32_t>(cluster - file.firstCluster) * SECTOR_BYTES, sector);
        break;
      }
    }
  }
  memcpy(buffer, sector + offset, size);
  return size;
}

int32_t rejectWrite(uint32_t, uint32_t, uint8_t *, uint32_t) { return -1; }
bool startStop(uint8_t, bool, bool) { return true; }

alignas(USBMSC) uint8_t mscObjectStorage[sizeof(USBMSC)];
USBMSC *msc = nullptr;
bool configured = false;

}  // namespace

void usbDriveConfigure(bool enabled) {
  if (!enabled || configured) return;
  msc = new (mscObjectStorage) USBMSC();
  msc->vendorID("SANTAMRT");
  msc->productID("Field Notes");
  msc->productRevision("1.0");
  msc->onRead(readDrive);
  msc->onWrite(rejectWrite);
  msc->onStartStop(startStop);
  msc->isWritable(false);
  msc->mediaPresent(true);
  configured = msc->begin(SECTOR_COUNT, SECTOR_BYTES);
}

void usbDriveRefresh() { refreshFiles(); }

UsbDriveState getUsbDriveState() {
  return {configured, boardStoredCount(), usbHidPayloadCount()};
}

#else
void usbDriveConfigure(bool) {}
void usbDriveRefresh() {}
UsbDriveState getUsbDriveState() { return {false, 0, 0}; }
#endif
