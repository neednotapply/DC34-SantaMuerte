#pragma once

#include <Arduino.h>

// Opens the badge settings namespace. If no Wi-Fi credential exists, creates
// one password and commits it to NVS with read-back verification. If a stored
// credential exists but cannot be validated, this returns false and does not
// generate a replacement.
bool initializeBadgeSettings();

// Returns the verified NVS passphrase, or an empty string when settings
// initialization failed.
const char *getPersistentWifiPassword();

// Returns whether the SoftAP SSID is hidden. This setting is stored in NVS.
bool getPersistentWifiHidden();

// The access point normally uses its MAC-derived name. A saved custom name is
// validated as a 1–32 byte Wi-Fi SSID and survives restarts and filesystem
// flashes. It never contains a password or other secret.
const char *getPersistentAccessPointSsid();
bool setPersistentAccessPointSsid(const String &ssid, String &error);

// Whether the badge should broadcast its own walk-up access point. This is
// persistent so an owner can keep the AP off while using saved Wi-Fi;
// USB serial can always turn it back on.
bool getPersistentAccessPointEnabled();
bool setPersistentAccessPointEnabled(bool enabled, String &error);

// The portal and USB Altar share one badge-wide language preference. Spanish
// is the first-boot default; callers use true for English.
bool getPersistentEnglishLanguage();
bool setPersistentEnglishLanguage(bool english, String &error);

// Offering numbers only ever count upward. The board itself cannot remember
// that: clearing it rewrites every record, and re-flashing LittleFS replaces
// the whole partition. NVS is a different partition and survives both, so the
// high-water mark lives here. It is a reservation, not the exact next id --
// see BOARD_ID_RESERVE_STEP in board.cpp.
uint32_t getPersistentBoardIdWatermark();
bool setPersistentBoardIdWatermark(uint32_t watermark);

// Validates, stores, and reads back the dashboard Wi-Fi settings. The password
// may be empty for an open access point, or satisfy WPA2's 8 to 63
// printable-ASCII character rule. It is not required to follow the generated
// three-word form. On failure, the previous settings remain active.
bool setPersistentWifiSettings(const String &password,
                               bool hidden,
                               String &error);

// Optional station credentials let the badge join an existing LAN while its
// own access point remains available. The home-network password is never sent
// back through the web API after it has been saved.
bool hasPersistentStationWifiSettings();
constexpr uint8_t MAX_SAVED_STATION_NETWORKS = 4;
uint8_t getPersistentStationWifiCount();
bool getPersistentStationWifi(uint8_t index, String &ssid, String &password);
const char *getPersistentStationWifiSsid();
const char *getPersistentStationWifiPassword();
bool setPersistentStationWifiSettings(const String &ssid,
                                      const String &password,
                                      String &error);

// The same validation setPersistentStationWifiSettings() applies, without
// storing anything. A caller that means to try a network before remembering it
// needs to reject a malformed SSID or passphrase up front, while it can still
// say so to whoever typed it.
String getStationWifiCredentialError(const String &ssid, const String &password);

// Persistent LED state. Only the record's integrity is checked here; the
// meaning of each field, and the valid range of each, stays in main.cpp with
// the rest of the LED logic.
struct StoredLedSettings {
  uint8_t pattern;
  uint8_t red;
  uint8_t green;
  uint8_t blue;
  uint8_t brightness;
  uint8_t speed;
};

// Returns false when nothing has been saved yet, or when the stored record
// fails its magic, version, or checksum check. The caller keeps its defaults.
bool loadLedSettings(StoredLedSettings &settings);

// Unchanged settings are not rewritten, so holding a slider still costs at
// most one NVS write once the value settles.
bool saveLedSettings(const StoredLedSettings &settings);

// What the NFC radio should be doing. A badge comes back from a reboot the way
// it was left, the same as the LEDs do.
enum : uint8_t {
  NFC_MODE_STOPPED = 0,
  NFC_MODE_WIFI = 1,
  NFC_MODE_TEXT = 2,
  NFC_MODE_URL = 3,
};

// payload is meaningful only for NFC_MODE_TEXT and NFC_MODE_URL. Its size is
// the emulation payload ceiling in nfc.cpp; a longer record cannot be
// emulated, so it can never need storing.
struct StoredNfcSettings {
  uint8_t mode;
  bool offeringEnabled;
  char payload[221];
};

// Returns false when nothing has been saved yet, which is how a freshly
// flashed badge is recognized: it boots sharing its Wi-Fi over NFC, because
// nobody has told it to do anything else yet.
bool loadNfcSettings(StoredNfcSettings &settings);

bool saveNfcSettings(const StoredNfcSettings &settings);

// USB BOOT-button mappings are separate from LED state: an owner can use the
// same physical button for host controls while keeping the chosen animation.
// Values are UsbControlAction enum ordinals, kept as bytes here to avoid a
// dependency from persistent settings onto the USB subsystem.
struct StoredUsbButtonSettings {
  uint8_t shortAction;
  uint8_t longAction;
};

bool loadUsbButtonSettings(StoredUsbButtonSettings &settings);
bool saveUsbButtonSettings(const StoredUsbButtonSettings &settings);

// The S3 has room for either NCM or mass storage alongside CDC + HID, not
// both. USB serial remains present in every profile.
enum class UsbDeviceProfile : uint8_t { NETWORK = 0, DRIVE = 1 };
UsbDeviceProfile getPersistentUsbDeviceProfile();
bool setPersistentUsbDeviceProfile(UsbDeviceProfile profile, String &error);
