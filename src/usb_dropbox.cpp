#include "usb_dropbox.h"

#if !ARDUINO_USB_MODE && defined(SM_USB_DROPBOX) && SM_USB_DROPBOX

#include <USBMSC.h>
#include <dirent.h>
#include <sys/stat.h>
#include <cstdio>
#include <cstring>
#include <new>

#include "esp_partition.h"
#include "esp_vfs_fat.h"
#include "wear_levelling.h"
extern "C" {
#include "diskio_wl.h"  // ff_diskio_get_pdrv_wl: map the wl handle to its FATFS drive
#include "ff.h"         // f_setlabel
}

#include "usb_badusb.h"
#include "usb_dropbox_names.h"
#include "usb_hid.h"

namespace {
constexpr char PARTITION_LABEL[] = "ffat";
constexpr char MOUNT_POINT[] = "/dropbox";
constexpr char DUCKY_DIR[] = "/dropbox/DUCKY";
constexpr char BADUSB_DIR[] = "/dropbox/BADUSB";
constexpr char README_PATH[] = "/dropbox/README.txt";
constexpr uint16_t MSC_BLOCK = 512;
constexpr uint32_t READ_ACTIVITY_MS = 280;
// Import this long after the host's last write: the cue for a host that dropped
// files and walked away without ejecting (the common case with a badge).
constexpr uint32_t IDLE_FLUSH_MS = 2500;
// A little over the 2048-byte payload cap so an over-long file is still read far
// enough for usbHidSavePayload to reject it with its own "too large" message.
constexpr size_t MAX_IMPORT_BYTES = 4096;
// Wear-levelling sector ceiling; the read-modify-write scratch is sized to it.
constexpr uint16_t MAX_SECTOR = 4096;
constexpr uint8_t MAX_REMEMBERED = 40;

const esp_partition_t *partition = nullptr;
wl_handle_t servingHandle = WL_INVALID_HANDLE;
size_t sectorSize = MAX_SECTOR;
uint32_t blockCount = 0;

alignas(USBMSC) uint8_t mscStorage[sizeof(USBMSC)];
USBMSC *msc = nullptr;
bool configured = false;
bool mediumPresent = false;
// An import is mounting/scanning the partition; host block IO must stand off.
volatile bool busy = false;

volatile bool importRequested = false;  // set by the eject/stop callback
volatile bool hostWrote = false;        // set by the write callback
volatile uint32_t lastWriteAt = 0;
volatile uint32_t lastReadAt = 0;

uint16_t importedDucky = 0, importedBadUSB = 0;

// Files already imported this power cycle, so a re-scan (on idle, on eject, on
// the next drop) does not re-save and re-log an untouched file. Keyed by name
// and size, so an in-place edit -- which changes the size -- imports again. Not
// persisted: after a reboot everything present is re-imported, which only
// overwrites identical payloads, so it is harmless.
struct Remembered {
  String name;
  long size;
};
Remembered remembered[MAX_REMEMBERED];
uint8_t rememberedCount = 0;

// MSC transfers are serialized, so one shared scratch sector is safe.
uint8_t sectorBuffer[MAX_SECTOR];

bool alreadyImported(const char *name, long size) {
  for (uint8_t i = 0; i < rememberedCount; ++i)
    if (remembered[i].size == size && remembered[i].name == name) return true;
  return false;
}
void rememberImported(const char *name, long size) {
  for (uint8_t i = 0; i < rememberedCount; ++i) {
    if (remembered[i].name == name) {
      remembered[i].size = size;
      return;
    }
  }
  if (rememberedCount < MAX_REMEMBERED) {
    remembered[rememberedCount].name = name;
    remembered[rememberedCount].size = size;
    ++rememberedCount;
  }
}

// ---- Raw block service: the host owns the FAT, we only move sectors. ----
int32_t dropboxRead(uint32_t lba, uint32_t offset, void *buffer, uint32_t size) {
  if (busy || servingHandle == WL_INVALID_HANDLE || !buffer) return -1;
  const uint64_t addr = static_cast<uint64_t>(lba) * MSC_BLOCK + offset;
  if (addr + size > static_cast<uint64_t>(blockCount) * MSC_BLOCK) return -1;
  lastReadAt = millis();
  if (wl_read(servingHandle, static_cast<size_t>(addr), buffer, size) != ESP_OK) return -1;
  return static_cast<int32_t>(size);
}
// The host writes in 512-byte blocks; wear levelling erases and writes a whole
// sector (4096 B on this build) at a time. Each host write is therefore read-
// modify-written into its containing sector(s): read the sector, overlay the
// incoming bytes, erase it, write it back. A write that covers a full sector
// skips the read.
int32_t dropboxWrite(uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t size) {
  if (busy || servingHandle == WL_INVALID_HANDLE || !buffer) return -1;
  const uint64_t addr = static_cast<uint64_t>(lba) * MSC_BLOCK + offset;
  if (addr + size > static_cast<uint64_t>(blockCount) * MSC_BLOCK) return -1;
  const size_t S = sectorSize;
  uint32_t done = 0;
  while (done < size) {
    const size_t sectorBase = (static_cast<size_t>(addr + done) / S) * S;
    const size_t within = static_cast<size_t>(addr + done) - sectorBase;
    const uint32_t chunk = min(size - done, static_cast<uint32_t>(S - within));
    if (within == 0 && chunk == S) {
      if (wl_erase_range(servingHandle, sectorBase, S) != ESP_OK) return -1;
      if (wl_write(servingHandle, sectorBase, buffer + done, S) != ESP_OK) return -1;
    } else {
      if (wl_read(servingHandle, sectorBase, sectorBuffer, S) != ESP_OK) return -1;
      memcpy(sectorBuffer + within, buffer + done, chunk);
      if (wl_erase_range(servingHandle, sectorBase, S) != ESP_OK) return -1;
      if (wl_write(servingHandle, sectorBase, sectorBuffer, S) != ESP_OK) return -1;
    }
    done += chunk;
  }
  lastWriteAt = millis();
  hostWrote = true;
  return static_cast<int32_t>(size);
}
bool dropboxStartStop(uint8_t, bool start, bool load_eject) {
  // The host ejecting -- or spinning the medium down -- is the clean cue that it
  // has flushed its FAT and released the volume: a safe moment to import.
  if (!start || load_eject) importRequested = true;
  return true;
}

// ---- Local FAT access, for seeding and importing. ----
bool mountLocal(wl_handle_t &handle) {
  esp_vfs_fat_mount_config_t config = {};
  config.format_if_mount_failed = true;  // first boot: the old app1 bytes are not FAT
  config.max_files = 4;
  config.allocation_unit_size = 0;
  return esp_vfs_fat_spiflash_mount_rw_wl(MOUNT_POINT, PARTITION_LABEL, &config, &handle) == ESP_OK;
}
void unmountLocal(wl_handle_t handle) { esp_vfs_fat_spiflash_unmount_rw_wl(MOUNT_POINT, handle); }

// Name the volume so a host shows the drive as "DROP BOX" rather than a generic
// size label. f_setlabel targets a FATFS logical drive by number, which we get
// from the wl handle esp_vfs_fat mounted the partition on. Cosmetic: a failure
// only leaves the volume unlabelled.
void applyVolumeLabel(wl_handle_t handle) {
  const unsigned char pdrv = ff_diskio_get_pdrv_wl(handle);
  if (pdrv == 0xff) return;
  char arg[24];
  snprintf(arg, sizeof(arg), "%u:DROP BOX", static_cast<unsigned>(pdrv));
  const FRESULT result = f_setlabel(arg);
  if (result != FR_OK)
    Serial.printf("[DROPBOX] f_setlabel failed (%d)\r\n", static_cast<int>(result));
}

void ensureFolders() {
  mkdir(DUCKY_DIR, 0777);
  mkdir(BADUSB_DIR, 0777);
  struct stat st;
  if (stat(README_PATH, &st) == 0) return;
  FILE *f = fopen(README_PATH, "w");
  if (!f) return;
  fputs(
      "SANTA MUERTE // DROP BOX\r\n\r\n"
      "Drop a DuckyScript into DUCKY/ or a BadUSB script into BADUSB/ and the\r\n"
      "badge saves it, exactly as if you had pasted it into the portal. The\r\n"
      "file name (without .txt) becomes the script name; max 2048 bytes, up to\r\n"
      "16 scripts per folder. Eject the drive -- or just wait a moment -- and\r\n"
      "the badge imports what you dropped. Saved scripts then appear on the\r\n"
      "read-only Field Notes drive and in the badge's USB Tools menu.\r\n",
      f);
  fclose(f);
}
uint16_t importFolder(const char *path, bool badusb) {
  DIR *dir = opendir(path);
  if (!dir) return 0;
  uint16_t imported = 0;
  for (struct dirent *entry = readdir(dir); entry; entry = readdir(dir)) {
    if (entry->d_type == DT_DIR) continue;
    const char *filename = entry->d_name;
    if (!dropboxIsImportableFilename(filename)) continue;

    char full[160];
    snprintf(full, sizeof(full), "%s/%s", path, filename);
    struct stat st;
    if (stat(full, &st) != 0 || st.st_size <= 0) continue;
    if (alreadyImported(filename, st.st_size)) continue;

    String name;
    if (!dropboxPayloadName(filename, name)) {
      Serial.printf("[DROPBOX] Skipped %s: no usable name\r\n", filename);
      continue;
    }
    FILE *f = fopen(full, "rb");
    if (!f) continue;
    String body;
    const long cap = st.st_size < static_cast<long>(MAX_IMPORT_BYTES)
                         ? st.st_size
                         : static_cast<long>(MAX_IMPORT_BYTES);
    body.reserve(cap + 1);
    int c;
    while ((c = fgetc(f)) != EOF && body.length() < MAX_IMPORT_BYTES)
      body += static_cast<char>(c);
    fclose(f);

    String error;
    const bool ok = badusb ? usbBadUSBSavePayload(name, body, error)
                           : usbHidSavePayload(name, body, error);
    if (ok) {
      rememberImported(filename, st.st_size);
      ++imported;
      Serial.printf("[DROPBOX] Imported %s -> %s (%s)\r\n", filename, name.c_str(),
                    badusb ? "BadUSB" : "DuckyScript");
    } else {
      Serial.printf("[DROPBOX] Rejected %s: %s\r\n", filename, error.c_str());
    }
  }
  closedir(dir);
  return imported;
}
// Drop the medium, take exclusive local access, scan both folders, then hand the
// volume back. Dropping the medium first stops the host issuing block IO, so the
// FAT is quiescent while FATFS reads it; the fresh mount sees whatever the host
// just wrote.
bool runImport() {
  busy = true;
  if (msc) msc->mediaPresent(false);
  mediumPresent = false;
  if (servingHandle != WL_INVALID_HANDLE) {
    wl_unmount(servingHandle);
    servingHandle = WL_INVALID_HANDLE;
  }

  uint16_t ducky = 0, bad = 0;
  wl_handle_t local = WL_INVALID_HANDLE;
  if (mountLocal(local)) {
    ensureFolders();
    ducky = importFolder(DUCKY_DIR, false);
    bad = importFolder(BADUSB_DIR, true);
    unmountLocal(local);
  } else {
    Serial.println("[DROPBOX] WARNING: could not mount ffat to import");
  }

  if (wl_mount(partition, &servingHandle) == ESP_OK) {
    if (msc) msc->mediaPresent(true);
    mediumPresent = true;
  } else {
    Serial.println("[DROPBOX] WARNING: could not remount ffat for serving");
  }
  importedDucky += ducky;
  importedBadUSB += bad;
  busy = false;
  return (ducky + bad) > 0;
}
}  // namespace

void usbDropboxConfigure(bool enabled) {
  if (!enabled || configured) return;
  partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                       ESP_PARTITION_SUBTYPE_DATA_FAT, PARTITION_LABEL);
  if (!partition) {
    Serial.println("[DROPBOX] No ffat partition; writable drive unavailable");
    return;  // Leaves the LUN count untouched -- no broken second drive appears.
  }
  msc = new (mscStorage) USBMSC();
  msc->vendorID("SANTAMRT");
  msc->productID("Drop Box");
  msc->productRevision("1.0");
  msc->onRead(dropboxRead);
  msc->onWrite(dropboxWrite);
  msc->onStartStop(dropboxStartStop);
  msc->isWritable(true);
  configured = true;
}

void usbDropboxBegin() {
  if (!configured || !msc) return;
  // First boot formats the partition; seed the folders and import anything
  // already there (e.g. dropped during a previous power cycle, before an import
  // ran). This mount is local only -- the medium is presented afterwards.
  wl_handle_t local = WL_INVALID_HANDLE;
  if (mountLocal(local)) {
    ensureFolders();
    applyVolumeLabel(local);
    importedDucky += importFolder(DUCKY_DIR, false);
    importedBadUSB += importFolder(BADUSB_DIR, true);
    unmountLocal(local);
  } else {
    Serial.println("[DROPBOX] WARNING: ffat mount/format failed; drive unavailable");
    return;
  }
  if (wl_mount(partition, &servingHandle) != ESP_OK) {
    Serial.println("[DROPBOX] WARNING: ffat serving mount failed");
    return;
  }
  sectorSize = wl_sector_size(servingHandle);
  if (sectorSize == 0 || sectorSize > MAX_SECTOR) sectorSize = MAX_SECTOR;
  blockCount = static_cast<uint32_t>(wl_size(servingHandle) / MSC_BLOCK);
  if (!msc->begin(blockCount, MSC_BLOCK)) {
    Serial.println("[DROPBOX] WARNING: MSC begin failed");
    wl_unmount(servingHandle);
    servingHandle = WL_INVALID_HANDLE;
    return;
  }
  msc->mediaPresent(true);
  mediumPresent = true;
  Serial.printf("[DROPBOX] Writable volume ready: %u blocks x %u B (sector %u B)\r\n",
                blockCount, MSC_BLOCK, static_cast<unsigned>(sectorSize));
}

bool usbDropboxService() {
  if (!configured || busy) return false;
  const bool idleFlush =
      hostWrote && static_cast<uint32_t>(millis() - lastWriteAt) > IDLE_FLUSH_MS;
  if (!importRequested && !idleFlush) return false;
  importRequested = false;
  hostWrote = false;
  return runImport();
}

bool usbDropboxActive() {
  if (!configured) return false;
  const uint32_t now = millis();
  return static_cast<uint32_t>(now - lastReadAt) < READ_ACTIVITY_MS ||
         static_cast<uint32_t>(now - lastWriteAt) < READ_ACTIVITY_MS;
}

UsbDropboxState getUsbDropboxState() {
  return {configured, mediumPresent, static_cast<uint32_t>(blockCount) * MSC_BLOCK,
          importedDucky, importedBadUSB};
}

#else
void usbDropboxConfigure(bool) {}
void usbDropboxBegin() {}
bool usbDropboxService() { return false; }
bool usbDropboxActive() { return false; }
UsbDropboxState getUsbDropboxState() { return {false, false, 0, 0, 0}; }
#endif
