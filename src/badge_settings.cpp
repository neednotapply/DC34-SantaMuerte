#include <Arduino.h>
#include <Preferences.h>
#include <esp_system.h>
#include <cstddef>
#include <cstring>

#include "badge_settings.h"

namespace {

constexpr char SETTINGS_NAMESPACE[] = "badgecfg";
constexpr char KEY_WIFI_CREDENTIAL[] = "wifi_cred";
constexpr char KEY_WIFI_HIDDEN[] = "wifi_hidden";
constexpr char KEY_AP_ENABLED[] = "ap_enabled";
constexpr char KEY_UI_LANGUAGE[] = "ui_lang";
constexpr char KEY_STATION_WIFI[] = "sta_wifi";
constexpr char KEY_LED_SETTINGS[] = "led_state";
constexpr char KEY_NFC_SETTINGS[] = "nfc_state";
constexpr char KEY_BOARD_NEXT_ID[] = "board_id";

// WPA2 accepts an 8 to 63 character printable-ASCII passphrase. Both limits
// are enforced on generated and user-supplied passwords alike.
constexpr size_t WIFI_PASSWORD_MIN_LENGTH = 8;
constexpr size_t WIFI_PASSWORD_MAX_LENGTH = 63;
constexpr uint32_t WIFI_CREDENTIAL_MAGIC = 0x42444745UL;  // "BDGE"
constexpr uint8_t WIFI_CREDENTIAL_VERSION = 3;
constexpr uint32_t STATION_WIFI_MAGIC = 0x53544121UL;  // "STA!"
constexpr uint8_t STATION_WIFI_VERSION = 1;
constexpr size_t WIFI_SSID_MAX_LENGTH = 32;

// Version 1 stored a fixed 12-character generated password; version 2 stored a
// passphrase built from a mixed English and Spanish word list. Both are
// recognized only so a badge flashed with older firmware can be upgraded once,
// rather than refusing to bring Wi-Fi up at all.
constexpr uint8_t WIFI_CREDENTIAL_VERSION_V1 = 1;
constexpr uint8_t WIFI_CREDENTIAL_VERSION_V2 = 2;
constexpr size_t LEGACY_WIFI_PASSWORD_LENGTH = 12;

// The generated password is three distinct words joined in CamelCase, drawn
// from the badge's own iconography. Every word is Spanish and every word is
// plain ASCII: accented forms such as Guadana, Panteon and Espiritu are
// deliberately unaccented, because a WPA2 passphrase may not carry characters
// outside printable ASCII.
constexpr char const *PASSPHRASE_WORDS[] = {
    "Alma",     "Altar",     "Amuleto",   "Angel",     "Ataud",
    "Bruja",    "Calaca",    "Calavera",  "Campana",   "Camposanto",
    "Cempasuchil", "Ceniza", "Cirio",     "Copal",     "Corona",
    "Cripta",   "Cruz",      "Culto",     "Devoto",    "Difunto",
    "Encanto",  "Espejo",    "Espiritu",  "Estrella",  "Fantasma",
    "Flor",     "Guadana",   "Hechizo",   "Hueso",     "Humo",
    "Incienso", "Lapida",    "Luna",      "Luto",      "Manto",
    "Milagro",  "Moneda",    "Muerte",    "Nicho",     "Noche",
    "Novena",   "Nube",      "Ofrenda",   "Oracion",   "Osario",
    "Pacto",    "Panteon",   "Peregrino", "Petalo",    "Plegaria",
    "Promesa",  "Reliquia",  "Rezo",      "Rosario",   "Ruego",
    "Sagrada",  "Santa",     "Sombra",    "Sudario",   "Suerte",
    "Tumba",    "Vela",      "Veladora",  "Velorio",   "Vigilia",
    "Voto"};
constexpr size_t PASSPHRASE_WORD_COUNT =
    sizeof(PASSPHRASE_WORDS) / sizeof(PASSPHRASE_WORDS[0]);
constexpr size_t PASSPHRASE_WORDS_PER_PASSWORD = 3;

struct __attribute__((packed)) StoredWifiCredential {
  uint32_t magic;
  uint8_t version;
  char password[WIFI_PASSWORD_MAX_LENGTH + 1];
  uint32_t checksum;
};

static_assert(sizeof(StoredWifiCredential) == 73,
              "Unexpected StoredWifiCredential packing");

struct __attribute__((packed)) StoredStationWifi {
  uint32_t magic;
  uint8_t version;
  char ssid[WIFI_SSID_MAX_LENGTH + 1];
  char password[WIFI_PASSWORD_MAX_LENGTH + 1];
  uint32_t checksum;
};

static_assert(sizeof(StoredStationWifi) == 106,
              "Unexpected StoredStationWifi packing");

struct __attribute__((packed)) LegacyWifiCredential {
  uint32_t magic;
  uint8_t version;
  char password[LEGACY_WIFI_PASSWORD_LENGTH + 1];
  uint32_t checksum;
};

static_assert(sizeof(LegacyWifiCredential) == 22,
              "Unexpected LegacyWifiCredential packing");

constexpr uint32_t LED_SETTINGS_MAGIC = 0x4C454421UL;  // "LED!"
constexpr uint8_t LED_SETTINGS_VERSION = 1;

// The pattern is stored as its enum index, so reordering LedPattern in
// main.cpp changes the meaning of an already-saved record. Bump the version
// above if that enum is ever reordered rather than appended to.
struct __attribute__((packed)) StoredLedRecord {
  uint32_t magic;
  uint8_t version;
  uint8_t pattern;
  uint8_t red;
  uint8_t green;
  uint8_t blue;
  uint8_t brightness;
  uint8_t speed;
  uint32_t checksum;
};

static_assert(sizeof(StoredLedRecord) == 15,
              "Unexpected StoredLedRecord packing");

constexpr uint32_t NFC_SETTINGS_MAGIC = 0x4E464321UL;  // "NFC!"
constexpr uint8_t NFC_SETTINGS_VERSION = 1;

struct __attribute__((packed)) StoredNfcRecord {
  uint32_t magic;
  uint8_t version;
  uint8_t mode;
  uint8_t offeringEnabled;
  char payload[221];
  uint32_t checksum;
};

static_assert(sizeof(StoredNfcRecord) == 232,
              "Unexpected StoredNfcRecord packing");

char cachedWifiPassword[WIFI_PASSWORD_MAX_LENGTH + 1] = {};
bool settingsInitialized = false;
bool cachedWifiHidden = false;
bool cachedAccessPointEnabled = true;
bool cachedEnglishLanguage = false;
uint32_t cachedBoardIdWatermark = 1;
char cachedStationWifiSsid[WIFI_SSID_MAX_LENGTH + 1] = {};
char cachedStationWifiPassword[WIFI_PASSWORD_MAX_LENGTH + 1] = {};
bool cachedStationWifiAvailable = false;

uint32_t updateFnv1a(uint32_t hash, const uint8_t *data, size_t length) {
  for (size_t i = 0; i < length; ++i) {
    hash ^= data[i];
    hash *= 16777619UL;
  }
  return hash;
}

uint32_t credentialChecksum(const StoredWifiCredential &credential) {
  uint32_t hash = 2166136261UL;
  hash = updateFnv1a(
      hash,
      reinterpret_cast<const uint8_t *>(&credential.magic),
      sizeof(credential.magic));
  hash = updateFnv1a(hash, &credential.version, sizeof(credential.version));
  hash = updateFnv1a(
      hash,
      reinterpret_cast<const uint8_t *>(credential.password),
      sizeof(credential.password));
  return hash;
}

uint32_t stationWifiChecksum(const StoredStationWifi &settings) {
  uint32_t hash = 2166136261UL;
  hash = updateFnv1a(hash, reinterpret_cast<const uint8_t *>(&settings.magic),
                     sizeof(settings.magic));
  hash = updateFnv1a(hash, &settings.version, sizeof(settings.version));
  hash = updateFnv1a(hash,
                     reinterpret_cast<const uint8_t *>(settings.ssid),
                     sizeof(settings.ssid));
  return updateFnv1a(hash,
                     reinterpret_cast<const uint8_t *>(settings.password),
                     sizeof(settings.password));
}

bool isPrintableAscii(char character) {
  return character >= 0x20 && character <= 0x7E;
}

// Accepts any WPA2-legal passphrase, not only the generated three-word form,
// so a password chosen from the dashboard is stored and reloaded unchanged.
bool isValidPassword(const char *password) {
  if (!password) return false;

  const size_t length = strnlen(password, WIFI_PASSWORD_MAX_LENGTH + 1);
  if (length < WIFI_PASSWORD_MIN_LENGTH ||
      length > WIFI_PASSWORD_MAX_LENGTH) {
    return false;
  }

  for (size_t i = 0; i < length; ++i) {
    if (!isPrintableAscii(password[i])) return false;
  }
  return true;
}

String wifiPasswordValidationError(const String &password);

// The whole buffer is rewritten, not just the characters in use. Trailing
// bytes are part of the checksummed region, so leaving stale content behind
// would make an otherwise valid credential fail to verify on the next boot.
void copyPassword(char destination[WIFI_PASSWORD_MAX_LENGTH + 1],
                  const char *source) {
  memset(destination, 0, WIFI_PASSWORD_MAX_LENGTH + 1);
  const size_t length = strnlen(source, WIFI_PASSWORD_MAX_LENGTH);
  memcpy(destination, source, length);
}

void copySsid(char destination[WIFI_SSID_MAX_LENGTH + 1], const char *source) {
  memset(destination, 0, WIFI_SSID_MAX_LENGTH + 1);
  const size_t length = strnlen(source, WIFI_SSID_MAX_LENGTH);
  memcpy(destination, source, length);
}

bool isValidStationSsid(const char *ssid) {
  if (!ssid) return false;
  const size_t length = strnlen(ssid, WIFI_SSID_MAX_LENGTH + 1);
  if (length == 0 || length > WIFI_SSID_MAX_LENGTH) return false;
  for (size_t i = 0; i < length; ++i) {
    const uint8_t character = static_cast<uint8_t>(ssid[i]);
    if (character < 0x20 || character == 0x7F) return false;
  }
  return true;
}

bool validateStationWifi(const StoredStationWifi &settings) {
  return settings.magic == STATION_WIFI_MAGIC &&
         settings.version == STATION_WIFI_VERSION &&
         isValidStationSsid(settings.ssid) &&
         isValidPassword(settings.password) &&
         settings.checksum == stationWifiChecksum(settings);
}

bool readStationWifi(Preferences &preferences, StoredStationWifi &settings) {
  if (preferences.getBytesLength(KEY_STATION_WIFI) != sizeof(settings)) {
    return false;
  }
  return preferences.getBytes(KEY_STATION_WIFI, &settings, sizeof(settings)) ==
             sizeof(settings) &&
         validateStationWifi(settings);
}

bool storeAndVerifyStationWifi(Preferences &preferences,
                               const char *ssid,
                               const char *password) {
  StoredStationWifi settings = {};
  settings.magic = STATION_WIFI_MAGIC;
  settings.version = STATION_WIFI_VERSION;
  copySsid(settings.ssid, ssid);
  copyPassword(settings.password, password);
  settings.checksum = stationWifiChecksum(settings);
  if (preferences.putBytes(KEY_STATION_WIFI, &settings, sizeof(settings)) !=
      sizeof(settings)) {
    return false;
  }
  StoredStationWifi readBack = {};
  return readStationWifi(preferences, readBack) &&
         strncmp(readBack.ssid, ssid, WIFI_SSID_MAX_LENGTH + 1) == 0 &&
         strncmp(readBack.password, password, WIFI_PASSWORD_MAX_LENGTH + 1) == 0;
}

String stationWifiValidationError(const String &ssid, const String &password) {
  if (!isValidStationSsid(ssid.c_str())) {
    return F("El SSID de casa debe tener 1 a 32 bytes sin controles.");
  }
  String passwordError = wifiPasswordValidationError(password);
  if (passwordError.length() > 0) return passwordError;
  return String();
}

// Three distinct words in CamelCase. The shortest words in the list are four
// characters, so even the shortest result clears the eight-character WPA2
// minimum, and three of the longest still leave room below the maximum.
void generateRandomWifiPassword(
    char password[WIFI_PASSWORD_MAX_LENGTH + 1]) {
  size_t chosen[PASSPHRASE_WORDS_PER_PASSWORD] = {};
  size_t chosenCount = 0;

  while (chosenCount < PASSPHRASE_WORDS_PER_PASSWORD) {
    const size_t candidate = esp_random() % PASSPHRASE_WORD_COUNT;

    bool duplicate = false;
    for (size_t i = 0; i < chosenCount; ++i) {
      if (chosen[i] == candidate) duplicate = true;
    }
    if (duplicate) continue;

    chosen[chosenCount++] = candidate;
  }

  memset(password, 0, WIFI_PASSWORD_MAX_LENGTH + 1);
  size_t offset = 0;
  for (size_t i = 0; i < PASSPHRASE_WORDS_PER_PASSWORD; ++i) {
    const char *word = PASSPHRASE_WORDS[chosen[i]];
    const size_t wordLength = strlen(word);
    if (offset + wordLength > WIFI_PASSWORD_MAX_LENGTH) break;
    memcpy(password + offset, word, wordLength);
    offset += wordLength;
  }
}

StoredWifiCredential makeCredential(const char *password) {
  StoredWifiCredential credential = {};
  credential.magic = WIFI_CREDENTIAL_MAGIC;
  credential.version = WIFI_CREDENTIAL_VERSION;
  copyPassword(credential.password, password);
  credential.checksum = credentialChecksum(credential);
  return credential;
}

bool validateCredential(const StoredWifiCredential &credential) {
  return credential.magic == WIFI_CREDENTIAL_MAGIC &&
         credential.version == WIFI_CREDENTIAL_VERSION &&
         isValidPassword(credential.password) &&
         credential.checksum == credentialChecksum(credential);
}

bool readCredential(Preferences &preferences,
                    StoredWifiCredential &credential) {
  if (preferences.getBytesLength(KEY_WIFI_CREDENTIAL) !=
      sizeof(StoredWifiCredential)) {
    return false;
  }

  if (preferences.getBytes(
          KEY_WIFI_CREDENTIAL,
          &credential,
          sizeof(credential)) != sizeof(credential)) {
    return false;
  }

  return validateCredential(credential);
}

bool storeAndVerifyCredential(Preferences &preferences,
                              const char *password) {
  const StoredWifiCredential credential = makeCredential(password);

  if (preferences.putBytes(
          KEY_WIFI_CREDENTIAL,
          &credential,
          sizeof(credential)) != sizeof(credential)) {
    return false;
  }

  StoredWifiCredential readBack = {};
  return readCredential(preferences, readBack) &&
         strncmp(readBack.password,
                 password,
                 WIFI_PASSWORD_MAX_LENGTH + 1) == 0;
}

String wifiPasswordValidationError(const String &password) {
  if (password.length() < WIFI_PASSWORD_MIN_LENGTH ||
      password.length() > WIFI_PASSWORD_MAX_LENGTH) {
    return F("La contraseña debe tener 8 a 63 caracteres.");
  }

  for (size_t i = 0; i < password.length(); ++i) {
    if (!isPrintableAscii(password[i])) {
      return F(
          "Usa ASCII visible. Letras con acento y emoji no caben en la "
          "clave WPA2.");
    }
  }

  return String();
}

bool readLegacyCredential(Preferences &preferences) {
  if (preferences.getBytesLength(KEY_WIFI_CREDENTIAL) !=
      sizeof(LegacyWifiCredential)) {
    return false;
  }

  LegacyWifiCredential legacy = {};
  if (preferences.getBytes(KEY_WIFI_CREDENTIAL, &legacy, sizeof(legacy)) !=
      sizeof(legacy)) {
    return false;
  }

  if (legacy.magic != WIFI_CREDENTIAL_MAGIC ||
      legacy.version != WIFI_CREDENTIAL_VERSION_V1 ||
      legacy.password[LEGACY_WIFI_PASSWORD_LENGTH] != '\0') {
    return false;
  }

  uint32_t hash = 2166136261UL;
  hash = updateFnv1a(hash, reinterpret_cast<const uint8_t *>(&legacy.magic),
                     sizeof(legacy.magic));
  hash = updateFnv1a(hash, &legacy.version, sizeof(legacy.version));
  hash = updateFnv1a(hash,
                     reinterpret_cast<const uint8_t *>(legacy.password),
                     sizeof(legacy.password));
  return hash == legacy.checksum;
}

// True when the stored blob is a well-formed credential from an older
// firmware: a version 1 fixed-length password, or a version 2 passphrase from
// the mixed-language word list. Either is replaced once, on the next boot.
// Anything that is neither this nor a valid current record stays a hard error,
// so a corrupt blob can still never cause a silent password change.
bool readSupersededCredential(Preferences &preferences) {
  if (preferences.getBytesLength(KEY_WIFI_CREDENTIAL) ==
      sizeof(StoredWifiCredential)) {
    StoredWifiCredential stored = {};
    if (preferences.getBytes(KEY_WIFI_CREDENTIAL, &stored, sizeof(stored)) ==
            sizeof(stored) &&
        stored.magic == WIFI_CREDENTIAL_MAGIC &&
        stored.version == WIFI_CREDENTIAL_VERSION_V2 &&
        isValidPassword(stored.password) &&
        stored.checksum == credentialChecksum(stored)) {
      return true;
    }
  }

  return readLegacyCredential(preferences);
}

uint32_t ledRecordChecksum(const StoredLedRecord &record) {
  return updateFnv1a(2166136261UL,
                     reinterpret_cast<const uint8_t *>(&record),
                     offsetof(StoredLedRecord, checksum));
}

uint32_t nfcRecordChecksum(const StoredNfcRecord &record) {
  return updateFnv1a(2166136261UL,
                     reinterpret_cast<const uint8_t *>(&record),
                     offsetof(StoredNfcRecord, checksum));
}

}  // namespace

bool initializeBadgeSettings() {
  if (settingsInitialized) return true;

  Preferences preferences;
  if (!preferences.begin(SETTINGS_NAMESPACE, false)) {
    Serial.println(
        "[SETTINGS] ERROR: Could not open NVS; Wi-Fi will not start");
    return false;
  }

  StoredWifiCredential credential = {};

  const bool upgradingFromLegacy =
      preferences.isKey(KEY_WIFI_CREDENTIAL) &&
      !readCredential(preferences, credential) &&
      readSupersededCredential(preferences);

  if (upgradingFromLegacy) {
    Serial.println(
        "[SETTINGS] Replacing a superseded Wi-Fi password with a passphrase");
  }

  if (!preferences.isKey(KEY_WIFI_CREDENTIAL) || upgradingFromLegacy) {
    // This is the only path that creates a password.
    char generatedPassword[WIFI_PASSWORD_MAX_LENGTH + 1] = {};
    generateRandomWifiPassword(generatedPassword);

    if (!storeAndVerifyCredential(preferences, generatedPassword)) {
      preferences.end();
      Serial.println(
          "[SETTINGS] ERROR: First-boot Wi-Fi password could not be stored "
          "and verified; Wi-Fi will not start");
      return false;
    }

    copyPassword(cachedWifiPassword, generatedPassword);
    Serial.println(
        "[SETTINGS] Generated, stored, and verified the Wi-Fi passphrase");
  } else {
    // Once the key exists, never regenerate it. An invalid record is treated
    // as a hard error so the badge cannot silently change passwords.
    if (!readCredential(preferences, credential)) {
      preferences.end();
      Serial.println(
          "[SETTINGS] ERROR: Stored Wi-Fi credential is invalid; refusing "
          "to generate a replacement password");
      return false;
    }

    copyPassword(cachedWifiPassword, credential.password);
    Serial.println("[SETTINGS] Loaded and verified stored Wi-Fi password");
  }

  cachedWifiHidden = preferences.getBool(KEY_WIFI_HIDDEN, false);
  cachedAccessPointEnabled = preferences.getBool(KEY_AP_ENABLED, true);
  cachedEnglishLanguage = preferences.getBool(KEY_UI_LANGUAGE, false);
  cachedBoardIdWatermark = preferences.getUInt(KEY_BOARD_NEXT_ID, 1);

  // Home-network credentials are optional. A corrupt or superseded station
  // record must never keep the badge's own access point from starting.
  StoredStationWifi station = {};
  if (preferences.isKey(KEY_STATION_WIFI)) {
    if (readStationWifi(preferences, station)) {
      copySsid(cachedStationWifiSsid, station.ssid);
      copyPassword(cachedStationWifiPassword, station.password);
      cachedStationWifiAvailable = true;
      Serial.println("[SETTINGS] Loaded saved home Wi-Fi settings");
    } else {
      Serial.println("[SETTINGS] WARNING: Saved home Wi-Fi settings are invalid");
    }
  }
  preferences.end();

  settingsInitialized = true;
  return true;
}

const char *getPersistentWifiPassword() {
  if (!settingsInitialized && !initializeBadgeSettings()) return "";
  return cachedWifiPassword;
}

bool getPersistentWifiHidden() {
  if (!settingsInitialized && !initializeBadgeSettings()) return false;
  return cachedWifiHidden;
}

bool getPersistentAccessPointEnabled() {
  if (!settingsInitialized && !initializeBadgeSettings()) return false;
  return cachedAccessPointEnabled;
}

bool setPersistentAccessPointEnabled(bool enabled, String &error) {
  error = String();
  if (!settingsInitialized && !initializeBadgeSettings()) {
    error = F("Los ajustes guardados no están disponibles.");
    return false;
  }
  if (enabled == cachedAccessPointEnabled) return true;

  Preferences preferences;
  if (!preferences.begin(SETTINGS_NAMESPACE, false)) {
    error = F("No se pudo abrir NVS para guardar el ajuste del punto de acceso.");
    return false;
  }
  const bool stored = preferences.putBool(KEY_AP_ENABLED, enabled) != 0 &&
                      preferences.getBool(KEY_AP_ENABLED, !enabled) == enabled;
  preferences.end();
  if (!stored) {
    error = F("No se pudo guardar el ajuste del punto de acceso.");
    return false;
  }
  cachedAccessPointEnabled = enabled;
  Serial.printf("[SETTINGS] Badge AP %s\r\n", enabled ? "enabled" : "disabled");
  return true;
}

bool getPersistentEnglishLanguage() {
  if (!settingsInitialized && !initializeBadgeSettings()) return false;
  return cachedEnglishLanguage;
}

uint32_t getPersistentBoardIdWatermark() {
  if (!settingsInitialized && !initializeBadgeSettings()) return 1;
  return cachedBoardIdWatermark;
}

bool setPersistentBoardIdWatermark(uint32_t watermark) {
  if (!settingsInitialized && !initializeBadgeSettings()) return false;
  // Only ever forward. A stale or rolled-back value would let a number repeat.
  if (watermark <= cachedBoardIdWatermark) return true;

  Preferences preferences;
  if (!preferences.begin(SETTINGS_NAMESPACE, false)) return false;
  const bool stored = preferences.putUInt(KEY_BOARD_NEXT_ID, watermark) != 0;
  preferences.end();
  if (!stored) return false;
  cachedBoardIdWatermark = watermark;
  return true;
}

bool setPersistentEnglishLanguage(bool english, String &error) {
  error = String();
  if (!settingsInitialized && !initializeBadgeSettings()) {
    error = F("Los ajustes guardados no están disponibles.");
    return false;
  }
  if (english == cachedEnglishLanguage) return true;

  Preferences preferences;
  if (!preferences.begin(SETTINGS_NAMESPACE, false)) {
    error = F("No se pudo abrir NVS para guardar el idioma.");
    return false;
  }
  const bool stored = preferences.putBool(KEY_UI_LANGUAGE, english) != 0 &&
                      preferences.getBool(KEY_UI_LANGUAGE, !english) == english;
  preferences.end();
  if (!stored) {
    error = F("No se pudo guardar el idioma.");
    return false;
  }
  cachedEnglishLanguage = english;
  Serial.printf("[SETTINGS] UI language: %s\r\n", english ? "English" : "Spanish");
  return true;
}

bool setPersistentWifiSettings(const String &password,
                               bool hidden,
                               String &error) {
  error = String();
  if (!settingsInitialized && !initializeBadgeSettings()) {
    error = F("Los ajustes guardados no están disponibles.");
    return false;
  }

  error = wifiPasswordValidationError(password);
  if (error.length() > 0) return false;

  char requestedPassword[WIFI_PASSWORD_MAX_LENGTH + 1] = {};
  copyPassword(requestedPassword, password.c_str());

  const bool passwordChanged =
      strncmp(requestedPassword,
              cachedWifiPassword,
              WIFI_PASSWORD_MAX_LENGTH + 1) != 0;
  const bool hiddenChanged = hidden != cachedWifiHidden;
  if (!passwordChanged && !hiddenChanged) return true;

  Preferences preferences;
  if (!preferences.begin(SETTINGS_NAMESPACE, false)) {
    error = F("No se pudo abrir NVS para guardar el Wi-Fi.");
    return false;
  }

  char previousPassword[WIFI_PASSWORD_MAX_LENGTH + 1] = {};
  copyPassword(previousPassword, cachedWifiPassword);
  const bool previousHidden = cachedWifiHidden;

  bool passwordStored = true;
  bool hiddenStored = true;

  if (passwordChanged) {
    passwordStored =
        storeAndVerifyCredential(preferences, requestedPassword);
  }

  if (passwordStored && hiddenChanged) {
    hiddenStored =
        preferences.putBool(KEY_WIFI_HIDDEN, hidden) != 0 &&
        preferences.getBool(KEY_WIFI_HIDDEN, !hidden) == hidden;
  }

  if (!passwordStored || !hiddenStored) {
    // Best-effort rollback keeps NVS and the running configuration aligned.
    if (passwordChanged) {
      (void)storeAndVerifyCredential(preferences, previousPassword);
    }
    if (hiddenChanged) {
      (void)preferences.putBool(KEY_WIFI_HIDDEN, previousHidden);
    }
    preferences.end();

    error = !passwordStored
                ? F("La contraseña nueva no se pudo guardar.")
                : F("El ajuste de SSID oculto no se pudo guardar.");
    return false;
  }

  preferences.end();
  copyPassword(cachedWifiPassword, requestedPassword);
  cachedWifiHidden = hidden;

  Serial.printf("[SETTINGS] Wi-Fi settings updated: hidden=%s\r\n",
                cachedWifiHidden ? "true" : "false");
  return true;
}

bool hasPersistentStationWifiSettings() {
  if (!settingsInitialized && !initializeBadgeSettings()) return false;
  return cachedStationWifiAvailable;
}

const char *getPersistentStationWifiSsid() {
  if (!settingsInitialized && !initializeBadgeSettings()) return "";
  return cachedStationWifiAvailable ? cachedStationWifiSsid : "";
}

const char *getPersistentStationWifiPassword() {
  if (!settingsInitialized && !initializeBadgeSettings()) return "";
  return cachedStationWifiAvailable ? cachedStationWifiPassword : "";
}

bool setPersistentStationWifiSettings(const String &ssid,
                                      const String &password,
                                      String &error) {
  error = stationWifiValidationError(ssid, password);
  if (error.length() > 0) return false;
  if (!settingsInitialized && !initializeBadgeSettings()) {
    error = F("Los ajustes guardados no están disponibles.");
    return false;
  }

  char requestedSsid[WIFI_SSID_MAX_LENGTH + 1] = {};
  char requestedPassword[WIFI_PASSWORD_MAX_LENGTH + 1] = {};
  copySsid(requestedSsid, ssid.c_str());
  copyPassword(requestedPassword, password.c_str());
  if (cachedStationWifiAvailable &&
      strncmp(requestedSsid, cachedStationWifiSsid, sizeof(requestedSsid)) ==
          0 &&
      strncmp(requestedPassword, cachedStationWifiPassword,
              sizeof(requestedPassword)) == 0) {
    return true;
  }

  Preferences preferences;
  if (!preferences.begin(SETTINGS_NAMESPACE, false)) {
    error = F("No se pudo abrir NVS para guardar el Wi-Fi de casa.");
    return false;
  }
  const bool stored =
      storeAndVerifyStationWifi(preferences, requestedSsid, requestedPassword);
  preferences.end();
  if (!stored) {
    error = F("La red de casa no se pudo guardar.");
    return false;
  }

  copySsid(cachedStationWifiSsid, requestedSsid);
  copyPassword(cachedStationWifiPassword, requestedPassword);
  cachedStationWifiAvailable = true;
  Serial.printf("[SETTINGS] Home Wi-Fi updated: ssid=%s\r\n", requestedSsid);
  return true;
}

bool loadLedSettings(StoredLedSettings &settings) {
  Preferences preferences;
  if (!preferences.begin(SETTINGS_NAMESPACE, true)) {
    Serial.println(
        "[SETTINGS] WARNING: Could not open NVS to restore LED settings");
    return false;
  }

  // isKey() first: getBytesLength() on an absent key logs an NVS error, and a
  // badge that has simply never saved LED settings should not boot with an
  // error in its serial log.
  StoredLedRecord record = {};
  const bool sizeMatches =
      preferences.isKey(KEY_LED_SETTINGS) &&
      preferences.getBytesLength(KEY_LED_SETTINGS) == sizeof(record);
  const bool readComplete =
      sizeMatches && preferences.getBytes(KEY_LED_SETTINGS, &record,
                                          sizeof(record)) == sizeof(record);
  preferences.end();

  if (!readComplete || record.magic != LED_SETTINGS_MAGIC ||
      record.version != LED_SETTINGS_VERSION ||
      record.checksum != ledRecordChecksum(record)) {
    return false;
  }

  settings.pattern = record.pattern;
  settings.red = record.red;
  settings.green = record.green;
  settings.blue = record.blue;
  settings.brightness = record.brightness;
  settings.speed = record.speed;
  return true;
}

bool saveLedSettings(const StoredLedSettings &settings) {
  StoredLedRecord record = {};
  record.magic = LED_SETTINGS_MAGIC;
  record.version = LED_SETTINGS_VERSION;
  record.pattern = settings.pattern;
  record.red = settings.red;
  record.green = settings.green;
  record.blue = settings.blue;
  record.brightness = settings.brightness;
  record.speed = settings.speed;
  record.checksum = ledRecordChecksum(record);

  StoredLedSettings existing = {};
  if (loadLedSettings(existing) &&
      memcmp(&existing, &settings, sizeof(existing)) == 0) {
    return true;
  }

  Preferences preferences;
  if (!preferences.begin(SETTINGS_NAMESPACE, false)) {
    Serial.println(
        "[SETTINGS] ERROR: Could not open NVS to save LED settings");
    return false;
  }

  const bool stored =
      preferences.putBytes(KEY_LED_SETTINGS, &record, sizeof(record)) ==
      sizeof(record);
  preferences.end();

  if (!stored) {
    Serial.println("[SETTINGS] ERROR: LED settings were not committed");
  }
  return stored;
}

bool loadNfcSettings(StoredNfcSettings &settings) {
  Preferences preferences;
  if (!preferences.begin(SETTINGS_NAMESPACE, true)) {
    Serial.println(
        "[SETTINGS] WARNING: Could not open NVS to restore NFC settings");
    return false;
  }

  // isKey() first, for the same reason as the LED record: a badge that has
  // never saved NFC settings should not boot with an NVS error in its log.
  StoredNfcRecord record = {};
  const bool sizeMatches =
      preferences.isKey(KEY_NFC_SETTINGS) &&
      preferences.getBytesLength(KEY_NFC_SETTINGS) == sizeof(record);
  const bool readComplete =
      sizeMatches && preferences.getBytes(KEY_NFC_SETTINGS, &record,
                                          sizeof(record)) == sizeof(record);
  preferences.end();

  if (!readComplete || record.magic != NFC_SETTINGS_MAGIC ||
      record.version != NFC_SETTINGS_VERSION ||
      record.mode > NFC_MODE_URL ||
      record.payload[sizeof(record.payload) - 1] != '\0' ||
      record.checksum != nfcRecordChecksum(record)) {
    return false;
  }

  settings.mode = record.mode;
  settings.offeringEnabled = record.offeringEnabled != 0;
  memcpy(settings.payload, record.payload, sizeof(settings.payload));
  return true;
}

bool saveNfcSettings(const StoredNfcSettings &settings) {
  if (settings.mode > NFC_MODE_URL) return false;

  StoredNfcRecord record = {};
  record.magic = NFC_SETTINGS_MAGIC;
  record.version = NFC_SETTINGS_VERSION;
  record.mode = settings.mode;
  record.offeringEnabled = settings.offeringEnabled ? 1 : 0;
  strncpy(record.payload, settings.payload, sizeof(record.payload) - 1);
  record.checksum = nfcRecordChecksum(record);

  StoredNfcSettings existing = {};
  if (loadNfcSettings(existing) &&
      existing.mode == settings.mode &&
      existing.offeringEnabled == settings.offeringEnabled &&
      strncmp(existing.payload, record.payload, sizeof(record.payload)) == 0) {
    return true;
  }

  Preferences preferences;
  if (!preferences.begin(SETTINGS_NAMESPACE, false)) {
    Serial.println("[SETTINGS] ERROR: Could not open NVS to save NFC settings");
    return false;
  }

  const size_t written =
      preferences.putBytes(KEY_NFC_SETTINGS, &record, sizeof(record));
  preferences.end();

  if (written != sizeof(record)) {
    Serial.println("[SETTINGS] ERROR: NFC settings write was short");
    return false;
  }
  return true;
}
