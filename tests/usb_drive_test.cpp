// Host-side checks for the virtual USB drive's FAT12 image.
//
// Build and run:
//   g++ -std=gnu++17 -DARDUINO_USB_MODE=0 -DSM_USB_DRIVE=1 -I tests/shim -I src
//       tests/usb_drive_test.cpp src/usb_drive.cpp -o /tmp/usb_drive_test
//       && /tmp/usb_drive_test
//
// The drive is the one artifact a host filesystem driver parses byte for byte,
// so the interesting behaviour is structural: each portal page that keeps
// something has to appear as a folder carrying that page's own name, which
// needs VFAT long-name entries because none of those names fit 8.3; the short
// aliases have to stay readable on their own; and every note, script and logged
// tag has to be reachable through its folder with the right bytes behind it.
//
// It writes the rendered image to /tmp/smdrive.img so a real FAT checker
// (fsck.fat -n) can be pointed at the same bytes.

#include <USBMSC.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "board.h"
#include "nfc_log.h"
#include "usb_drive.h"
#include "usb_hid.h"

USBMSC::ReadCallback USBMSC::read = nullptr;
bool USBMSC::medium = false;

namespace {

constexpr uint32_t SECTOR_BYTES = 512;
constexpr uint32_t SECTOR_COUNT = 4096;

int failures = 0;
int checks = 0;

void check(bool condition, const std::string &what) {
  ++checks;
  if (condition) return;
  ++failures;
  std::printf("  FAIL: %s\n", what.c_str());
}

// --- what the badge is pretending to hold ------------------------------------

struct StubPost {
  uint32_t id;
  std::string text;
  bool hasImage;
};
std::vector<StubPost> posts;
std::vector<std::pair<std::string, std::string>> payloads;  // name, script
std::vector<NfcLogEntry> tags;

// --- the image ---------------------------------------------------------------

std::vector<uint8_t> image;

void renderImage() {
  image.assign(SECTOR_COUNT * SECTOR_BYTES, 0);
  for (uint32_t lba = 0; lba < SECTOR_COUNT; ++lba) {
    const int32_t read = USBMSC::read(lba, 0, image.data() + lba * SECTOR_BYTES,
                                      SECTOR_BYTES);
    if (read == static_cast<int32_t>(SECTOR_BYTES)) continue;
    check(false, "sector " + std::to_string(lba) + " did not render");
    return;
  }
}

uint16_t read16(const uint8_t *from) { return from[0] | (from[1] << 8); }
uint32_t read32(const uint8_t *from) {
  return from[0] | (from[1] << 8) | (from[2] << 16) | (from[3] << 24);
}

// FAT12 geometry, mirrored from the drive so a change to one shows up as a
// mismatch here rather than as a disk a host silently refuses to mount.
constexpr uint32_t FAT_SECTORS = 12;
constexpr uint32_t ROOT_LBA = 1 + FAT_SECTORS;
constexpr uint32_t DATA_LBA = ROOT_LBA + 1;

const uint8_t *sectorAt(uint32_t lba) { return image.data() + lba * SECTOR_BYTES; }
const uint8_t *clusterAt(uint16_t cluster) { return sectorAt(DATA_LBA + cluster - 2); }

uint16_t fatEntry(uint16_t cluster) {
  const uint8_t *fat = sectorAt(1);
  const uint32_t offset = cluster + cluster / 2;
  const uint16_t pair = fat[offset] | (fat[offset + 1] << 8);
  return (cluster & 1) ? (pair >> 4) : (pair & 0x0FFF);
}

struct Entry {
  std::string shortName;   // "NAME    EXT", trailing spaces trimmed per half
  std::string longName;    // empty when the entry carried no VFAT run
  uint8_t attributes = 0;
  uint16_t firstCluster = 0;
  uint32_t size = 0;
};

std::string trimmed(const uint8_t *raw, size_t length) {
  std::string value(reinterpret_cast<const char *>(raw), length);
  while (!value.empty() && value.back() == ' ') value.pop_back();
  return value;
}

// Reassembles one directory's entries, folding each VFAT run into the 8.3 entry
// that follows it. Long-name chunks arrive last-first, so each one is prepended.
std::vector<Entry> parseDirectory(const uint8_t *raw, size_t entryCount) {
  std::vector<Entry> entries;
  std::string pending;
  for (size_t i = 0; i < entryCount; ++i) {
    const uint8_t *record = raw + i * 32;
    if (record[0] == 0x00) break;
    if (record[0] == 0xE5) continue;
    if (record[11] == 0x0F) {
      static constexpr uint8_t offsets[13] = {1,  3,  5,  7,  9,  14, 16,
                                              18, 20, 22, 24, 28, 30};
      std::string chunk;
      for (uint8_t at : offsets) {
        const uint16_t character = read16(record + at);
        if (character == 0x0000 || character == 0xFFFF) break;
        chunk.push_back(static_cast<char>(character));
      }
      pending = chunk + pending;
      continue;
    }
    if (record[11] & 0x08) { pending.clear(); continue; }  // volume label
    Entry entry;
    entry.shortName = trimmed(record, 8);
    const std::string extension = trimmed(record + 8, 3);
    if (!extension.empty()) entry.shortName += "." + extension;
    entry.longName = pending;
    entry.attributes = record[11];
    entry.firstCluster = read16(record + 26);
    entry.size = read32(record + 28);
    entries.push_back(entry);
    pending.clear();
  }
  return entries;
}

std::vector<Entry> rootEntries() { return parseDirectory(sectorAt(ROOT_LBA), 16); }

// A subdirectory is one cluster chain of 32-byte entries; . and .. lead it.
std::vector<Entry> folderEntries(const Entry &folder) {
  std::vector<uint8_t> raw;
  for (uint16_t cluster = folder.firstCluster;
       cluster >= 2 && cluster < 0x0FF8; cluster = fatEntry(cluster)) {
    const uint8_t *from = clusterAt(cluster);
    raw.insert(raw.end(), from, from + SECTOR_BYTES);
  }
  std::vector<Entry> entries = parseDirectory(raw.data(), raw.size() / 32);
  // Drop . and .., which carry no name of their own worth asserting on.
  entries.erase(std::remove_if(entries.begin(), entries.end(),
                               [](const Entry &entry) {
                                 return entry.shortName == "." ||
                                        entry.shortName == "..";
                               }),
                entries.end());
  return entries;
}

std::string fileBytes(const Entry &entry) {
  std::string content;
  uint32_t remaining = entry.size;
  for (uint16_t cluster = entry.firstCluster;
       remaining && cluster >= 2 && cluster < 0x0FF8; cluster = fatEntry(cluster)) {
    const uint32_t count = remaining < SECTOR_BYTES ? remaining : SECTOR_BYTES;
    content.append(reinterpret_cast<const char *>(clusterAt(cluster)), count);
    remaining -= count;
  }
  return content;
}

const Entry *findByLongName(const std::vector<Entry> &entries, const std::string &name) {
  for (const Entry &entry : entries) {
    if (entry.longName == name) return &entry;
  }
  return nullptr;
}

// --- the checks ---------------------------------------------------------------

void checkBootSector() {
  const uint8_t *boot = sectorAt(0);
  check(boot[510] == 0x55 && boot[511] == 0xAA, "boot sector carries its signature");
  check(read16(boot + 11) == SECTOR_BYTES, "boot sector declares 512-byte sectors");
  check(std::string(reinterpret_cast<const char *>(boot + 54), 5) == "FAT12",
        "boot sector declares FAT12");
  // fsck.fat calls a label that disagrees with the root directory's damaged.
  const std::string bootLabel(reinterpret_cast<const char *>(boot + 43), 11);
  const std::string rootLabel(reinterpret_cast<const char *>(sectorAt(ROOT_LBA)), 11);
  check(bootLabel == rootLabel,
        "boot sector and root agree on the volume label, got '" + bootLabel +
            "' and '" + rootLabel + "'");
}

void checkFoldersAreNamedForPages() {
  const std::vector<Entry> root = rootEntries();
  check(root.size() == 4, "root holds the readme and three folders, got " +
                              std::to_string(root.size()));

  const Entry *notes = findByLongName(root, "Field Notes");
  const Entry *scripting = findByLongName(root, "Scripting");
  const Entry *log = findByLongName(root, "NFC Log");
  check(notes != nullptr, "root has a folder named Field Notes");
  check(scripting != nullptr, "root has a folder named Scripting");
  check(log != nullptr, "root has a folder named NFC Log");
  if (!notes || !scripting || !log) return;

  check((notes->attributes & 0x10) != 0, "Field Notes is a directory");
  check((scripting->attributes & 0x10) != 0, "Scripting is a directory");
  check((log->attributes & 0x10) != 0, "NFC Log is a directory");
  // A directory's size field reads zero; its length is the cluster chain.
  check(notes->size == 0 && scripting->size == 0 && log->size == 0,
        "the folders report no size of their own");

  // A reader without long-name support falls back to these, so they have to say
  // what the folder is on their own.
  check(notes->shortName == "FIELDNTS", "Field Notes aliases to FIELDNTS, got " + notes->shortName);
  check(scripting->shortName == "SCRIPTNG", "Scripting aliases to SCRIPTNG, got " + scripting->shortName);
  check(log->shortName == "NFCLOG", "NFC Log aliases to NFCLOG, got " + log->shortName);

  for (const Entry &entry : root) {
    if (entry.shortName == "README.TXT") {
      check(entry.longName.empty(), "the readme needs no long name");
      check(entry.size > 0, "the readme has content");
    }
  }
}

void checkNotesFolder() {
  const std::vector<Entry> root = rootEntries();
  const Entry *notes = findByLongName(root, "Field Notes");
  if (!notes) return;
  const std::vector<Entry> entries = folderEntries(*notes);
  // Two posts, one of them with a drawing: three files.
  check(entries.size() == 3, "Field Notes holds one file per note plus the JPEG, got " +
                                 std::to_string(entries.size()));
  const Entry *first = nullptr;
  for (const Entry &entry : entries) {
    if (entry.shortName == "0002.TXT") first = &entry;
  }
  check(first != nullptr, "the newest note is 0002.TXT");
  if (first) {
    const std::string content = fileBytes(*first);
    check(content.find("SANTA MUERTE // FIELD NOTE 2") == 0, "the note names itself");
    check(content.find("segunda ofrenda") != std::string::npos, "the note carries its text");
    check(content.find("0002.JPG") != std::string::npos, "the note points at its drawing");
  }
}

void checkScriptingFolder() {
  const std::vector<Entry> root = rootEntries();
  const Entry *scripting = findByLongName(root, "Scripting");
  if (!scripting) return;
  const std::vector<Entry> entries = folderEntries(*scripting);
  check(entries.size() == 2, "Scripting holds one file per saved script, got " +
                                 std::to_string(entries.size()));
  if (entries.empty()) return;
  const std::string content = fileBytes(entries[0]);
  check(content.find("SANTA MUERTE // USB SCRIPT // hola") == 0,
        "a script file names the script");
  check(content.find("STRING hola mundo") != std::string::npos,
        "a script file carries the script");
}

void checkNfcLogFolder() {
  const std::vector<Entry> root = rootEntries();
  const Entry *log = findByLongName(root, "NFC Log");
  if (!log) return;
  const std::vector<Entry> entries = folderEntries(*log);
  check(entries.size() == tags.size(), "NFC Log holds one file per tag, got " +
                                           std::to_string(entries.size()));
  if (entries.size() < 2) return;

  const std::string newest = fileBytes(entries[0]);
  check(entries[0].shortName == "TAG0001.TXT", "the newest tag is TAG0001.TXT, got " +
                                                   entries[0].shortName);
  check(newest.find("SANTA MUERTE // NFC LOG // 04:69:CA:1A") == 0,
        "a tag file leads with its UID");
  check(newest.find("TYPE: NFC Forum Type 2") != std::string::npos,
        "a tag file carries the card type");
  check(newest.find("SEEN: 1 time") != std::string::npos,
        "a tag seen once says so in the singular");
  check(newest.find("https://santamuerte.local/notes") != std::string::npos,
        "a tag file carries what was read off it");

  const std::string second = fileBytes(entries[1]);
  check(second.find("SEEN: 4 times") != std::string::npos,
        "a repeat sighting reports its count");

  // The third tag gave up nothing but its UID, which is still worth a file.
  const std::string bare = fileBytes(entries[2]);
  check(bare.find("[UID only -- no readable content]") != std::string::npos,
        "a contentless tag says so rather than rendering empty");
}

void checkClustersDoNotOverlap() {
  std::vector<Entry> all = rootEntries();
  for (const Entry &entry : rootEntries()) {
    if (entry.attributes & 0x10) {
      const std::vector<Entry> inside = folderEntries(entry);
      all.insert(all.end(), inside.begin(), inside.end());
    }
  }
  std::vector<uint16_t> owner(SECTOR_COUNT, 0);
  bool overlapped = false;
  for (size_t i = 0; i < all.size(); ++i) {
    for (uint16_t cluster = all[i].firstCluster;
         cluster >= 2 && cluster < 0x0FF8; cluster = fatEntry(cluster)) {
      if (cluster >= owner.size()) { overlapped = true; break; }
      if (owner[cluster]) { overlapped = true; break; }
      owner[cluster] = static_cast<uint16_t>(i + 1);
    }
  }
  check(!overlapped, "no two files claim the same cluster");
}

void writeImage() {
  std::FILE *out = std::fopen("/tmp/smdrive.img", "wb");
  if (!out) return;
  std::fwrite(image.data(), 1, image.size(), out);
  std::fclose(out);
  std::printf("  wrote /tmp/smdrive.img (%zu bytes)\n", image.size());
}

}  // namespace

// --- stubs for the badge's storage --------------------------------------------

bool readNextBoardPost(uint32_t &beforeId, BoardPost &post) {
  for (auto it = posts.rbegin(); it != posts.rend(); ++it) {
    if (beforeId && it->id >= beforeId) continue;
    post = BoardPost{};
    post.id = it->id;
    post.text = String(it->text);
    post.authorId = 1000;
    post.createdAt = 1700000000 + it->id;
    post.hasImage = it->hasImage;
    post.imageLength = it->hasImage ? 900 : 0;
    beforeId = it->id;
    return true;
  }
  return false;
}
size_t readBoardImage(uint32_t postId, uint8_t *buffer, size_t capacity) {
  for (const StubPost &post : posts) {
    if (post.id != postId || !post.hasImage) continue;
    const size_t length = capacity < 900 ? capacity : 900;
    std::memset(buffer, 0x5A, length);
    return length;
  }
  return 0;
}
uint16_t boardStoredCount() { return static_cast<uint16_t>(posts.size()); }

uint8_t usbHidPayloadCount() { return static_cast<uint8_t>(payloads.size()); }
String usbHidPayloadNameAt(uint8_t index) {
  if (index >= payloads.size()) return String();
  return String(payloads[index].first);
}
bool usbHidReadPayload(const String &name, String &outScript) {
  for (const auto &payload : payloads) {
    if (String(payload.first) == name) { outScript = String(payload.second); return true; }
  }
  return false;
}

uint16_t nfcLogStoredCount() { return static_cast<uint16_t>(tags.size()); }
bool nfcLogReadNext(uint32_t &beforeId, NfcLogEntry &entry) {
  for (const NfcLogEntry &candidate : tags) {
    if (beforeId && candidate.lastSeenId >= beforeId) continue;
    entry = candidate;
    beforeId = candidate.lastSeenId;
    return true;
  }
  return false;
}

int main() {
  posts = {{1, "primera ofrenda", false}, {2, "segunda ofrenda", true}};
  payloads = {{"hola", "STRING hola mundo\nENTER\n"}, {"lock", "GUI l\n"}};

  NfcLogEntry newest;
  newest.uid = "04:69:CA:1A:2B:5C:80";
  newest.tagType = "NFC Forum Type 2 (NTAG / Ultralight)";
  newest.content = "https://santamuerte.local/notes";
  newest.hitCount = 1;
  newest.lastSeenId = 30;
  NfcLogEntry repeated;
  repeated.uid = "A3:51:07:FE";
  repeated.tagType = "MIFARE Classic 1K";
  repeated.content = "Sector 1 block 4: SNEAKREAPER";
  repeated.hitCount = 4;
  repeated.lastSeenId = 20;
  NfcLogEntry bare;
  bare.uid = "BB:0C:D4:19";
  bare.tagType = "ISO14443A";
  bare.hitCount = 2;
  bare.lastSeenId = 10;
  tags = {newest, repeated, bare};

  usbDriveConfigure(true);
  // A host that reads before the snapshot exists must be told there is no
  // medium, not handed an I/O error -- an error makes it give up on the device.
  check(!USBMSC::medium, "the drive offers no medium before the snapshot");
  check(getUsbDriveState().available, "the interface still registers");
  check(!getUsbDriveState().mediaPresent, "and reports no medium");

  usbDriveRefresh();
  check(USBMSC::medium, "the medium goes in once the snapshot is built");
  check(getUsbDriveState().mediaPresent, "and the state says so");
  check(getUsbDriveState().tagCount == tags.size(), "the state counts the logged tags");

  if (!USBMSC::read) {
    std::printf("FAIL: the drive never registered a read callback\n");
    return 1;
  }
  renderImage();

  checkBootSector();
  checkFoldersAreNamedForPages();
  checkNotesFolder();
  checkScriptingFolder();
  checkNfcLogFolder();
  checkClustersDoNotOverlap();
  writeImage();

  std::printf("%d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
