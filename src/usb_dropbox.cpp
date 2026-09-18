#include "usb_dropbox.h"

#if !ARDUINO_USB_MODE && defined(SM_USB_DROPBOX) && SM_USB_DROPBOX

#include <USBMSC.h>
#include <dirent.h>
#include <sys/stat.h>
#include <cstdio>
#include <cstring>
#include <new>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

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
// These are the exact folder names exposed on Santa Muerte. Ofrenda only
// claims files in these matching folders; root-level and unrelated folders
// remain entirely under the host's control.
constexpr char DUCKY_DIR[] = "/dropbox/DuckyScript";
constexpr char BADUSB_DIR[] = "/dropbox/BadUSB";
constexpr char README_PATH[] = "/dropbox/README.txt";
constexpr uint16_t MSC_BLOCK = 512;
constexpr uint32_t READ_ACTIVITY_MS = 280;
// Read one byte past the larger payload cap so the save path can distinguish a
// full-size script from an over-long one without importing truncation.
constexpr size_t MAX_IMPORT_BYTES =
    (USB_HID_MAX_PAYLOAD_BYTES > USB_BADUSB_MAX_PAYLOAD_BYTES
         ? USB_HID_MAX_PAYLOAD_BYTES
         : USB_BADUSB_MAX_PAYLOAD_BYTES) +
    1;
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
// A raw MSC callback is currently touching flash. Kept separate from `busy`:
// `busy` covers the local FAT handoff, whereas this covers a host read/write.
volatile bool ioBusy = false;
StaticSemaphore_t storageMutexBuffer;
SemaphoreHandle_t storageMutex = nullptr;

volatile bool importRequested = false;  // set by the eject/stop callback
volatile bool hostOwnsMedium = false;
volatile uint32_t lastWriteAt = 0;
volatile uint32_t lastReadAt = 0;

uint16_t importedDucky = 0, importedBadUSB = 0;

// Files already imported this power cycle, so a later eject does not re-save
// and re-log an untouched file. Keyed by name
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

bool lockStorage() {
  return storageMutex && xSemaphoreTake(storageMutex, portMAX_DELAY) == pdTRUE;
}
void unlockStorage() { xSemaphoreGive(storageMutex); }

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
  if (busy || !buffer || !lockStorage()) return -1;
  if (busy || servingHandle == WL_INVALID_HANDLE) {
    unlockStorage();
    return -1;
  }
  const uint64_t addr = static_cast<uint64_t>(lba) * MSC_BLOCK + offset;
  if (addr + size > static_cast<uint64_t>(blockCount) * MSC_BLOCK) {
    unlockStorage();
    return -1;
  }
  ioBusy = true;
  hostOwnsMedium = true;
  lastReadAt = millis();
  const esp_err_t result = wl_read(servingHandle, static_cast<size_t>(addr), buffer, size);
  ioBusy = false;
  unlockStorage();
  if (result != ESP_OK) return -1;
  return static_cast<int32_t>(size);
}
// The host writes in 512-byte blocks; wear levelling erases and writes a whole
// sector (4096 B on this build) at a time. Each host write is therefore read-
// modify-written into its containing sector(s): read the sector, overlay the
// incoming bytes, erase it, write it back. A write that covers a full sector
// skips the read.
int32_t dropboxWrite(uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t size) {
  if (busy || !buffer || !lockStorage()) return -1;
  if (busy || servingHandle == WL_INVALID_HANDLE) {
    unlockStorage();
    return -1;
  }
  const uint64_t addr = static_cast<uint64_t>(lba) * MSC_BLOCK + offset;
  if (addr + size > static_cast<uint64_t>(blockCount) * MSC_BLOCK) {
    unlockStorage();
    return -1;
  }
  ioBusy = true;
  hostOwnsMedium = true;
  // Mark the activity before the erase/read-modify-write work begins so the
  // render task shows the normal busy-drive animation throughout the transfer.
  lastWriteAt = millis();
  const size_t S = sectorSize;
  uint32_t done = 0;
  while (done < size) {
    const size_t sectorBase = (static_cast<size_t>(addr + done) / S) * S;
    const size_t within = static_cast<size_t>(addr + done) - sectorBase;
    const uint32_t chunk = min(size - done, static_cast<uint32_t>(S - within));
    if (within == 0 && chunk == S) {
      if (wl_erase_range(servingHandle, sectorBase, S) != ESP_OK) {
        ioBusy = false;
        unlockStorage();
        return -1;
      }
      if (wl_write(servingHandle, sectorBase, buffer + done, S) != ESP_OK) {
        ioBusy = false;
        unlockStorage();
        return -1;
      }
    } else {
      if (wl_read(servingHandle, sectorBase, sectorBuffer, S) != ESP_OK) {
        ioBusy = false;
        unlockStorage();
        return -1;
      }
      memcpy(sectorBuffer + within, buffer + done, chunk);
      if (wl_erase_range(servingHandle, sectorBase, S) != ESP_OK) {
        ioBusy = false;
        unlockStorage();
        return -1;
      }
      if (wl_write(servingHandle, sectorBase, sectorBuffer, S) != ESP_OK) {
        ioBusy = false;
        unlockStorage();
        return -1;
      }
    }
    done += chunk;
  }
  lastWriteAt = millis();
  ioBusy = false;
  unlockStorage();
  return static_cast<int32_t>(size);
}
bool dropboxStartStop(uint8_t, bool start, bool) {
  // The host ejecting -- or spinning the medium down -- is the clean cue that it
  // has flushed its FAT and released the volume: a safe moment to import.
  // LOEJ describes both load and eject. `start=true, load_eject=true` means
  // load, and must not make us withdraw the volume just as the host mounts it.
  if (start) {
    hostOwnsMedium = true;
  } else {
    hostOwnsMedium = false;
    importRequested = true;
  }
  return true;
}

// ---- Local FAT access, for seeding and importing. ----
// Formatting is exclusively a first-boot provisioning step. A later failure
// can mean an interrupted host write, and must leave the host's files intact
// for recovery rather than treating them as disposable.
bool mountLocal(wl_handle_t &handle, bool formatOnFail) {
  esp_vfs_fat_mount_config_t config = {};
  config.format_if_mount_failed = formatOnFail;
  config.max_files = 4;
  config.allocation_unit_size = 0;
  return esp_vfs_fat_spiflash_mount_rw_wl(MOUNT_POINT, PARTITION_LABEL, &config, &handle) == ESP_OK;
}
void unmountLocal(wl_handle_t handle) { esp_vfs_fat_spiflash_unmount_rw_wl(MOUNT_POINT, handle); }

// Name the volume so a host shows the drive as "Ofrenda" rather than a generic
// size label. f_setlabel targets a FATFS logical drive by number, which we get
// from the wl handle esp_vfs_fat mounted the partition on. Cosmetic: a failure
// only leaves the volume unlabelled.
void applyVolumeLabel(wl_handle_t handle) {
  const unsigned char pdrv = ff_diskio_get_pdrv_wl(handle);
  if (pdrv == 0xff) return;
  char arg[24];
  snprintf(arg, sizeof(arg), "%u:OFRENDA", static_cast<unsigned>(pdrv));
  const FRESULT result = f_setlabel(arg);
  if (result != FR_OK)
    Serial.printf("[DROPBOX] f_setlabel failed (%d)\r\n", static_cast<int>(result));
}

void ensureFolders() {
  mkdir(DUCKY_DIR, 0777);
  mkdir(BADUSB_DIR, 0777);
  FILE *f = fopen(README_PATH, "w");
  if (!f) return;
  fputs(
      "SANTA MUERTE // OFRENDA\r\n\r\n"
      "Drop a DuckyScript into DuckyScript/ or a BadUSB script into BadUSB/ and\r\n"
      "the badge moves it into Santa Muerte, exactly as if you had pasted it\r\n"
      "into the portal. The\r\n"
      "file name (without .txt) becomes the script name; max 8192 bytes.\r\n"
      "Scripts are limited by badge storage, not a fixed count. Eject Ofrenda\r\n"
      "after copying so the badge can safely move the files.\r\n"
      "Saved scripts then appear on the\r\n"
      "read-only Santa Muerte drive and in the badge's USB Tools menu. Files\r\n"
      "outside those two folders stay on Ofrenda.\r\n",
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
      // LittleFS and FAT cannot participate in one atomic rename. Saving the
      // payload first protects the user's script; after that succeeds, remove
      // its Ofrenda source to complete the intended move. If removal fails,
      // retain the source rather than risk data loss, and log the exception.
      if (remove(full) == 0) {
        Serial.printf("[OFRENDA] Moved %s -> Santa Muerte/%s (%s)\r\n", filename,
                      name.c_str(), badusb ? "BadUSB" : "DuckyScript");
      } else {
        Serial.printf("[OFRENDA] Saved %s but could not remove its source\r\n", filename);
      }
    } else {
      Serial.printf("[DROPBOX] Rejected %s: %s\r\n", filename, error.c_str());
    }
  }
  closedir(dir);
  return imported;
}

// A payload may have arrived as .txt, .dd, .ducky, or .badusb, so reverse the
// import mapping rather than guessing the original filename. Remove all
// aliases that map to this payload name: keeping even one would restore the
// script on the next boot.
bool deletePayloadSources(const char *path, const String &payloadName, String &error) {
  DIR *dir = opendir(path);
  if (!dir) {
    error = "Could not open the Ofrenda folder.";
    return false;
  }
  bool ok = true;
  for (struct dirent *entry = readdir(dir); entry; entry = readdir(dir)) {
    if (entry->d_type == DT_DIR) continue;
    const char *filename = entry->d_name;
    if (!dropboxIsImportableFilename(filename)) continue;
    String derived;
    if (!dropboxPayloadName(filename, derived) || derived != payloadName) continue;

    char full[160];
    const int written = snprintf(full, sizeof(full), "%s/%s", path, filename);
    if (written < 0 || static_cast<size_t>(written) >= sizeof(full) || remove(full) != 0) {
      ok = false;
      error = "Could not remove the Ofrenda source file.";
      continue;
    }
    Serial.printf("[DROPBOX] Removed %s after portal delete (%s)\r\n", filename,
                  payloadName.c_str());
  }
  closedir(dir);
  return ok;
}

void takeMediumOffline() {
  if (msc) msc->mediaPresent(false);
  mediumPresent = false;
  // Setting `busy` makes callbacks that have not started fail fast. Taking the
  // mutex waits for a callback that passed that first check, and its second
  // check after locking prevents it from touching a handle we just unmounted.
  if (!lockStorage()) return;
  if (servingHandle != WL_INVALID_HANDLE) {
    wl_unmount(servingHandle);
    servingHandle = WL_INVALID_HANDLE;
  }
  unlockStorage();
}

bool restoreMedium(String *error = nullptr) {
  if (!lockStorage()) {
    if (error && error->isEmpty()) *error = "Ofrenda storage lock failed.";
    return false;
  }
  if (wl_mount(partition, &servingHandle) != ESP_OK) {
    unlockStorage();
    Serial.println("[DROPBOX] WARNING: could not remount ffat for serving");
    if (error && error->isEmpty()) *error = "Ofrenda could not be restored.";
    return false;
  }
  unlockStorage();
  if (msc) msc->mediaPresent(true);
  mediumPresent = true;
  return true;
}

// Drop the medium, take exclusive local access, scan both folders, then hand the
// volume back. Dropping the medium first stops the host issuing block IO, so the
// FAT is quiescent while FATFS reads it; the fresh mount sees whatever the host
// just wrote.
bool runImport() {
  busy = true;
  takeMediumOffline();

  uint16_t ducky = 0, bad = 0;
  wl_handle_t local = WL_INVALID_HANDLE;
  if (mountLocal(local, false)) {
    ensureFolders();
    ducky = importFolder(DUCKY_DIR, false);
    bad = importFolder(BADUSB_DIR, true);
    unmountLocal(local);
  } else {
    Serial.println("[DROPBOX] WARNING: could not mount ffat to import");
  }

  restoreMedium();
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
  storageMutex = xSemaphoreCreateMutexStatic(&storageMutexBuffer);
  if (!storageMutex) {
    Serial.println("[DROPBOX] Could not create storage mutex; writable drive unavailable");
    return;
  }
  msc = new (mscStorage) USBMSC();
  msc->vendorID("SANTAMRT");
  msc->productID("Ofrenda");
  msc->productRevision("1.1");
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
  if (mountLocal(local, true)) {
    ensureFolders();
    applyVolumeLabel(local);
    importedDucky += importFolder(DUCKY_DIR, false);
    importedBadUSB += importFolder(BADUSB_DIR, true);
    unmountLocal(local);
  } else {
    Serial.println("[DROPBOX] WARNING: ffat mount/format failed; drive unavailable");
    return;
  }
  if (!restoreMedium()) {
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
  Serial.printf("[OFRENDA] Writable volume ready: %u blocks x %u B (sector %u B)\r\n",
                blockCount, MSC_BLOCK, static_cast<unsigned>(sectorSize));
}

bool usbDropboxService() {
  if (!configured || busy) return false;
  // Moving files changes Ofrenda's FAT. Only do that after START STOP tells us
  // the host has released the volume; an idle timeout is not an ownership
  // handoff and can corrupt a still-mounted host cache.
  if (!importRequested) return false;
  importRequested = false;
  return runImport();
}

bool usbDropboxDeletePayloadSource(bool badusb, const String &name, String &error) {
  error = String();
  // In the Network profile (or after a failed Ofrenda startup) there is no
  // source volume that can later re-import a script, so portal-only payloads
  // remain independently deletable.
  if (!configured || !msc || !mediumPresent) return true;
  // A normal import already removed its source. If the host has mounted the
  // medium again, do not mutate its FAT behind a live cache merely to clean up
  // a legacy or failed source copy; the payload deletion itself can proceed.
  if (hostOwnsMedium) return true;
  if (busy) {
    error = "Ofrenda is busy. Try again in a moment.";
    return false;
  }

  busy = true;
  takeMediumOffline();

  bool removed = false;
  wl_handle_t local = WL_INVALID_HANDLE;
  const char *folder = badusb ? BADUSB_DIR : DUCKY_DIR;
  if (mountLocal(local, false)) {
    removed = deletePayloadSources(folder, name, error);
    unmountLocal(local);
  } else {
    error = "Could not access Ofrenda; the script was kept.";
  }
  const bool restored = restoreMedium(&error);
  busy = false;
  return removed && restored;
}

bool usbDropboxActive() {
  if (!configured) return false;
  const uint32_t now = millis();
  // Use the existing Field Notes "disk busy" renderer for every Dropbox
  // state that owns storage: host reads/writes, their short afterglow, and the
  // period where the LUN is deliberately unavailable for a safe local import
  // or portal-delete handoff.
  return busy || ioBusy || static_cast<uint32_t>(now - lastReadAt) < READ_ACTIVITY_MS ||
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
bool usbDropboxDeletePayloadSource(bool, const String &, String &) { return true; }
bool usbDropboxActive() { return false; }
UsbDropboxState getUsbDropboxState() { return {false, false, 0, 0, 0}; }
#endif
