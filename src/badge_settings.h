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

// Validates, stores, and reads back the dashboard Wi-Fi settings. The password
// must satisfy WPA2: 8 to 63 printable-ASCII characters. It is not required to
// follow the generated three-word form. On failure, the previous settings
// remain active.
bool setPersistentWifiSettings(const String &password,
                               bool hidden,
                               String &error);

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
