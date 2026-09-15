#include <Arduino.h>
#include <algorithm>
#include <SPI.h>
#include <Adafruit_I2CDevice.h>
#include <Adafruit_SPIDevice.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <Adafruit_PN532.h>

#include "nfc.h"
#include "badge_settings.h"
#include "usb_console.h"

// Single switch for every advanced NFC diagnostic: APDU byte dumps, Type 2
// pages, TLV/NDEF internals, polling detail, and write preparation detail.
#ifndef NFC_DEBUG_VERBOSE
#define NFC_DEBUG_VERBOSE true
#endif

namespace {

// -----------------------------------------------------------------------------
// PCB PN532 hardware
// -----------------------------------------------------------------------------
constexpr uint8_t SEL0 = 40;
constexpr uint8_t SEL1 = 39;
constexpr int8_t RSTPD_N = -1;  // Not connected to a valid ESP32-S3 GPIO
constexpr uint8_t PN532_SCK = 35;
constexpr uint8_t PN532_MISO = 36;
constexpr uint8_t PN532_MOSI = 37;
constexpr uint8_t PN532_SS = 38;

Adafruit_PN532 nfc(PN532_SCK, PN532_MISO, PN532_MOSI, PN532_SS);

constexpr uint32_t OPERATION_TIMEOUT_MS = 15000;
constexpr uint32_t POLL_INTERVAL_MS = 140;
// Adafruit's timeout covers both command acknowledgement and the final
// InListPassiveTarget response. Twenty-five milliseconds was too short for
// reliable tag activation on the PN532.
constexpr uint16_t TAG_POLL_TIMEOUT_MS = 500;
constexpr uint8_t PASSIVE_ACTIVATION_RETRIES = 0x10;
constexpr uint32_t POLL_PROGRESS_INTERVAL_MS = 1000;
constexpr size_t MAX_TYPE2_USER_BYTES = 888;  // NTAG216 user memory
constexpr size_t MAX_WEB_PAYLOAD_BYTES = 700;
constexpr size_t MAX_TAG_EMULATION_PAYLOAD_BYTES = 220;
constexpr size_t MAX_EMULATED_NDEF_BYTES = 240;
constexpr uint8_t MAX_TARGET_READ_BYTES = 59;

// Capture mode. A tag left sitting on the antenna re-activates on every poll,
// so the same UID is ignored until it has been away for this long. Polling is
// slower than an operator-initiated read: nothing is waiting on it, and the
// PN532 runs cooler for it.
constexpr uint32_t CAPTURE_POLL_INTERVAL_MS = 400;
constexpr uint32_t CAPTURE_REPEAT_MS = 8000;
// Two empty polls, about a second, before a tag counts as taken away.
constexpr uint8_t CAPTURE_CLEAR_MISSES = 2;
constexpr size_t MAX_CAPTURE_BYTES = 280;  // BOARD_MAX_TEXT_LENGTH
constexpr uint8_t CAPTURE_QUEUE_DEPTH = 4;

// Wi-Fi Simple Configuration (WSC) attribute identifiers. Values are encoded
// big-endian inside an application/vnd.wfa.wsc MIME NDEF record.
constexpr uint16_t WSC_CREDENTIAL = 0x100E;
constexpr uint16_t WSC_NETWORK_INDEX = 0x1026;
constexpr uint16_t WSC_SSID = 0x1045;
constexpr uint16_t WSC_AUTH_TYPE = 0x1003;
constexpr uint16_t WSC_ENCRYPTION_TYPE = 0x100F;
constexpr uint16_t WSC_NETWORK_KEY = 0x1027;
constexpr uint16_t WSC_MAC_ADDRESS = 0x1020;
constexpr uint16_t WSC_AUTH_OPEN = 0x0001;
constexpr uint16_t WSC_AUTH_WPA2_PSK = 0x0020;
constexpr uint16_t WSC_ENCRYPTION_NONE = 0x0001;
constexpr uint16_t WSC_ENCRYPTION_AES = 0x0008;
constexpr char WSC_MIME_TYPE[] = "application/vnd.wfa.wsc";
constexpr uint32_t TARGET_RETRY_INTERVAL_MS = 160;
constexpr uint8_t TYPE2_PAGE_BYTES = 4;
constexpr uint8_t TYPE2_READ_RETRIES = 3;
constexpr uint8_t TYPE2_WRITE_RETRIES = 3;
constexpr uint16_t TYPE2_RETRY_DELAY_MS = 4;
constexpr uint16_t TYPE2_WRITE_SETTLE_MS = 12;
constexpr size_t NFC_DEBUG_MAX_BYTES = 192;

constexpr uint8_t NFC_TASK_CORE = 0;
constexpr UBaseType_t NFC_TASK_PRIORITY = 1;
constexpr uint32_t NFC_TASK_STACK_SIZE = 8192;
constexpr UBaseType_t NFC_COMMAND_QUEUE_DEPTH = 4;
constexpr size_t NFC_WIFI_SSID_BUFFER_SIZE = 33;
constexpr size_t NFC_WIFI_PASSWORD_BUFFER_SIZE = 65;

uint8_t type2Buffer[MAX_TYPE2_USER_BYTES + 8];
uint8_t emulatedNdefFile[MAX_EMULATED_NDEF_BYTES + 2];

// -----------------------------------------------------------------------------
// Runtime state
// -----------------------------------------------------------------------------
enum class NfcOperation : uint8_t {
  NONE,
  READ,
  WRITE_TEXT,
  WRITE_URL
};

const char *operationName(NfcOperation operation) {
  switch (operation) {
    case NfcOperation::READ: return "READ";
    case NfcOperation::WRITE_TEXT: return "WRITE_TEXT";
    case NfcOperation::WRITE_URL: return "WRITE_URL";
    default: return "NONE";
  }
}

void logNfcBytes(const char *label, const uint8_t *data, size_t length,
                 size_t maxBytes = NFC_DEBUG_MAX_BYTES) {
#if NFC_DEBUG_VERBOSE
  const size_t shown = std::min(length, maxBytes);
  Serial.printf("[NFC][DATA] %s (%u byte%s): ", label,
                static_cast<unsigned>(length), length == 1 ? "" : "s");
  for (size_t i = 0; i < shown; ++i) {
    if (i) Serial.print(' ');
    if (data[i] < 0x10) Serial.print('0');
    Serial.print(data[i], HEX);
  }
  if (shown < length) Serial.print(" ...");
  Serial.println();
#else
  (void)label;
  (void)data;
  (void)length;
  (void)maxBytes;
#endif
}

void logType2Page(const char *action, uint16_t page,
                  const uint8_t data[TYPE2_PAGE_BYTES]) {
#if NFC_DEBUG_VERBOSE
  Serial.printf("[NFC][PAGE] %s page %u: %02X %02X %02X %02X | ",
                action, static_cast<unsigned>(page), data[0], data[1], data[2],
                data[3]);
  for (size_t i = 0; i < TYPE2_PAGE_BYTES; ++i) {
    const uint8_t c = data[i];
    Serial.print(c >= 32 && c <= 126 ? static_cast<char>(c) : '.');
  }
  Serial.println();
#else
  (void)action;
  (void)page;
  (void)data;
#endif
}

struct NfcState {
  bool readerReady = false;
  bool busy = false;
  bool writable = false;
  String status = "offline";
  String message = "El PN532 no ha iniciado.";
  String uid;
  String tagType;
  String recordType;
  String payload;
  String raw;
  uint16_t capacity = 0;
  uint32_t updatedAt = 0;

  bool tagEmulationEnabled = false;
  bool emulationReaderConnected = false;
  String emulatedRecordType;
  String emulatedPayload;
  String emulationMessage = "La emulación está parada.";
  uint32_t tagScans = 0;
  uint32_t lastTagScanAt = 0;

  bool captureEnabled = false;
  uint32_t captureCount = 0;
  String captureMessage = "Escaneo automático apagado.";

  // Internal published-state field used by the Wi-Fi settings workflow.
  bool wifiOnboardingActive = false;
};

enum class NfcCommandType : uint8_t {
  READ,
  WRITE_TEXT,
  WRITE_URL,
  START_TEXT_EMULATION,
  START_URL_EMULATION,
  START_WIFI_ONBOARDING,
  STOP_EMULATION,
  SET_CAPTURE
};

struct NfcCommand {
  NfcCommandType type = NfcCommandType::READ;
  bool flag = false;
  char payload[MAX_WEB_PAYLOAD_BYTES + 1] = {};
  char ssid[NFC_WIFI_SSID_BUFFER_SIZE] = {};
  char password[NFC_WIFI_PASSWORD_BUFFER_SIZE] = {};
  uint8_t apMac[6] = {};
  bool hasApMac = false;
};

NfcState state;
NfcState publishedState;
QueueHandle_t nfcCommandQueue = nullptr;
QueueHandle_t nfcCaptureQueue = nullptr;
SemaphoreHandle_t nfcStateMutex = nullptr;
TaskHandle_t nfcTaskHandle = nullptr;
bool commandPending = false;
NfcOperation pendingOperation = NfcOperation::NONE;
String pendingPayload;
String type2IoError;
uint32_t operationDeadline = 0;
uint32_t lastPoll = 0;
uint32_t lastPollProgress = 0;
uint32_t pollAttempts = 0;
uint32_t lastCapturePoll = 0;
uint8_t captureMisses = 0;
String lastCaptureUid;
uint32_t lastCaptureAt = 0;

// Emulation is started and stopped from the page, so a badge being fiddled
// with would otherwise write NVS on every tap. Save only once the radio has
// held the same mode for a moment.
constexpr uint32_t NFC_SETTINGS_SAVE_DELAY_MS = 1500;
constexpr uint32_t NFC_SETTINGS_POLL_MS = 250;
uint32_t lastNfcPersistencePoll = 0;
bool nfcPersistenceArmed = false;
bool haveObservedNfcSettings = false;
StoredNfcSettings observedNfcSettings = {};
uint32_t observedNfcSettingsAt = 0;

bool sameNfcSettings(const StoredNfcSettings &a, const StoredNfcSettings &b) {
  return a.mode == b.mode && a.offeringEnabled == b.offeringEnabled &&
         strncmp(a.payload, b.payload, sizeof(a.payload)) == 0;
}

// Reads only the fields the stored record needs, straight out of the published
// state and into fixed buffers.
//
// The obvious version -- copy the whole NfcState and pick fields off it -- was
// a heap disaster: NfcState carries eleven Strings, so every call allocated and
// freed eleven blocks, and loop() called it about a thousand times a second.
// That churn fragments the heap until a board write, which needs one large
// contiguous block for a base64 drawing, can no longer be satisfied.
bool nfcSettingsSnapshotLocked(StoredNfcSettings &settings) {
  if (!publishedState.readerReady) return false;

  settings = {};
  settings.offeringEnabled = publishedState.captureEnabled;

  if (!publishedState.tagEmulationEnabled) {
    settings.mode = NFC_MODE_STOPPED;
  } else if (publishedState.wifiOnboardingActive) {
    settings.mode = NFC_MODE_WIFI;
  } else {
    settings.mode =
        publishedState.emulatedRecordType == "URL" ? NFC_MODE_URL : NFC_MODE_TEXT;
    strncpy(settings.payload, publishedState.emulatedPayload.c_str(),
            sizeof(settings.payload) - 1);
  }
  return true;
}

enum class EmulatedFile : uint8_t {
  NONE,
  CAPABILITY_CONTAINER,
  NDEF
};

// This profile selects which NDEF content is served. All profiles use the
// same proven Adafruit PN532 AsTarget() activation methodology.
enum class TagEmulationProfile : uint8_t {
  WIFI_WSC_ANDROID,
  GENERIC_NDEF
};

TagEmulationProfile tagEmulationProfile =
    TagEmulationProfile::GENERIC_NDEF;

enum class NdefApplicationVersion : uint8_t { NONE, V1_0, V2_0 };
NdefApplicationVersion selectedNdefApplicationVersion =
    NdefApplicationVersion::NONE;
bool tagEmulationSessionActive = false;
bool tagScanCounted = false;
bool ndefApplicationSelected = false;
EmulatedFile selectedEmulatedFile = EmulatedFile::NONE;
uint16_t emulatedNdefLength = 0;
uint16_t highestEmulatedReadOffset = 0;
uint32_t lastTargetAttempt = 0;

constexpr uint8_t TYPE4_CAPABILITY_CONTAINER_V1[] = {
    0x00, 0x0F,        // CCLEN: 15 bytes
    0x10,              // NFC Forum Type 4 Tag mapping version 1.0
    0x00, 0x3B,        // MLe: reader may read up to 59 bytes per command
    0x00, 0x34,        // MLc: maximum command data length
    0x04, 0x06,        // NDEF File Control TLV
    0xE1, 0x04,        // NDEF file ID
    0x00, 0xF0,        // maximum NDEF file size: 240 bytes
    0x00,              // read access: always allowed
    0xFF               // write access: never allowed
};

constexpr uint8_t TYPE4_CAPABILITY_CONTAINER_V2[] = {
    0x00, 0x0F,        // CCLEN: 15 bytes
    0x20,              // NFC Forum Type 4 Tag mapping version 2.0
    0x00, 0x3B,        // MLe: reader may read up to 59 bytes per command
    0x00, 0x34,        // MLc: maximum command data length
    0x04, 0x06,        // NDEF File Control TLV
    0xE1, 0x04,        // NDEF file ID
    0x00, 0xF0,        // maximum NDEF file size: 240 bytes
    0x00,              // read access: always allowed
    0xFF               // write access: never allowed
};

// -----------------------------------------------------------------------------
// Formatting helpers
// -----------------------------------------------------------------------------
String jsonEscape(const String &value) {
  String escaped;
  escaped.reserve(value.length() + 16);
  for (size_t i = 0; i < value.length(); ++i) {
    const uint8_t c = static_cast<uint8_t>(value[i]);
    switch (c) {
      case '"': escaped += F("\\\""); break;
      case '\\': escaped += F("\\\\"); break;
      case '\b': escaped += F("\\b"); break;
      case '\f': escaped += F("\\f"); break;
      case '\n': escaped += F("\\n"); break;
      case '\r': escaped += F("\\r"); break;
      case '\t': escaped += F("\\t"); break;
      default:
        if (c < 0x20) {
          char encoded[7];
          snprintf(encoded, sizeof(encoded), "\\u%04X", c);
          escaped += encoded;
        } else {
          escaped += static_cast<char>(c);
        }
    }
  }
  return escaped;
}

String bytesToHex(const uint8_t *data, size_t length, size_t maxBytes = static_cast<size_t>(-1)) {
  const size_t shown = std::min(length, maxBytes);
  String output;
  output.reserve(shown * 3 + 4);
  char byteText[4];
  for (size_t i = 0; i < shown; ++i) {
    if (i) output += ' ';
    snprintf(byteText, sizeof(byteText), "%02X", data[i]);
    output += byteText;
  }
  if (shown < length) output += F(" ...");
  return output;
}

String uidToString(const uint8_t *uid, uint8_t uidLength) {
  String output;
  output.reserve(uidLength * 3);
  char byteText[4];
  for (uint8_t i = 0; i < uidLength; ++i) {
    if (i) output += ':';
    snprintf(byteText, sizeof(byteText), "%02X", uid[i]);
    output += byteText;
  }
  return output;
}

String bytesToString(const uint8_t *data, size_t length) {
  String output;
  output.reserve(length);
  for (size_t i = 0; i < length; ++i) output += static_cast<char>(data[i]);
  return output;
}

const char *uriPrefix(uint8_t code) {
  static const char *const prefixes[] = {
      "", "http://www.", "https://www.", "http://", "https://", "tel:",
      "mailto:", "ftp://anonymous:anonymous@", "ftp://ftp.", "ftps://",
      "sftp://", "smb://", "nfs://", "ftp://", "dav://", "news:",
      "telnet://", "imap:", "rtsp://", "urn:", "pop:", "sip:",
      "sips:", "tftp:", "btspp://", "btl2cap://", "btgoep://",
      "tcpobex://", "irdaobex://", "file://", "urn:epc:id:",
      "urn:epc:tag:", "urn:epc:pat:", "urn:epc:raw:", "urn:epc:",
      "urn:nfc:"};
  return code < (sizeof(prefixes) / sizeof(prefixes[0])) ? prefixes[code] : "";
}

uint8_t selectUriPrefix(const String &uri, String &remainder) {
  struct PrefixChoice {
    const char *text;
    uint8_t code;
  };
  static const PrefixChoice choices[] = {
      {"https://www.", 0x02}, {"http://www.", 0x01}, {"https://", 0x04},
      {"http://", 0x03},     {"mailto:", 0x06},    {"tel:", 0x05}};

  for (const auto &choice : choices) {
    if (uri.startsWith(choice.text)) {
      remainder = uri.substring(strlen(choice.text));
      return choice.code;
    }
  }
  remainder = uri;
  return 0x00;
}

void clearTagResult() {
  state.writable = false;
  state.uid = String();
  state.tagType = String();
  state.recordType = String();
  state.payload = String();
  state.raw = String();
  state.capacity = 0;
}

void finishSuccess(const String &message) {
  Serial.printf("[NFC][DONE] %s: %s\r\n", operationName(pendingOperation),
                message.c_str());
  state.busy = false;
  state.status = "success";
  state.message = message;
  state.updatedAt = millis();
  pendingOperation = NfcOperation::NONE;
  pendingPayload = String();
}

void finishError(const String &message) {
  Serial.printf("[NFC][ERROR] %s: %s\r\n", operationName(pendingOperation),
                message.c_str());
  state.busy = false;
  state.status = "error";
  state.message = message;
  state.updatedAt = millis();
  pendingOperation = NfcOperation::NONE;
  pendingPayload = String();
}

bool configurePassiveReader(const char *reason) {
  Serial.printf("[NFC][READER] Configuring passive reader mode: %s\r\n", reason);

  if (!nfc.SAMConfig()) {
    state.readerReady = false;
    state.status = "offline";
    state.message = "Falló la configuración SAM del PN532.";
    state.updatedAt = millis();
    Serial.println("[NFC][READER] ERROR: SAMConfig failed");
    return false;
  }

  if (!nfc.setPassiveActivationRetries(PASSIVE_ACTIVATION_RETRIES)) {
    state.readerReady = false;
    state.status = "offline";
    state.message = "Falló la configuración de reintentos del PN532.";
    state.updatedAt = millis();
    Serial.println("[NFC][READER] ERROR: setPassiveActivationRetries failed");
    return false;
  }

  delay(20);
  state.readerReady = true;
  Serial.printf("[NFC][READER] Ready; retries=0x%02X pollTimeout=%u ms\r\n",
                PASSIVE_ACTIVATION_RETRIES, TAG_POLL_TIMEOUT_MS);
  return true;
}

bool beginOperation(NfcOperation operation, const String &message) {
  if (!state.readerReady) {
    Serial.printf("[NFC][REJECT] %s: PN532 reader is not available\r\n",
                  operationName(operation));
    state.status = "error";
    state.message = "El lector PN532 no está disponible.";
    state.updatedAt = millis();
    return false;
  }
  if (state.tagEmulationEnabled) {
    Serial.printf("[NFC][REJECT] %s: tag emulation is active\r\n",
                  operationName(operation));
    state.status = "error";
    state.message = "Para la emulación antes de leer o escribir otro tag.";
    state.updatedAt = millis();
    return false;
  }
  if (state.busy) {
    Serial.printf("[NFC][REJECT] %s: %s is already active\r\n",
                  operationName(operation), operationName(pendingOperation));
    state.message = "Ya hay otra acción NFC esperando un tag.";
    state.updatedAt = millis();
    return false;
  }

  // Target/card-emulation mode and aborted commands can leave the PN532 in a
  // state where a new auto-scan does not start cleanly. Reassert normal SAM
  // reader mode before every web-requested external-tag operation.
  if (!configurePassiveReader("starting external-tag operation")) return false;

  pendingOperation = operation;
#if NFC_DEBUG_VERBOSE
  Serial.printf("[NFC][QUEUE] %s queued; timeout=%lu ms\r\n",
                operationName(operation),
                static_cast<unsigned long>(OPERATION_TIMEOUT_MS));
#endif
  state.busy = true;
  state.status = "waiting";
  state.message = message;
  state.updatedAt = millis();
  operationDeadline = millis() + OPERATION_TIMEOUT_MS;
  lastPoll = 0;
  lastPollProgress = 0;
  pollAttempts = 0;
#if NFC_DEBUG_VERBOSE
  Serial.println("[NFC][POLL] Reader armed. Present the tag and keep it stationary.");
#endif
  return true;
}

// -----------------------------------------------------------------------------
// NDEF parsing and Type 2 tag access
// -----------------------------------------------------------------------------
bool readType2Page(uint16_t page, uint8_t output[TYPE2_PAGE_BYTES]) {
  if (page > 230U) {
    type2IoError = "La página Type 2 queda fuera del rango NTAG2xx.";
    return false;
  }

  for (uint8_t attempt = 1; attempt <= TYPE2_READ_RETRIES; ++attempt) {
    memset(output, 0, TYPE2_PAGE_BYTES);
    if (nfc.ntag2xx_ReadPage(static_cast<uint8_t>(page), output)) {
      type2IoError = String();
      logType2Page("READ", page, output);
      return true;
    }
#if NFC_DEBUG_VERBOSE
    Serial.printf("[NFC][PAGE] Read page %u failed (attempt %u/%u)\r\n",
                  static_cast<unsigned>(page), attempt, TYPE2_READ_RETRIES);
#endif
    delay(TYPE2_RETRY_DELAY_MS);
    yield();
  }

  type2IoError = "No se pudo leer la página Type 2 " + String(page) +
                 " después de " + String(TYPE2_READ_RETRIES) + " intentos.";
  Serial.printf("[NFC] ERROR: %s\r\n", type2IoError.c_str());
  return false;
}

bool writeType2PageVerified(uint16_t page,
                            const uint8_t data[TYPE2_PAGE_BYTES]) {
  if (page < 4U || page > 225U) {
    type2IoError = "No se escribe fuera del rango de usuario NTAG2xx.";
    return false;
  }

  for (uint8_t attempt = 1; attempt <= TYPE2_WRITE_RETRIES; ++attempt) {
    uint8_t writableData[TYPE2_PAGE_BYTES];
    memcpy(writableData, data, TYPE2_PAGE_BYTES);
    logType2Page("WRITE request", page, writableData);

    if (nfc.ntag2xx_WritePage(static_cast<uint8_t>(page), writableData)) {
      delay(TYPE2_WRITE_SETTLE_MS);

      uint8_t verifyData[TYPE2_PAGE_BYTES] = {0, 0, 0, 0};
      if (readType2Page(page, verifyData) &&
          memcmp(verifyData, data, TYPE2_PAGE_BYTES) == 0) {
        logType2Page("WRITE verified", page, verifyData);
        type2IoError = String();
        return true;
      }
#if NFC_DEBUG_VERBOSE
      logType2Page("VERIFY expected", page, data);
      logType2Page("VERIFY actual", page, verifyData);
#endif
    } else {
#if NFC_DEBUG_VERBOSE
      Serial.printf("[NFC][PAGE] Write page %u command failed (attempt %u/%u)\r\n",
                    static_cast<unsigned>(page), attempt, TYPE2_WRITE_RETRIES);
#endif
    }

    delay(TYPE2_RETRY_DELAY_MS);
    yield();
  }

  type2IoError = "No se pudo escribir y verificar la página Type 2 " + String(page) + ".";
  Serial.printf("[NFC] ERROR: %s\r\n", type2IoError.c_str());
  return false;
}

bool loadType2Bytes(size_t requiredBytes, size_t capacity,
                    size_t &loadedBytes) {
  if (requiredBytes > capacity || requiredBytes > MAX_TYPE2_USER_BYTES) {
    type2IoError = "El largo TLV Type 2 pasa la capacidad del tag.";
    return false;
  }

  while (loadedBytes < requiredBytes) {
    const uint16_t page = 4U + static_cast<uint16_t>(loadedBytes / TYPE2_PAGE_BYTES);
    uint8_t pageData[TYPE2_PAGE_BYTES];
    if (!readType2Page(page, pageData)) return false;

    const size_t copyLength =
        std::min<size_t>(TYPE2_PAGE_BYTES, capacity - loadedBytes);
    memcpy(type2Buffer + loadedBytes, pageData, copyLength);
    loadedBytes += copyLength;
    yield();
  }
  return true;
}

bool parseNdefRecord(const uint8_t *message, size_t messageLength) {
  if (messageLength < 4) {
    state.recordType = "Desconocido";
    state.payload = "El mensaje NDEF es muy corto para leerlo.";
    return false;
  }

  size_t offset = 0;
  const uint8_t header = message[offset++];
  const bool shortRecord = (header & 0x10) != 0;
  const bool hasId = (header & 0x08) != 0;
  const uint8_t tnf = header & 0x07;
  const uint8_t typeLength = message[offset++];

  uint32_t payloadLength = 0;
  if (shortRecord) {
    if (offset >= messageLength) return false;
    payloadLength = message[offset++];
  } else {
    if (offset + 4 > messageLength) return false;
    payloadLength = (static_cast<uint32_t>(message[offset]) << 24) |
                    (static_cast<uint32_t>(message[offset + 1]) << 16) |
                    (static_cast<uint32_t>(message[offset + 2]) << 8) |
                    static_cast<uint32_t>(message[offset + 3]);
    offset += 4;
  }

  uint8_t idLength = 0;
  if (hasId) {
    if (offset >= messageLength) return false;
    idLength = message[offset++];
  }

  if (offset + typeLength + idLength + payloadLength > messageLength) return false;

  const uint8_t *type = message + offset;
  offset += typeLength + idLength;
  const uint8_t *payload = message + offset;

#if NFC_DEBUG_VERBOSE
  Serial.printf("[NFC][NDEF] header=%02X tnf=%u typeLen=%u payloadLen=%lu short=%s id=%s\r\n",
                header, tnf, typeLength, static_cast<unsigned long>(payloadLength),
                shortRecord ? "yes" : "no", hasId ? "yes" : "no");
  logNfcBytes("NDEF record", message, messageLength);
#endif

  if (tnf == 0x01 && typeLength == 1 && type[0] == 'T') {
    state.recordType = "Text";
    if (payloadLength < 1) {
      state.payload = String();
      return true;
    }
    const uint8_t status = payload[0];
    const bool utf16 = (status & 0x80) != 0;
    const uint8_t languageLength = status & 0x3F;
    if (1U + languageLength > payloadLength) return false;
    if (utf16) {
      state.payload = "Se detectó texto UTF-16; aquí solo se muestra UTF-8.";
      return true;
    }
    state.payload = bytesToString(payload + 1 + languageLength,
                                  payloadLength - 1 - languageLength);
#if NFC_DEBUG_VERBOSE
    Serial.printf("[NFC][NDEF] Decoded Text: %s\r\n", state.payload.c_str());
#endif
    return true;
  }

  if (tnf == 0x01 && typeLength == 1 && type[0] == 'U') {
    state.recordType = "URL";
    if (payloadLength < 1) {
      state.payload = String();
      return true;
    }
    state.payload = String(uriPrefix(payload[0]));
    state.payload += bytesToString(payload + 1, payloadLength - 1);
#if NFC_DEBUG_VERBOSE
    Serial.printf("[NFC][NDEF] Decoded URL: %s\r\n", state.payload.c_str());
#endif
    return true;
  }

  state.recordType = "NDEF";
  state.payload = "Tipo de registro NDEF no compatible. Abajo salen los bytes crudos.";
  return true;
}

bool readUnformattedType2Preview() {
  constexpr size_t PREVIEW_PAGES = 6;
  uint8_t preview[PREVIEW_PAGES * TYPE2_PAGE_BYTES] = {0};
  size_t bytesRead = 0;

  for (size_t index = 0; index < PREVIEW_PAGES; ++index) {
    uint8_t pageData[TYPE2_PAGE_BYTES];
    if (!readType2Page(4U + index, pageData)) break;
    memcpy(preview + bytesRead, pageData, TYPE2_PAGE_BYTES);
    bytesRead += TYPE2_PAGE_BYTES;
  }

  state.recordType = "Raw";
  state.payload =
      "El tag responde como memoria Type 2, pero no está en formato NDEF. "
      "Abajo salen sus primeros bytes.";
  state.raw = bytesToHex(preview, bytesRead);
  state.capacity = 0;
  state.writable = false;
  return bytesRead != 0;
}

bool readType2Tag(bool &type2MemoryResponded) {
  type2MemoryResponded = false;
  type2IoError = String();

  uint8_t capability[TYPE2_PAGE_BYTES];
  if (!readType2Page(3, capability)) return false;
  type2MemoryResponded = true;

  logType2Page("CAPABILITY", 3, capability);

  if (capability[0] != 0xE1) {
    // tagType is baked verbatim into the offering text a capture posts, so it
    // has to read the same in both languages -- exactly like the ISO14443A and
    // "NFC Forum Type 2" identifiers it sits alongside. A translated string
    // here would be frozen into board content in whatever language happened to
    // be active at capture time.
    state.tagType = "Type 2 compatible (no NDEF)";
    state.message = "Se vio memoria Type 2, pero sin contenedor válido.";
    return readUnformattedType2Preview();
  }

  state.tagType = "NFC Forum Type 2 (NTAG / Ultralight)";
  state.capacity = static_cast<uint16_t>(capability[2]) * 8U;
  state.capacity = std::min<uint16_t>(state.capacity,
                                     static_cast<uint16_t>(MAX_TYPE2_USER_BYTES));
  state.writable = (capability[3] & 0x0F) != 0x0F;

#if NFC_DEBUG_VERBOSE
  Serial.printf("[NFC] Type 2 CC: %02X %02X %02X %02X, data area %u bytes\r\n",
                capability[0], capability[1], capability[2], capability[3],
                static_cast<unsigned>(state.capacity));
#endif

  if (state.capacity == 0) {
    state.recordType = "None";
    state.payload = "El tag no tiene memoria de usuario.";
    return true;
  }

  // Parse the Type 2 TLV stream incrementally. This is intentionally not a
  // full-memory dump: a typical short NDEF record can be decoded after only a
  // few pages. The previous implementation read every advertised page first,
  // causing 124 separate RF reads on an NTAG215 even for a tiny record.
  // JoinOurDiscord-discord.gg/thesafehouse-Ed-Loves-Furries-Bring-Us-Backpack-Beers-Next-Year
  size_t loadedBytes = 0;
  size_t offset = 0;

  while (offset < state.capacity) {
    if (!loadType2Bytes(offset + 1U, state.capacity, loadedBytes)) return false;
    const uint8_t tlvType = type2Buffer[offset++];
#if NFC_DEBUG_VERBOSE
    Serial.printf("[NFC][TLV] offset=%u type=%02X\r\n",
                  static_cast<unsigned>(offset - 1U), tlvType);
#endif

    if (tlvType == 0x00) continue;  // NULL TLV
    if (tlvType == 0xFE) break;     // Terminator TLV

    if (!loadType2Bytes(offset + 1U, state.capacity, loadedBytes)) return false;
    size_t tlvLength = type2Buffer[offset++];
    if (tlvLength == 0xFF) {
      if (!loadType2Bytes(offset + 2U, state.capacity, loadedBytes)) return false;
      tlvLength = (static_cast<size_t>(type2Buffer[offset]) << 8) |
                  static_cast<size_t>(type2Buffer[offset + 1]);
      offset += 2;
    }

    if (tlvLength > state.capacity - offset) {
      type2IoError = "El tag trae un largo TLV mayor que su área de datos.";
      return false;
    }

#if NFC_DEBUG_VERBOSE
    Serial.printf("[NFC][TLV] type=%02X length=%u payloadOffset=%u\r\n", tlvType,
                  static_cast<unsigned>(tlvLength),
                  static_cast<unsigned>(offset));
#endif

    if (tlvType == 0x03) {
      if (tlvLength == 0) {
        state.recordType = "Empty";
        state.payload = "Este tag trae un mensaje NDEF vacío.";
        state.raw = String();
        return true;
      }

      if (!loadType2Bytes(offset + tlvLength, state.capacity, loadedBytes)) {
        return false;
      }

      state.raw = bytesToHex(type2Buffer + offset, tlvLength, 320);
      if (!parseNdefRecord(type2Buffer + offset, tlvLength)) {
        state.recordType = "NDEF";
        state.payload = "Hay datos NDEF, pero no se pudieron leer.";
      }

#if NFC_DEBUG_VERBOSE
      Serial.printf("[NFC] NDEF read completed after %u user bytes (%u pages)\r\n",
                    static_cast<unsigned>(loadedBytes),
                    static_cast<unsigned>((loadedBytes + 3U) / 4U));
#endif
      return true;
    }

    offset += tlvLength;
  }

  state.recordType = "None";
  state.payload = "No se encontró mensaje NDEF en el tag.";
  state.raw = String();
  return true;
}

bool buildNdef(const String &recordType, const String &input, size_t capacity,
               size_t &totalLength, String &error) {
  if (input.length() == 0) {
    error = "Escribe texto o una URL antes de guardar.";
    return false;
  }
  if (input.length() > MAX_WEB_PAYLOAD_BYTES) {
    error = "El contenido pasa el límite de 700 bytes.";
    return false;
  }

  String recordPayload = input;
  uint8_t prefixCode = 0;
  uint8_t typeByte = 0;
  size_t payloadLength = 0;

  if (recordType == "url") {
    typeByte = 'U';
    prefixCode = selectUriPrefix(input, recordPayload);
    payloadLength = 1U + recordPayload.length();
  } else {
    typeByte = 'T';
    payloadLength = 3U + input.length();  // status byte + "en" + text
  }

  const bool shortRecord = payloadLength <= 255U;
  const size_t recordHeaderLength = shortRecord ? 4U : 7U;
  const size_t ndefLength = recordHeaderLength + payloadLength;
  const size_t tlvHeaderLength = ndefLength < 255U ? 2U : 4U;
  totalLength = tlvHeaderLength + ndefLength + 1U;  // terminator TLV

  if (totalLength > capacity || totalLength > MAX_TYPE2_USER_BYTES) {
    error = "El registro NDEF pesa mucho para este tag.";
    return false;
  }

  memset(type2Buffer, 0, totalLength + 4U);
  size_t offset = 0;
  type2Buffer[offset++] = 0x03;  // NDEF Message TLV
  if (ndefLength < 255U) {
    type2Buffer[offset++] = static_cast<uint8_t>(ndefLength);
  } else {
    type2Buffer[offset++] = 0xFF;
    type2Buffer[offset++] = static_cast<uint8_t>((ndefLength >> 8) & 0xFF);
    type2Buffer[offset++] = static_cast<uint8_t>(ndefLength & 0xFF);
  }

  type2Buffer[offset++] = shortRecord ? 0xD1 : 0xC1;  // MB + ME + TNF well-known
  type2Buffer[offset++] = 0x01;                       // type length
  if (shortRecord) {
    type2Buffer[offset++] = static_cast<uint8_t>(payloadLength);
  } else {
    type2Buffer[offset++] = static_cast<uint8_t>((payloadLength >> 24) & 0xFF);
    type2Buffer[offset++] = static_cast<uint8_t>((payloadLength >> 16) & 0xFF);
    type2Buffer[offset++] = static_cast<uint8_t>((payloadLength >> 8) & 0xFF);
    type2Buffer[offset++] = static_cast<uint8_t>(payloadLength & 0xFF);
  }
  type2Buffer[offset++] = typeByte;

  if (recordType == "url") {
    type2Buffer[offset++] = prefixCode;
    memcpy(type2Buffer + offset, recordPayload.c_str(), recordPayload.length());
    offset += recordPayload.length();
  } else {
    type2Buffer[offset++] = 0x02;  // UTF-8, two-character language code
    type2Buffer[offset++] = 'e';
    type2Buffer[offset++] = 'n';
    memcpy(type2Buffer + offset, input.c_str(), input.length());
    offset += input.length();
  }

  type2Buffer[offset++] = 0xFE;
  totalLength = offset;
#if NFC_DEBUG_VERBOSE
  Serial.printf("[NFC][NDEF] Built %s record: input=%u bytes, encoded=%u bytes\r\n",
                recordType.c_str(), static_cast<unsigned>(input.length()),
                static_cast<unsigned>(totalLength));
#endif
  logNfcBytes("Encoded Type 2 TLV", type2Buffer, totalLength);
  return true;
}

bool writeType2Buffer(size_t totalLength) {
  type2IoError = String();

  // NFC Forum transaction-safe update: publish a zero-length NDEF TLV first,
  // write the remainder, then commit the real first page last.
  const uint8_t emptyTag[TYPE2_PAGE_BYTES] = {0x03, 0x00, 0xFE, 0x00};
  if (!writeType2PageVerified(4, emptyTag)) return false;

  const size_t pages = (totalLength + TYPE2_PAGE_BYTES - 1U) / TYPE2_PAGE_BYTES;
#if NFC_DEBUG_VERBOSE
  Serial.printf("[NFC][WRITE] Storing %u bytes across %u page(s); commit page 4 last\r\n",
                static_cast<unsigned>(totalLength),
                static_cast<unsigned>(pages));
#endif
  for (size_t pageIndex = 1; pageIndex < pages; ++pageIndex) {
    uint8_t pageData[TYPE2_PAGE_BYTES] = {0, 0, 0, 0};
    const size_t sourceOffset = pageIndex * TYPE2_PAGE_BYTES;
    const size_t remaining =
        totalLength > sourceOffset ? totalLength - sourceOffset : 0;
    memcpy(pageData, type2Buffer + sourceOffset,
           std::min<size_t>(TYPE2_PAGE_BYTES, remaining));

    if (!writeType2PageVerified(4U + pageIndex, pageData)) return false;
    yield();
  }

  uint8_t firstPage[TYPE2_PAGE_BYTES] = {0, 0, 0, 0};
  memcpy(firstPage, type2Buffer,
         std::min<size_t>(TYPE2_PAGE_BYTES, totalLength));
  if (!writeType2PageVerified(4, firstPage)) return false;

  Serial.printf("[NFC] NDEF write committed and verified across %u pages\r\n",
                static_cast<unsigned>(pages));
  return true;
}

bool prepareType2ForWrite(uint16_t &capacity, String &error) {
  type2IoError = String();
  uint8_t capability[TYPE2_PAGE_BYTES];
  if (!readType2Page(3, capability)) {
    error = type2IoError.length() ? type2IoError
                                  : "No se pudo leer la página de capacidad Type 2.";
    return false;
  }
  logType2Page("WRITE capability", 3, capability);
  if (capability[0] != 0xE1) {
    error =
        "Este tag no está en formato NFC Forum Type 2. No se formatea solo "
        "porque eso podría cambiar un chip incompatible.";
    return false;
  }
  if ((capability[3] & 0x0F) == 0x0F) {
    error = "Este tag dice que su NDEF es solo lectura.";
    return false;
  }
  capacity = std::min<uint16_t>(static_cast<uint16_t>(capability[2]) * 8U,
                                static_cast<uint16_t>(MAX_TYPE2_USER_BYTES));
  if (capacity == 0) {
    error = "Este tag no tiene memoria para escribir.";
    return false;
  }
  return true;
}


// -----------------------------------------------------------------------------
// NFC Forum Type 4 Tag emulation
// -----------------------------------------------------------------------------
bool appendWscAttribute(uint8_t *buffer, size_t capacity, size_t &offset,
                        uint16_t attributeId, const uint8_t *value,
                        size_t valueLength) {
  if (!buffer || valueLength > 0xFFFFU ||
      offset + 4U + valueLength > capacity) {
    return false;
  }

  buffer[offset++] = static_cast<uint8_t>((attributeId >> 8) & 0xFF);
  buffer[offset++] = static_cast<uint8_t>(attributeId & 0xFF);
  buffer[offset++] = static_cast<uint8_t>((valueLength >> 8) & 0xFF);
  buffer[offset++] = static_cast<uint8_t>(valueLength & 0xFF);
  if (valueLength && value) {
    memcpy(buffer + offset, value, valueLength);
    offset += valueLength;
  }
  return true;
}

bool buildWifiOnboardingNdef(const String &ssid, const String &password,
                             const uint8_t apMac[6], String &error) {
  if (ssid.length() == 0 || ssid.length() > 32U) {
    error = "El SSID está vacío o pasa de 32 bytes.";
    return false;
  }
  const bool openNetwork = password.length() == 0;
  if (!openNetwork && (password.length() < 8U || password.length() > 63U)) {
    error = "La contraseña debe tener 8 a 63 caracteres.";
    return false;
  }

  // Keep the Android Wi-Fi Simple Configuration credential payload unchanged.
  // This is the same WSC attribute order and content used by the working
  // single-record Android implementation.
  uint8_t credential[160] = {0};
  size_t credentialLength = 0;
  const uint8_t networkIndex = 1;
  const uint16_t auth = openNetwork ? WSC_AUTH_OPEN : WSC_AUTH_WPA2_PSK;
  const uint16_t encryption =
      openNetwork ? WSC_ENCRYPTION_NONE : WSC_ENCRYPTION_AES;
  const uint8_t authType[] = {static_cast<uint8_t>((auth >> 8) & 0xFF),
                              static_cast<uint8_t>(auth & 0xFF)};
  const uint8_t encryptionType[] = {
      static_cast<uint8_t>((encryption >> 8) & 0xFF),
      static_cast<uint8_t>(encryption & 0xFF)};
  const uint8_t zeroMac[6] = {0};
  const uint8_t *mac = apMac ? apMac : zeroMac;

  const bool attributesBuilt =
      appendWscAttribute(credential, sizeof(credential), credentialLength,
                         WSC_NETWORK_INDEX, &networkIndex, 1) &&
      appendWscAttribute(credential, sizeof(credential), credentialLength,
                         WSC_SSID,
                         reinterpret_cast<const uint8_t *>(ssid.c_str()),
                         ssid.length()) &&
      appendWscAttribute(credential, sizeof(credential), credentialLength,
                         WSC_AUTH_TYPE, authType, sizeof(authType)) &&
      appendWscAttribute(credential, sizeof(credential), credentialLength,
                         WSC_ENCRYPTION_TYPE, encryptionType,
                         sizeof(encryptionType)) &&
      (openNetwork || appendWscAttribute(
                          credential, sizeof(credential), credentialLength,
                          WSC_NETWORK_KEY,
                          reinterpret_cast<const uint8_t *>(password.c_str()),
                          password.length())) &&
      appendWscAttribute(credential, sizeof(credential), credentialLength,
                         WSC_MAC_ADDRESS, mac, 6);

  if (!attributesBuilt) {
    error = "Los datos de Wi-Fi no caben en el tag emulado.";
    return false;
  }

  uint8_t wscPayload[176] = {0};
  size_t wscPayloadLength = 0;
  if (!appendWscAttribute(wscPayload, sizeof(wscPayload), wscPayloadLength,
                          WSC_CREDENTIAL, credential, credentialLength)) {
    error = "No se pudo armar el registro WSC de Wi-Fi.";
    return false;
  }

  // The second NDEF record is a standard NFC Forum Well Known Text record.
  // Dedicated iPhone NFC apps can enumerate the message and decode this record.
  const String textPayload = openNetwork
                                 ? "Wi-Fi Network: " + ssid + "\nSecurity: Open"
                                 : "Wi-Fi Network: " + ssid +
                                       "\nPassword: " + password;
  const size_t textPayloadLength = 3U + textPayload.length();

  constexpr size_t mimeTypeLength = sizeof(WSC_MIME_TYPE) - 1U;
  const size_t wscRecordLength = 3U + mimeTypeLength + wscPayloadLength;
  const size_t textRecordLength = 4U + textPayloadLength;
  const size_t ndefLength = wscRecordLength + textRecordLength;

  if (wscPayloadLength > 255U || textPayloadLength > 255U ||
      ndefLength > MAX_EMULATED_NDEF_BYTES) {
    error = "El mensaje NDEF de Wi-Fi y texto pesa mucho.";
    return false;
  }

  memset(emulatedNdefFile, 0, sizeof(emulatedNdefFile));
  emulatedNdefFile[0] = static_cast<uint8_t>((ndefLength >> 8) & 0xFF);
  emulatedNdefFile[1] = static_cast<uint8_t>(ndefLength & 0xFF);

  size_t offset = 2;

  // Record 1: Android WSC MIME record. Its MIME type, WSC payload bytes, and
  // record order are unchanged. Only ME is cleared because a second record
  // follows in the same NDEF message.
  emulatedNdefFile[offset++] = 0x92;  // MB + SR + TNF MIME media; ME clear
  emulatedNdefFile[offset++] = static_cast<uint8_t>(mimeTypeLength);
  emulatedNdefFile[offset++] = static_cast<uint8_t>(wscPayloadLength);
  memcpy(emulatedNdefFile + offset, WSC_MIME_TYPE, mimeTypeLength);
  offset += mimeTypeLength;
  memcpy(emulatedNdefFile + offset, wscPayload, wscPayloadLength);
  offset += wscPayloadLength;

  // Record 2: iPhone-readable NFC Forum Text RTD.
  emulatedNdefFile[offset++] = 0x51;  // ME + SR + TNF well-known; MB clear
  emulatedNdefFile[offset++] = 0x01;  // type length
  emulatedNdefFile[offset++] = static_cast<uint8_t>(textPayloadLength);
  emulatedNdefFile[offset++] = 'T';
  emulatedNdefFile[offset++] = 0x02;  // UTF-8, 2-byte language code
  emulatedNdefFile[offset++] = 'e';
  emulatedNdefFile[offset++] = 'n';
  memcpy(emulatedNdefFile + offset, textPayload.c_str(), textPayload.length());
  offset += textPayload.length();

  if (offset != ndefLength + 2U) {
    error = "El largo del mensaje NDEF de Wi-Fi no cuadra.";
    return false;
  }

  emulatedNdefLength = static_cast<uint16_t>(ndefLength);
  state.emulatedRecordType = "Wi-Fi + Text";
  state.emulatedPayload =
      "SSID: " + ssid + "\nPassword: " + password;

#if NFC_DEBUG_VERBOSE
  Serial.printf(
      "[NFC][WIFI] Built dual-record onboarding message: "
      "SSID=%s total=%u bytes WSC=%u bytes Text=%u bytes\r\n",
      ssid.c_str(), static_cast<unsigned>(ndefLength),
      static_cast<unsigned>(wscPayloadLength),
      static_cast<unsigned>(textPayloadLength));
#endif
  logNfcBytes("Wi-Fi onboarding NDEF", emulatedNdefFile + 2, ndefLength);
  return true;
}

bool buildTagEmulationNdef(const String &recordType, const String &input,
                         String &error) {
  String normalized = recordType;
  normalized.toLowerCase();
  if (normalized != "text" && normalized != "url") {
    error = "La emulación solo acepta texto o URL.";
    return false;
  }
  if (input.length() == 0) {
    error = "Escribe texto o una URL antes de emular.";
    return false;
  }
  if (input.length() > MAX_TAG_EMULATION_PAYLOAD_BYTES) {
    error = "La emulación acepta hasta 220 bytes.";
    return false;
  }

  String recordPayload = input;
  uint8_t typeByte = 0;
  uint8_t prefixCode = 0;
  size_t payloadLength = 0;

  if (normalized == "url") {
    typeByte = 'U';
    prefixCode = selectUriPrefix(input, recordPayload);
    payloadLength = 1U + recordPayload.length();
  } else {
    typeByte = 'T';
    payloadLength = 3U + input.length();  // status byte + "en" + UTF-8 text
  }

  // The configured limit keeps the record in short-record form, simplifying
  // Type 4 Tag reads and keeping every APDU response inside the PN532 buffer.
  const size_t ndefLength = 4U + payloadLength;
  if (ndefLength > MAX_EMULATED_NDEF_BYTES || payloadLength > 255U) {
    error = "El registro NDEF pesa mucho para emularlo.";
    return false;
  }

  memset(emulatedNdefFile, 0, sizeof(emulatedNdefFile));
  emulatedNdefFile[0] = static_cast<uint8_t>((ndefLength >> 8) & 0xFF);
  emulatedNdefFile[1] = static_cast<uint8_t>(ndefLength & 0xFF);

  size_t offset = 2;
  emulatedNdefFile[offset++] = 0xD1;  // MB + ME + SR + TNF well-known
  emulatedNdefFile[offset++] = 0x01;  // type length
  emulatedNdefFile[offset++] = static_cast<uint8_t>(payloadLength);
  emulatedNdefFile[offset++] = typeByte;

  if (normalized == "url") {
    emulatedNdefFile[offset++] = prefixCode;
    memcpy(emulatedNdefFile + offset, recordPayload.c_str(), recordPayload.length());
    offset += recordPayload.length();
  } else {
    emulatedNdefFile[offset++] = 0x02;  // UTF-8 with a 2-byte language code
    emulatedNdefFile[offset++] = 'e';
    emulatedNdefFile[offset++] = 'n';
    memcpy(emulatedNdefFile + offset, input.c_str(), input.length());
    offset += input.length();
  }

  emulatedNdefLength = static_cast<uint16_t>(ndefLength);
  state.emulatedRecordType = normalized == "url" ? "URL" : "Text";
  state.emulatedPayload = input;
  return true;
}

void logTagEmulationFrame(const char *direction, const uint8_t *data,
                         size_t length) {
#if NFC_DEBUG_VERBOSE
  Serial.printf("[NFC][EMULATION] %s (%u): ", direction,
                static_cast<unsigned>(length));
  for (size_t i = 0; i < length; ++i) {
    if (i) Serial.print(' ');
    if (data[i] < 0x10) Serial.print('0');
    Serial.print(data[i], HEX);
  }
  Serial.println();
#else
  (void)direction;
  (void)data;
  (void)length;
#endif
}

bool sendTargetResponse(const uint8_t *data, size_t dataLength,
                        uint8_t statusHigh = 0x90,
                        uint8_t statusLow = 0x00) {
  if (dataLength > MAX_TARGET_READ_BYTES) return false;

  uint8_t response[MAX_TARGET_READ_BYTES + 3] = {0};
  response[0] = PN532_COMMAND_TGSETDATA;
  if (dataLength && data) memcpy(response + 1, data, dataLength);
  response[dataLength + 1] = statusHigh;
  response[dataLength + 2] = statusLow;

  // Adafruit PN532 1.3.4 transmits TgSetData correctly, but its
  // setDataTarget() return-value check compares the response-command byte with
  // 0x15 (the SAMConfiguration response) instead of checking TgSetData's 0x8F
  // response/status. A valid R-APDU is therefore sent even though the helper
  // returns false. Do not tear down the tag-emulation session based on that broken
  // return value; the next TgGetData call is the reliable session-health test.
  logTagEmulationFrame("TX", response + 1, dataLength + 2);
  (void)nfc.setDataTarget(response, static_cast<uint8_t>(dataLength + 3));
  return true;
}

bool selectNdefApplication(const uint8_t *apdu, uint8_t length) {
  // NFC Forum Type 4 Tag applications exist with both the original 1.0 AID
  // (...0100) and the current 2.0 AID (...0101). Android normally selects
  // 2.0; iPhone/Core NFC readers and third-party apps may probe either.
  static const uint8_t NDEF_APPLICATION_ID_V1[] = {
      0xD2, 0x76, 0x00, 0x00, 0x85, 0x01, 0x00};
  static const uint8_t NDEF_APPLICATION_ID_V2[] = {
      0xD2, 0x76, 0x00, 0x00, 0x85, 0x01, 0x01};

  if (!apdu || length < 12 || apdu[1] != 0xA4 || apdu[2] != 0x04 ||
      apdu[4] != 0x07) {
    return false;
  }

  if (memcmp(apdu + 5, NDEF_APPLICATION_ID_V2,
             sizeof(NDEF_APPLICATION_ID_V2)) == 0) {
    selectedNdefApplicationVersion = NdefApplicationVersion::V2_0;
#if NFC_DEBUG_VERBOSE
    Serial.println("[NFC][EMULATION] Selected NDEF Tag Application v2.0");
#endif
    return true;
  }
  if (memcmp(apdu + 5, NDEF_APPLICATION_ID_V1,
             sizeof(NDEF_APPLICATION_ID_V1)) == 0) {
    selectedNdefApplicationVersion = NdefApplicationVersion::V1_0;
#if NFC_DEBUG_VERBOSE
    Serial.println("[NFC][EMULATION] Selected NDEF Tag Application v1.0");
#endif
    return true;
  }
  return false;
}

void resetTagEmulationSession() {
  tagEmulationSessionActive = false;
  tagScanCounted = false;
  ndefApplicationSelected = false;
  selectedNdefApplicationVersion = NdefApplicationVersion::NONE;
  selectedEmulatedFile = EmulatedFile::NONE;
  highestEmulatedReadOffset = 0;
  state.emulationReaderConnected = false;
}

bool processTagEmulationApdu(const uint8_t *apdu, uint8_t length) {
  if (length < 2) return sendTargetResponse(nullptr, 0, 0x67, 0x00);

  // SELECT NDEF Tag Application: D2760000850101
  if (apdu[1] == 0xA4 && apdu[2] == 0x04) {
    if (!selectNdefApplication(apdu, length)) {
      return sendTargetResponse(nullptr, 0, 0x6A, 0x82);
    }
    ndefApplicationSelected = true;
    selectedEmulatedFile = EmulatedFile::NONE;
    return sendTargetResponse(nullptr, 0);
  }

  // SELECT FILE: E103 is the Capability Container; E104 is the NDEF file.
  if (apdu[1] == 0xA4 && (apdu[2] == 0x00 || apdu[2] == 0x02)) {
    if (!ndefApplicationSelected) {
      return sendTargetResponse(nullptr, 0, 0x69, 0x85);
    }
    if (length < 7 || apdu[4] != 0x02) {
      return sendTargetResponse(nullptr, 0, 0x67, 0x00);
    }

    const uint16_t fileId =
        (static_cast<uint16_t>(apdu[5]) << 8) | apdu[6];
    if (fileId == 0xE103) {
      selectedEmulatedFile = EmulatedFile::CAPABILITY_CONTAINER;
      return sendTargetResponse(nullptr, 0);
    }
    if (fileId == 0xE104) {
      selectedEmulatedFile = EmulatedFile::NDEF;
      return sendTargetResponse(nullptr, 0);
    }
    return sendTargetResponse(nullptr, 0, 0x6A, 0x82);
  }

  // READ BINARY from the currently selected file.
  if (apdu[1] == 0xB0) {
    if (length < 5 || selectedEmulatedFile == EmulatedFile::NONE) {
      return sendTargetResponse(nullptr, 0, 0x69, 0x85);
    }

    const uint16_t readOffset =
        (static_cast<uint16_t>(apdu[2]) << 8) | apdu[3];

    // Accept both short and extended Le encodings. The Type 4 mapping normally
    // uses short APDUs, but some iPhone NFC applications issue extended-form
    // READ BINARY commands while probing an ISO7816 tag.
    uint16_t requested = 0;
    if (apdu[4] != 0) {
      requested = apdu[4];
    } else if (length >= 7) {
      requested = (static_cast<uint16_t>(apdu[5]) << 8) | apdu[6];
      if (requested == 0) requested = 256U;
    } else {
      requested = 256U;
    }

    const uint8_t *fileData = nullptr;
    size_t fileLength = 0;
    if (selectedEmulatedFile == EmulatedFile::CAPABILITY_CONTAINER) {
      if (selectedNdefApplicationVersion == NdefApplicationVersion::V1_0) {
        fileData = TYPE4_CAPABILITY_CONTAINER_V1;
        fileLength = sizeof(TYPE4_CAPABILITY_CONTAINER_V1);
      } else {
        fileData = TYPE4_CAPABILITY_CONTAINER_V2;
        fileLength = sizeof(TYPE4_CAPABILITY_CONTAINER_V2);
      }
    } else {
      fileData = emulatedNdefFile;
      fileLength = static_cast<size_t>(emulatedNdefLength) + 2U;
    }

    if (readOffset >= fileLength) {
      return sendTargetResponse(nullptr, 0, 0x6B, 0x00);
    }

    const size_t remaining = fileLength - readOffset;
    const size_t responseLength =
        std::min<size_t>(std::min<size_t>(requested, remaining),
                         MAX_TARGET_READ_BYTES);
    if (!sendTargetResponse(fileData + readOffset, responseLength)) return false;

    if (selectedEmulatedFile == EmulatedFile::NDEF) {
      const uint16_t readEnd = static_cast<uint16_t>(readOffset + responseLength);
      highestEmulatedReadOffset = std::max<uint16_t>(highestEmulatedReadOffset, readEnd);
      if (!tagScanCounted && highestEmulatedReadOffset >= fileLength) {
        tagScanCounted = true;
        ++state.tagScans;
        state.lastTagScanAt = millis();
        state.emulationMessage = "Registro NDEF leído del tag emulado.";
        state.message = state.emulationMessage;
        state.updatedAt = millis();
        Serial.printf("[NFC] Emulated %s record scanned (%u bytes)\r\n",
                      state.emulatedRecordType.c_str(),
                      static_cast<unsigned>(emulatedNdefLength));
      }
    }
    return true;
  }

  // iPhone readers can issue discovery/probing commands before selecting the
  // NFC Forum NDEF application. These responses are ISO7816-compliant and do
  // not change the existing Android/WSC read sequence.
  if (apdu[1] == 0x70) {  // MANAGE CHANNEL is not supported
    return sendTargetResponse(nullptr, 0, 0x6A, 0x81);
  }
  if (apdu[1] == 0xC0) {  // GET RESPONSE: no pending response data
    return sendTargetResponse(nullptr, 0);
  }

  // This emulated tag is deliberately read-only.
  if (apdu[1] == 0xD6) {
    return sendTargetResponse(nullptr, 0, 0x69, 0x82);
  }

  return sendTargetResponse(nullptr, 0, 0x6D, 0x00);
}

void serviceTagEmulation() {
  const uint32_t now = millis();
  uint8_t apdu[64] = {0};
  uint8_t apduLength = 0;

  if (!tagEmulationSessionActive) {
    if (now - lastTargetAttempt < TARGET_RETRY_INTERVAL_MS) return;
    lastTargetAttempt = now;

    // Use the same target activation methodology that has proven reliable for
    // the dual-record Wi-Fi credential emulation on both Android and iPhone.
    //
    // Adafruit PN532 1.3.4's AsTarget() success check is incorrect: after
    // TgInitAsTarget it checks for 0x15 instead of the TgInitAsTarget response
    // code. The PN532 can already be activated by the phone while AsTarget()
    // still returns false. Start target mode, then use the first successfully
    // received C-APDU as proof that a tag-emulation session is active.
    (void)nfc.AsTarget();

    if (!nfc.getDataTarget(apdu, &apduLength) || apduLength == 0) {
      state.emulationReaderConnected = false;
      return;
    }

    tagEmulationSessionActive = true;
    tagScanCounted = false;
    ndefApplicationSelected = false;
    selectedEmulatedFile = EmulatedFile::NONE;
    highestEmulatedReadOffset = 0;
    state.emulationReaderConnected = true;
    state.emulationMessage = "Lector NFC detectado. Sirviendo el registro NDEF…";
    state.message = state.emulationMessage;
    state.updatedAt = millis();
    if (tagEmulationProfile == TagEmulationProfile::WIFI_WSC_ANDROID) {
      Serial.println("[NFC] NFC reader connected to WSC emulated tag");
    } else {
      Serial.println(
          "[NFC] NFC reader connected to user-defined NDEF emulated tag");
    }
  } else if (!nfc.getDataTarget(apdu, &apduLength) || apduLength == 0) {
    resetTagEmulationSession();
    if (state.tagEmulationEnabled) {
      state.emulationMessage =
          "Listo. Acerca un lector NFC o teléfono a la antena del PN532.";
      state.message = state.emulationMessage;
      state.updatedAt = millis();
    }
    return;
  }

  logTagEmulationFrame("RX", apdu, apduLength);
  if (!processTagEmulationApdu(apdu, apduLength)) {
    resetTagEmulationSession();
    state.emulationMessage =
        "El lector NFC se fue antes de leer todo el registro.";
    state.message = state.emulationMessage;
    state.updatedAt = millis();
  }
}


// --- MIFARE Classic dictionary read -----------------------------------------
//
// Interop, not attack. The badge tries keys the world already publishes -- the
// factory default, the NFC Forum's public NDEF keys, and a handful of common
// transit/vendor keys -- so a Classic card left on one of them reads its
// content instead of surfacing only a UID. It deliberately does NOT recover
// unknown keys: no darkside, no nested Crypto1. A card whose sectors are on
// diversified keys stays a UID here, which is the honest outcome.
//
// Every entry is six bytes and lives in flash (.rodata), so a long list costs
// no RAM. Order matters only for speed: the most common keys come first, and a
// key that unlocks one sector is retried first on the next.
const uint8_t CLASSIC_KEYS[][6] = {
    {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},  // factory default
    {0xD3, 0xF7, 0xD3, 0xF7, 0xD3, 0xF7},  // NFC Forum NDEF, data sectors
    {0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5},  // NFC Forum MAD, sector 0 key A
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00},  // all zeros
    {0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5},
    {0xA0, 0xB0, 0xC0, 0xD0, 0xE0, 0xF0},
    {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF},
    {0x4D, 0x3A, 0x99, 0xC3, 0x51, 0xDD},
    {0x1A, 0x98, 0x2C, 0x7E, 0x45, 0x9A},
    {0x71, 0x4C, 0x5C, 0x88, 0x6E, 0x97},
    {0x58, 0x7E, 0xE5, 0xF9, 0x35, 0x0F},
    {0xA0, 0x47, 0x8C, 0xC3, 0x90, 0x91},
    {0x53, 0x3C, 0xB6, 0xC7, 0x23, 0xF6},
    {0x8F, 0xD0, 0xA4, 0xF2, 0x56, 0xE9},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x01},
    {0xB1, 0x27, 0xC6, 0xF4, 0x1C, 0x11},
    {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC},
    {0x64, 0x71, 0xA5, 0xEF, 0x2D, 0x1A},
    {0x4E, 0x35, 0x52, 0x42, 0x6B, 0x32},
    {0x6A, 0x19, 0x87, 0xC4, 0x0A, 0x21},
};
constexpr uint8_t CLASSIC_KEY_COUNT =
    sizeof(CLASSIC_KEYS) / sizeof(CLASSIC_KEYS[0]);

// A failed authentication leaves the PN532's view of the card halted, so the
// card must be re-selected before the next attempt or every following auth
// fails too. Re-selection is a fresh passive poll; the card is sitting on the
// antenna during a read, so it costs only a few milliseconds.
bool reselectForClassic(const uint8_t *expectUid, uint8_t expectLen) {
  uint8_t uid[10] = {};
  uint8_t len = 0;
  if (!nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &len,
                               TAG_POLL_TIMEOUT_MS)) {
    return false;
  }
  // A different card mid-read means the encounter changed; abandon rather than
  // stitch two cards' sectors into one result.
  return len == expectLen && memcmp(uid, expectUid, len) == 0;
}

// Whole-scan budget. A cooperative card finishes in well under a second; this
// only bounds the pathological case where most sectors reject every key and
// each rejection pays a re-selection.
constexpr uint32_t CLASSIC_READ_BUDGET_MS = 4000;

bool readMifareClassic(const uint8_t *uid, uint8_t uidLength) {
  // 1K is sixteen 4-block sectors. 4K's upper sectors are rarely NDEF and cost
  // the most time, so content parsing stays within the first 1K; the sector
  // tally still reflects that the deeper region was not walked.
  constexpr uint8_t SECTOR_COUNT = 16;
  constexpr uint8_t BLOCKS_PER_SECTOR = 4;

  const uint32_t started = millis();
  uint8_t readableSectors = 0;
  size_t accumulated = 0;
  int fastKey = -1;
  uint8_t fastType = 0;
  uint8_t workingKey[6] = {};

  for (uint8_t sector = 0; sector < SECTOR_COUNT; ++sector) {
    if (millis() - started > CLASSIC_READ_BUDGET_MS) break;
    const uint8_t firstBlock = sector * BLOCKS_PER_SECTOR;
    bool authed = false;

    // Fast path: the key that unlocked the previous sector, tried first. Most
    // cards key every sector alike, or share one data key across the NDEF
    // region, so this collapses the common case to a single auth per sector.
    if (fastKey >= 0 && reselectForClassic(uid, uidLength) &&
        nfc.mifareclassic_AuthenticateBlock(const_cast<uint8_t *>(uid),
                                            uidLength, firstBlock, fastType,
                                            workingKey)) {
      authed = true;
    }

    for (uint8_t k = 0; k < CLASSIC_KEY_COUNT && !authed; ++k) {
      for (uint8_t keyType = 0; keyType <= 1 && !authed; ++keyType) {
        if (millis() - started > CLASSIC_READ_BUDGET_MS) break;
        uint8_t key[6];
        memcpy(key, CLASSIC_KEYS[k], 6);
        if (!reselectForClassic(uid, uidLength)) return accumulated > 0;
        if (nfc.mifareclassic_AuthenticateBlock(const_cast<uint8_t *>(uid),
                                                uidLength, firstBlock, keyType,
                                                key)) {
          authed = true;
          fastKey = k;
          fastType = keyType;
          memcpy(workingKey, key, 6);
        }
      }
    }
    if (!authed) continue;
    ++readableSectors;

    // Data blocks only; the trailer (last block of the sector) holds the keys
    // and access bits, never content. Sector 0 block 0 is the read-only
    // manufacturer/UID block and blocks 1-2 are the MAD -- kept, because the
    // NDEF TLV search below simply skips over non-NDEF bytes.
    for (uint8_t b = 0; b < BLOCKS_PER_SECTOR - 1; ++b) {
      if (accumulated + 16 > MAX_TYPE2_USER_BYTES) break;
      uint8_t block[16];
      if (nfc.mifareclassic_ReadDataBlock(firstBlock + b, block)) {
        memcpy(type2Buffer + accumulated, block, 16);
        accumulated += 16;
      }
    }
  }

  if (readableSectors == 0) return false;  // UID-only card; caller reports it

  state.uid = uidToString(uid, uidLength);
  state.tagType = "MIFARE Classic";
  state.capacity = static_cast<uint16_t>(accumulated);

  // Classic NDEF wraps its message in a TLV stream (0x03 = NDEF) inside the
  // data blocks of the MAD-designated sectors. Rather than parse the MAD, scan
  // the readable bytes for a 0x03 TLV with a length that fits, and hand the
  // message to the same NDEF parser the Type 2 path uses. A card with no NDEF
  // (raw access data) falls through to a plain summary.
  for (size_t i = 0; i + 1 < accumulated; ++i) {
    if (type2Buffer[i] != 0x03) continue;
    size_t length = type2Buffer[i + 1];
    size_t headerBytes = 2;
    if (length == 0xFF) {
      if (i + 3 >= accumulated) continue;
      length = (static_cast<size_t>(type2Buffer[i + 2]) << 8) |
               static_cast<size_t>(type2Buffer[i + 3]);
      headerBytes = 4;
    }
    if (length == 0 || length > accumulated - i - headerBytes) continue;
    if (parseNdefRecord(type2Buffer + i + headerBytes, length)) {
      return true;
    }
  }

  // No NDEF, but sectors did open. Report what was recovered so the encounter
  // is still useful: how much of the card read on known keys, plus a short
  // printable preview of the first data.
  state.recordType = "MIFARE Classic";
  String preview;
  for (size_t i = 0; i < accumulated && preview.length() < 48; ++i) {
    const char c = static_cast<char>(type2Buffer[i]);
    preview += (c >= 32 && c <= 126) ? c : '.';
  }
  state.payload = String(readableSectors) +
                  " sector(es) leídos con llaves conocidas. Sin NDEF. Vista: " +
                  preview;
  return true;
}


// --- NFC Forum Type 4 read (APDU over ISO-DEP) ------------------------------
//
// A DESFire-backed tag, or a phone sharing a record, presents its NDEF as a
// Type 4 file read over ISO14443-4 APDUs rather than as Type 2 pages. The badge
// already *emulates* a Type 4 tag; this walks the same protocol from the reader
// side, so it can pull NDEF from anything that speaks it. Only the public NDEF
// application is touched -- no authentication, nothing beyond the NDEF file.

// One APDU round trip. `respLen` is the buffer capacity going in and the reply
// length coming out; a well-formed Type 4 reply ends in the 90 00 status word.
bool type4Exchange(const uint8_t *apdu, uint8_t apduLen, uint8_t *resp,
                   uint8_t *respLen) {
  const uint8_t capacity = *respLen;
  if (!nfc.inDataExchange(const_cast<uint8_t *>(apdu), apduLen, resp, respLen)) {
    return false;
  }
  if (*respLen < 2 || *respLen > capacity) return false;
  return resp[*respLen - 2] == 0x90 && resp[*respLen - 1] == 0x00;
}

bool readType4Tag() {
  // Re-activate first: a prior failed Type 2 read can leave the card halted,
  // and inDataExchange talks to whatever the last poll selected.
  {
    uint8_t uid[10] = {};
    uint8_t len = 0;
    if (!nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &len,
                                 TAG_POLL_TIMEOUT_MS)) {
      return false;
    }
  }

  uint8_t resp[64];
  uint8_t respLen;

  // 1. SELECT the NDEF tag application (name D2 76 00 00 85 01 01).
  static const uint8_t SELECT_APP[] = {0x00, 0xA4, 0x04, 0x00, 0x07, 0xD2, 0x76,
                                       0x00, 0x00, 0x85, 0x01, 0x01, 0x00};
  respLen = sizeof(resp);
  if (!type4Exchange(SELECT_APP, sizeof(SELECT_APP), resp, &respLen)) {
    return false;  // not a Type 4 tag; fails fast
  }

  // 2. SELECT the Capability Container file (E1 03).
  static const uint8_t SELECT_CC[] = {0x00, 0xA4, 0x00, 0x0C, 0x02, 0xE1, 0x03};
  respLen = sizeof(resp);
  if (!type4Exchange(SELECT_CC, sizeof(SELECT_CC), resp, &respLen)) return false;

  // 3. READ the 15-byte CC; its NDEF File Control TLV names the NDEF file id.
  static const uint8_t READ_CC[] = {0x00, 0xB0, 0x00, 0x00, 0x0F};
  respLen = sizeof(resp);
  if (!type4Exchange(READ_CC, sizeof(READ_CC), resp, &respLen)) return false;
  if (respLen < 2 + 11) return false;
  const uint8_t ndefFileHi = resp[9];   // CC bytes 9-10: NDEF file id, inside
  const uint8_t ndefFileLo = resp[10];  // the File Control TLV (tag 04, len 06)

  // 4. SELECT the NDEF file.
  const uint8_t selectNdef[] = {0x00,       0xA4, 0x00, 0x0C,
                                0x02,       ndefFileHi, ndefFileLo};
  respLen = sizeof(resp);
  if (!type4Exchange(selectNdef, sizeof(selectNdef), resp, &respLen)) return false;

  // 5. READ the 2-byte NLEN (message length) from the file's first two bytes.
  static const uint8_t READ_NLEN[] = {0x00, 0xB0, 0x00, 0x00, 0x02};
  respLen = sizeof(resp);
  if (!type4Exchange(READ_NLEN, sizeof(READ_NLEN), resp, &respLen)) return false;
  if (respLen < 2 + 2) return false;
  size_t ndefLen = (static_cast<size_t>(resp[0]) << 8) | resp[1];

  state.tagType = "NFC Forum Type 4";
  if (ndefLen == 0) {
    state.recordType = "Empty";
    state.payload = "El tag Type 4 trae un mensaje NDEF vacío.";
    return true;
  }
  if (ndefLen > MAX_TYPE2_USER_BYTES) ndefLen = MAX_TYPE2_USER_BYTES;

  // 6. READ the NDEF message in chunks. Content starts at offset 2, past NLEN.
  //    The PN532's packet buffer caps a single reply, so keep each Le small.
  size_t got = 0;
  while (got < ndefLen) {
    const size_t offset = 2 + got;
    const uint8_t want =
        static_cast<uint8_t>(std::min<size_t>(48, ndefLen - got));
    const uint8_t readCmd[] = {0x00, 0xB0,
                               static_cast<uint8_t>((offset >> 8) & 0xFF),
                               static_cast<uint8_t>(offset & 0xFF), want};
    respLen = sizeof(resp);
    if (!type4Exchange(readCmd, sizeof(readCmd), resp, &respLen)) break;
    const size_t payloadBytes = respLen - 2;  // drop the 90 00 status word
    if (payloadBytes == 0) break;
    const size_t copy = std::min<size_t>(payloadBytes, ndefLen - got);
    memcpy(type2Buffer + got, resp, copy);
    got += copy;
  }
  if (got == 0) return false;

  state.capacity = static_cast<uint16_t>(got);
  if (parseNdefRecord(type2Buffer, got)) return true;
  state.recordType = "NDEF";
  state.payload = "Se leyó un tag Type 4, pero el NDEF no se pudo interpretar.";
  return true;
}


// --- MIFARE Classic write (URL only, tested library path) -------------------
//
// Writing NDEF to Classic means rewriting a sector trailer -- its keys and
// access bits -- and a wrong trailer locks the sector for good. So this uses
// only the library's tested Format + WriteNDEFURI sequence, and only for a URL
// up to 38 bytes in sector 1 of a factory-keyed (or already-NDEF) card. Text
// records and multi-sector payloads are deliberately not hand-rolled onto
// Classic trailers here.
bool classicAuthKeyA(const uint8_t *uid, uint8_t uidLength, uint8_t block,
                     const uint8_t *key) {
  if (!reselectForClassic(uid, uidLength)) return false;
  return nfc.mifareclassic_AuthenticateBlock(const_cast<uint8_t *>(uid),
                                             uidLength, block, 0,
                                             const_cast<uint8_t *>(key));
}

bool writeMifareClassicUri(const uint8_t *uid, uint8_t uidLength,
                           const String &url, String &error) {
  static const uint8_t FACTORY[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  static const uint8_t MAD_A[6] = {0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5};
  static const uint8_t NDEF_A[6] = {0xD3, 0xF7, 0xD3, 0xF7, 0xD3, 0xF7};

  String remainder;
  const uint8_t prefix = selectUriPrefix(url, remainder);
  if (remainder.length() < 1 || remainder.length() > 38) {
    error =
        "La URL para MIFARE Classic debe medir entre 1 y 38 caracteres después "
        "del prefijo.";
    return false;
  }

  // Format sector 0's MAD on a factory-fresh card; a card already NDEF (MAD key
  // A0A1) is left as it is. Either way sector 0 must authenticate, or the card
  // is on unknown keys and cannot be safely formatted.
  if (classicAuthKeyA(uid, uidLength, 0, FACTORY)) {
    nfc.mifareclassic_FormatNDEF();
  } else if (!classicAuthKeyA(uid, uidLength, 0, MAD_A)) {
    error =
        "La tarjeta MIFARE Classic no responde a llaves de fábrica ni NDEF; no "
        "se puede formatear para escritura.";
    return false;
  }

  // Authenticate sector 1 for the data write: factory key first, then the NDEF
  // data key if the sector was already formatted by a previous write.
  if (!classicAuthKeyA(uid, uidLength, 4, FACTORY) &&
      !classicAuthKeyA(uid, uidLength, 4, NDEF_A)) {
    error = "No se pudo autenticar el sector 1 para escribir.";
    return false;
  }

  if (!nfc.mifareclassic_WriteNDEFURI(1, prefix, remainder.c_str())) {
    error = "Falló la escritura NDEF en MIFARE Classic.";
    return false;
  }
  return true;
}

// Names a card family from its SAK (SEL_RES) byte, the identity the PN532
// returns on selection. This works even when the card cannot be read -- a
// locked MIFARE Classic still says it is a Classic 1K -- which is what turns a
// bare "UID only" row into something useful. The successful readers below
// refine this with what they actually parsed; this is the baseline and the
// last word for a tag nothing could open.
String cardTypeFromSAK(uint8_t sak, uint8_t uidLength) {
  switch (sak) {
    case 0x00:
      return uidLength == 7 ? "NTAG / MIFARE Ultralight" : "MIFARE Ultralight";
    case 0x08: return "MIFARE Classic 1K";
    case 0x09: return "MIFARE Mini";
    case 0x18: return "MIFARE Classic 4K";
    case 0x10: return "MIFARE Plus 2K";
    case 0x11: return "MIFARE Plus 4K";
    case 0x20: return "ISO14443-4 (DESFire / Type 4)";
    case 0x28: return "SmartMX + Classic 1K";
    case 0x38: return "SmartMX + Classic 4K";
    default: break;
  }
  if (sak & 0x20) return "ISO14443-4 (Type 4)";
  if (sak & 0x08) return "MIFARE Classic compatible";
  char buffer[28];
  snprintf(buffer, sizeof(buffer), "ISO14443A (SAK 0x%02X)", sak);
  return String(buffer);
}

void identifyTag(const uint8_t *uid, uint8_t uidLength) {
  clearTagResult();
  state.uid = uidToString(uid, uidLength);
  state.tagType = cardTypeFromSAK(nfc.lastSAK(), uidLength);
}

void processDetectedTag(uint8_t *uid, uint8_t uidLength) {
  identifyTag(uid, uidLength);
#if NFC_DEBUG_VERBOSE
  Serial.printf("[NFC][TAG] Detected UID=%s length=%u operation=%s\r\n",
                state.uid.c_str(), uidLength, operationName(pendingOperation));
#endif

  if (pendingOperation == NfcOperation::READ) {
    // A genuine NTAG / Ultralight (NFC Forum Type 2) always carries a 7-byte
    // UID. A 4-byte UID is MIFARE Classic or a Type 4 emulator -- and a Classic
    // card can even hand back a bogus "successful" Type 2 read, the same junk
    // on every page (e.g. 67 00 83 00), which used to be shown as content. So a
    // 4-byte tag is resolved straight as Type 4 or Classic, never as a Type 2
    // preview.
    if (uidLength == 4) {
      if (readType4Tag()) {
        finishSuccess("Tag Type 4 leído.");
        return;
      }
      if (readMifareClassic(uid, uidLength)) {
        finishSuccess("MIFARE Classic leído.");
        return;
      }
      state.recordType = "UID only";
      state.payload =
          "Se leyó el UID. No respondió como Type 4 ni abrió con llaves MIFARE "
          "Classic conocidas.";
      finishSuccess("UID del tag leído.");
      return;
    }

    // 7-byte UID: NTAG / Ultralight (Type 2), or a DESFire-backed Type 4 tag.
    bool type2MemoryResponded = false;
    if (readType2Tag(type2MemoryResponded)) {
      finishSuccess("Tag leído.");
      return;
    }

    if (type2MemoryResponded) {
      const String detail = type2IoError.length()
                                ? " " + type2IoError
                                : String();
      finishError("Se detectó el tag, pero no se pudo leer su memoria Type 2." +
                  detail);
      return;
    }

    // Type 2 stayed silent. Try Type 4: a quick APDU probe that fails fast on
    // anything that is not ISO14443-4, and how DESFire tags and phones present
    // their NDEF.
    if (readType4Tag()) {
      finishSuccess("Tag Type 4 leído.");
      return;
    }

    state.recordType = "UID only";
    state.payload =
        "Se leyó el UID. No trae NDEF Type 2 legible ni respondió como Type 4.";
    finishSuccess("UID del tag leído.");
    return;
  }

  uint16_t capacity = 0;
  String error;
  if (!prepareType2ForWrite(capacity, error)) {
    // Type 2 preparation fails on a MIFARE Classic card. Offer the tested
    // Classic URL path for a 4-byte card; text and other families are not
    // hand-rolled onto Classic trailers.
    if (uidLength == 4 && pendingOperation == NfcOperation::WRITE_URL) {
      String classicError;
      if (writeMifareClassicUri(uid, uidLength, pendingPayload, classicError)) {
        state.tagType = "MIFARE Classic";
        state.recordType = "URL";
        state.payload = pendingPayload;
        state.writable = true;
        finishSuccess("URL escrita en MIFARE Classic.");
        return;
      }
      finishError(classicError);
      return;
    }
    if (uidLength == 4 && pendingOperation == NfcOperation::WRITE_TEXT) {
      finishError(
          "Para MIFARE Classic el badge escribe solo URLs; escribe texto en un "
          "tag NTAG / Ultralight (Type 2).");
      return;
    }
    finishError(error);
    return;
  }
  state.capacity = capacity;
  state.writable = true;
  state.tagType = "NFC Forum Type 2 (NTAG / Ultralight)";

  const String recordType =
      pendingOperation == NfcOperation::WRITE_URL ? "url" : "text";
#if NFC_DEBUG_VERBOSE
  Serial.printf("[NFC][WRITE] Preparing %s payload (%u bytes): %s\r\n",
                recordType.c_str(), static_cast<unsigned>(pendingPayload.length()),
                pendingPayload.c_str());
#endif
  size_t totalLength = 0;
  if (!buildNdef(recordType, pendingPayload, capacity, totalLength, error)) {
    finishError(error);
    return;
  }
  if (!writeType2Buffer(totalLength)) {
    finishError("Falló la escritura antes de guardar todo el registro NDEF. " +
                type2IoError);
    return;
  }

  // Incremental verification reads only through the committed NDEF TLV instead
  // of dumping the complete tag capacity.
  bool type2MemoryResponded = false;
  if (!readType2Tag(type2MemoryResponded)) {
    finishError("El tag se escribió, pero falló la lectura de prueba. " +
                type2IoError);
    return;
  }
  finishSuccess(recordType == "url" ? "URL escrita y verificada."
                                     : "Texto escrito y verificado.");
}

// -----------------------------------------------------------------------------
// Dedicated NFC worker and command queue
// -----------------------------------------------------------------------------
void publishNfcState() {
  if (!nfcStateMutex) {
    publishedState = state;
    return;
  }

  if (xSemaphoreTake(nfcStateMutex, portMAX_DELAY) == pdTRUE) {
    publishedState = state;
    xSemaphoreGive(nfcStateMutex);
  }
}

NfcState getPublishedStateSnapshot() {
  NfcState snapshot;
  if (!nfcStateMutex) return publishedState;

  if (xSemaphoreTake(nfcStateMutex, portMAX_DELAY) == pdTRUE) {
    snapshot = publishedState;
    xSemaphoreGive(nfcStateMutex);
  }
  return snapshot;
}

void setPublishedErrorLocked(const String &message) {
  publishedState.status = "error";
  publishedState.message = message;
  publishedState.updatedAt = millis();
}

void setPublishedError(const String &message) {
  if (!nfcStateMutex ||
      xSemaphoreTake(nfcStateMutex, portMAX_DELAY) != pdTRUE) {
    return;
  }
  setPublishedErrorLocked(message);
  xSemaphoreGive(nfcStateMutex);
}

bool copyCommandString(const String &source, char *destination,
                       size_t destinationSize) {
  if (!destination || destinationSize == 0 ||
      source.length() >= destinationSize) {
    return false;
  }

  memcpy(destination, source.c_str(), source.length());
  destination[source.length()] = '\0';
  return true;
}

bool sendCommandWhileLocked(const NfcCommand &command) {
  if (!nfcCommandQueue || commandPending) return false;

  commandPending = true;
  if (xQueueSend(nfcCommandQueue, &command, 0) != pdTRUE) {
    commandPending = false;
    return false;
  }
  return true;
}

void markCommandDequeued() {
  if (!nfcStateMutex) {
    commandPending = false;
    return;
  }

  if (xSemaphoreTake(nfcStateMutex, portMAX_DELAY) == pdTRUE) {
    commandPending = false;
    xSemaphoreGive(nfcStateMutex);
  }
}

bool executeQueueNfcRead() {
  if (!beginOperation(NfcOperation::READ,
                      "Acerca un tag NFC al lector del PCB para leerlo.")) {
    return false;
  }
  pendingPayload = String();
  clearTagResult();
  return true;
}

bool executeQueueNfcWrite(const NfcCommand &command) {
  const bool isUrl = command.type == NfcCommandType::WRITE_URL;
  const NfcOperation operation =
      isUrl ? NfcOperation::WRITE_URL : NfcOperation::WRITE_TEXT;
  const char *message =
      isUrl
          ? "Acerca un tag Type 2 que se pueda escribir para guardar la URL."
          : "Acerca un tag Type 2 que se pueda escribir para guardar el texto.";

  if (!beginOperation(operation, message)) return false;

  pendingPayload = String(command.payload);
  clearTagResult();
  return true;
}

// Emulation and constant scanning both want the one radio. Whichever the
// operator asks for last wins, and the other is switched off rather than left
// reading as on while doing nothing. Called where emulation actually starts,
// so a start that fails its checks leaves capture alone.
void releaseCaptureForEmulation() {
  if (!state.captureEnabled) return;
  state.captureEnabled = false;
  state.captureMessage = "El escaneo automático se apagó para emular.";
  lastCaptureUid = String();
  lastCaptureAt = 0;
  captureMisses = 0;
  Serial.println("[NFC][CAPTURE] Disabled to free the reader for emulation");
}

bool executeStartTagEmulation(const NfcCommand &command) {
  if (!state.readerReady) {
    state.status = "error";
    state.message = "El lector PN532 no está disponible.";
    state.updatedAt = millis();
    return false;
  }
  if (pendingOperation != NfcOperation::NONE ||
      (state.busy && !state.tagEmulationEnabled)) {
    state.status = "error";
    state.message = "Espera a que termine la acción del tag.";
    state.updatedAt = millis();
    return false;
  }

  const String recordType =
      command.type == NfcCommandType::START_URL_EMULATION ? "url" : "text";
  const String payload(command.payload);
  String error;
  if (!buildTagEmulationNdef(recordType, payload, error)) {
    state.status = "error";
    state.message = error;
    state.emulationMessage = error;
    state.updatedAt = millis();
    return false;
  }

  tagEmulationProfile = TagEmulationProfile::GENERIC_NDEF;
  resetTagEmulationSession();
  state.tagScans = 0;
  state.lastTagScanAt = 0;
  releaseCaptureForEmulation();
  state.tagEmulationEnabled = true;
  state.wifiOnboardingActive = false;
  state.busy = true;
  state.status = "emulating";
  state.emulationMessage =
      "Listo. Acerca un lector NFC o teléfono a la antena del PN532.";
  state.message = state.emulationMessage;
  state.updatedAt = millis();
  lastTargetAttempt = 0;

  Serial.printf("[NFC] Tag emulation started: %s, %u payload bytes\r\n",
                state.emulatedRecordType.c_str(),
                static_cast<unsigned>(payload.length()));
  return true;
}

bool executeStartWifiOnboarding(const NfcCommand &command) {
  if (!state.readerReady) {
    state.status = "error";
    state.message = "El PN532 no está disponible para pasar Wi-Fi.";
    state.updatedAt = millis();
    return false;
  }
  if (pendingOperation != NfcOperation::NONE ||
      (state.busy && !state.tagEmulationEnabled)) {
    state.status = "error";
    state.message = "Espera a que termine la acción NFC.";
    state.updatedAt = millis();
    return false;
  }

  const String ssid(command.ssid);
  const String password(command.password);
  String error;
  if (!buildWifiOnboardingNdef(
          ssid,
          password,
          command.hasApMac ? command.apMac : nullptr,
          error)) {
    state.status = "error";
    state.message = error;
    state.emulationMessage = error;
    state.updatedAt = millis();
    Serial.printf("[NFC][WIFI] ERROR: %s\r\n", error.c_str());
    return false;
  }

  tagEmulationProfile = TagEmulationProfile::WIFI_WSC_ANDROID;
  resetTagEmulationSession();
  state.tagScans = 0;
  state.lastTagScanAt = 0;
  releaseCaptureForEmulation();
  state.tagEmulationEnabled = true;
  state.wifiOnboardingActive = true;
  state.busy = true;
  state.status = "emulating";
  state.emulationMessage =
      "Wi-Fi listo. Lee el badge para conectarte a su red.";
  state.message = state.emulationMessage;
  state.updatedAt = millis();
  lastTargetAttempt = 0;

  Serial.printf("[NFC][WIFI] Tag emulation started for SSID: %s\r\n",
                ssid.c_str());
  return true;
}

bool executeStopTagEmulation() {
  const bool wasEnabled = state.tagEmulationEnabled;

  state.tagEmulationEnabled = false;
  state.wifiOnboardingActive = false;
  resetTagEmulationSession();
  state.busy = false;
  state.status = "idle";
  state.emulationMessage = "La emulación está parada.";
  state.message = "Lector listo. Elige una acción y acerca un tag.";
  state.updatedAt = millis();

  if (state.readerReady &&
      !configurePassiveReader("stopping tag emulation")) {
    state.message = "El PN532 no volvió al modo lector.";
    state.emulationMessage = state.message;
    state.updatedAt = millis();
    return false;
  }

  if (wasEnabled) Serial.println("[NFC] Tag emulation stopped");
  return true;
}

bool executeSetCapture(const NfcCommand &command) {
  if (command.flag == state.captureEnabled) return true;

  // The PN532 does one thing at a time, so constant scanning means taking the
  // radio off emulation rather than refusing to start while it is busy.
  if (command.flag && state.tagEmulationEnabled) {
    Serial.println("[NFC][CAPTURE] Stopping emulation to take the reader");
    (void)executeStopTagEmulation();
  }

  state.captureEnabled = command.flag;
  lastCaptureUid = String();
  lastCaptureAt = 0;
  lastCapturePoll = 0;
  captureMisses = 0;

  if (command.flag) {
    state.captureMessage =
        "Escaneo automático encendido. Cada tag que se lea va al registro NFC.";
    state.message = state.captureMessage;
  } else {
    state.captureMessage = "Escaneo automático apagado.";
    state.message = "Lector listo. Elige una acción y acerca un tag.";
  }
  state.status = "idle";
  state.updatedAt = millis();
  Serial.printf("[NFC][CAPTURE] %s\r\n", command.flag ? "enabled" : "disabled");
  return true;
}

// Queues one decoded payload for the loop task. Dropping on a full queue is
// deliberate: the board is the slow end, and a burst of tags should not stall
// the reader or grow memory without bound.
void stageCapturedTag(const uint8_t *uid, uint8_t uidLength,
                      const String &tagType, const String &content) {
  if (!nfcCaptureQueue) return;

  NfcCapturedTag tag = {};
  tag.uidLength = min<uint8_t>(uidLength, sizeof(tag.uid));
  memcpy(tag.uid, uid, tag.uidLength);
  strncpy(tag.tagType, tagType.c_str(), sizeof(tag.tagType) - 1);
  strncpy(tag.content, content.c_str(), sizeof(tag.content) - 1);

  if (xQueueSend(nfcCaptureQueue, &tag, 0) != pdTRUE) {
    Serial.println("[NFC][CAPTURE] Queue full; dropped one capture");
    return;
  }
  Serial.printf("[NFC][CAPTURE] Staged %s (%s)\r\n",
                uidToString(uid, uidLength).c_str(), tag.tagType);
}

// Runs a read against a tag that capture mode found on its own. Reuses the
// operator read path so decoding, tag typing and error text stay identical.
void captureDetectedTag(uint8_t *uid, uint8_t uidLength) {
  const String seenUid = uidToString(uid, uidLength);
  const uint32_t now = millis();

  if (seenUid == lastCaptureUid && lastCaptureAt != 0 &&
      now - lastCaptureAt < CAPTURE_REPEAT_MS) {
    return;
  }

  pendingOperation = NfcOperation::READ;
  processDetectedTag(uid, uidLength);
  pendingOperation = NfcOperation::NONE;

  lastCaptureUid = seenUid;
  lastCaptureAt = millis();

  // Every meeting goes to the unified NFC log, identity and content together.
  // Decoded records (Text, URL, an NDEF message off a Classic card) carry their
  // payload as content; a card that only gave up its UID is still worth a row --
  // its UID and type stand on their own, with empty content.
  const bool hasContent =
      (state.recordType == "Text" || state.recordType == "URL" ||
       state.recordType == "NDEF" || state.recordType == "MIFARE Classic") &&
      state.payload.length() > 0;
  const String content = hasContent ? state.payload : String();

  stageCapturedTag(uid, uidLength, state.tagType, content);
  ++state.captureCount;
  state.captureMessage = hasContent
                             ? "Tag guardado en el registro NFC."
                             : "Tag sin datos; su UID quedó en el registro NFC.";
  state.updatedAt = millis();
}

// The self-driven half of the worker: no deadline, no busy flag, and it yields
// the moment an operator-initiated action is queued.
void serviceCapture() {
  const uint32_t now = millis();
  if (now - lastCapturePoll < CAPTURE_POLL_INTERVAL_MS) return;
  lastCapturePoll = now;

  uint8_t uid[7] = {0};
  uint8_t uidLength = 0;
  if (!nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength,
                               TAG_POLL_TIMEOUT_MS)) {
    // Tag gone. Clearing the last-seen UID means presenting the same tag again
    // posts again straight away, while a tag left sitting on the antenna still
    // cannot spam: it never produces an empty poll, so the timed guard holds.
    if (++captureMisses >= CAPTURE_CLEAR_MISSES) lastCaptureUid = String();
    return;
  }
  captureMisses = 0;

  captureDetectedTag(uid, uidLength);
}

void executeNfcCommand(const NfcCommand &command) {
  switch (command.type) {
    case NfcCommandType::READ:
      (void)executeQueueNfcRead();
      break;
    case NfcCommandType::WRITE_TEXT:
    case NfcCommandType::WRITE_URL:
      (void)executeQueueNfcWrite(command);
      break;
    case NfcCommandType::START_TEXT_EMULATION:
    case NfcCommandType::START_URL_EMULATION:
      (void)executeStartTagEmulation(command);
      break;
    case NfcCommandType::START_WIFI_ONBOARDING:
      (void)executeStartWifiOnboarding(command);
      break;
    case NfcCommandType::STOP_EMULATION:
      (void)executeStopTagEmulation();
      break;
    case NfcCommandType::SET_CAPTURE:
      (void)executeSetCapture(command);
      break;
  }
}

void serviceNfcWorker() {
  if (!state.readerReady) return;

  if (state.tagEmulationEnabled) {
    serviceTagEmulation();
    return;
  }

  if (pendingOperation == NfcOperation::NONE) {
    // An operator action always wins; capture only runs in the gaps.
    if (state.captureEnabled) serviceCapture();
    return;
  }

  const uint32_t now = millis();
  if (static_cast<int32_t>(now - operationDeadline) >= 0) {
    Serial.printf("[NFC][TIMEOUT] %s expired without detecting a tag\r\n",
                  operationName(pendingOperation));
    finishError("No se detectó ningún tag en 15 segundos.");
    return;
  }
  if (now - lastPoll < POLL_INTERVAL_MS) return;
  lastPoll = now;

  uint8_t uid[7] = {0};
  uint8_t uidLength = 0;
  ++pollAttempts;
  if (!nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength,
                               TAG_POLL_TIMEOUT_MS)) {
#if NFC_DEBUG_VERBOSE
    const uint32_t afterPoll = millis();
    if (lastPollProgress == 0 ||
        afterPoll - lastPollProgress >= POLL_PROGRESS_INTERVAL_MS) {
      lastPollProgress = afterPoll;
      const uint32_t remaining =
          static_cast<int32_t>(operationDeadline - afterPoll) > 0
              ? operationDeadline - afterPoll
              : 0;
      Serial.printf("[NFC][POLL] No target yet; attempts=%lu remaining=%lu ms\r\n",
                    static_cast<unsigned long>(pollAttempts),
                    static_cast<unsigned long>(remaining));
    }
#endif
    return;
  }

#if NFC_DEBUG_VERBOSE
  Serial.printf("[NFC][POLL] Target activated after %lu attempt(s); UID length=%u\r\n",
                static_cast<unsigned long>(pollAttempts), uidLength);
#endif
  state.status = "working";
  state.message = "Tag detectado. Procesando…";
  state.updatedAt = millis();
  processDetectedTag(uid, uidLength);
}

void nfcWorkerTask(void *parameter) {
  (void)parameter;
  Serial.printf("[NFC][TASK] Worker started on core %d\r\n", xPortGetCoreID());

  for (;;) {
    NfcCommand command;
    if (nfcCommandQueue &&
        xQueueReceive(nfcCommandQueue, &command, 0) == pdTRUE) {
      markCommandDequeued();
      executeNfcCommand(command);
      publishNfcState();
    }

    serviceNfcWorker();
    publishNfcState();
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

String buildNfcStateJson(const NfcState &snapshot) {
  String json;
  json.reserve(960 + snapshot.payload.length() + snapshot.raw.length() +
               snapshot.emulatedPayload.length() +
               snapshot.emulationMessage.length() +
               snapshot.captureMessage.length());
  json += F("{\"readerReady\":");
  json += snapshot.readerReady ? F("true") : F("false");
  json += F(",\"busy\":");
  json += snapshot.busy ? F("true") : F("false");
  json += F(",\"writable\":");
  json += snapshot.writable ? F("true") : F("false");
  json += F(",\"status\":\"");
  json += jsonEscape(snapshot.status);
  json += F("\",\"message\":\"");
  json += jsonEscape(snapshot.message);
  json += F("\",\"uid\":\"");
  json += jsonEscape(snapshot.uid);
  json += F("\",\"tagType\":\"");
  json += jsonEscape(snapshot.tagType);
  json += F("\",\"recordType\":\"");
  json += jsonEscape(snapshot.recordType);
  json += F("\",\"payload\":\"");
  json += jsonEscape(snapshot.payload);
  json += F("\",\"raw\":\"");
  json += jsonEscape(snapshot.raw);
  json += F("\",\"capacity\":");
  json += snapshot.capacity;
  json += F(",\"tagEmulationEnabled\":");
  json += snapshot.tagEmulationEnabled ? F("true") : F("false");
  json += F(",\"wifiOnboardingActive\":");
  json += snapshot.wifiOnboardingActive ? F("true") : F("false");
  json += F(",\"emulationReaderConnected\":");
  json += snapshot.emulationReaderConnected ? F("true") : F("false");
  json += F(",\"emulatedRecordType\":\"");
  json += jsonEscape(snapshot.emulatedRecordType);
  json += F("\",\"emulatedPayload\":\"");
  json += jsonEscape(snapshot.emulatedPayload);
  json += F("\",\"emulationMessage\":\"");
  json += jsonEscape(snapshot.emulationMessage);
  json += F("\",\"captureEnabled\":");
  json += snapshot.captureEnabled ? F("true") : F("false");
  json += F(",\"captureCount\":");
  json += snapshot.captureCount;
  json += F(",\"captureMessage\":\"");
  json += jsonEscape(snapshot.captureMessage);
  json += F("\",\"tagScans\":");
  json += snapshot.tagScans;
  json += F(",\"lastTagScanAt\":");
  json += snapshot.lastTagScanAt;
  json += F(",\"updatedAt\":");
  json += snapshot.updatedAt;
  json += '}';
  return json;
}

}  // namespace

void setupNFC() {
  nfcStateMutex = xSemaphoreCreateMutex();
  nfcCommandQueue =
      xQueueCreate(NFC_COMMAND_QUEUE_DEPTH, sizeof(NfcCommand));
  nfcCaptureQueue =
      xQueueCreate(CAPTURE_QUEUE_DEPTH, sizeof(NfcCapturedTag));

  if (!nfcStateMutex || !nfcCommandQueue || !nfcCaptureQueue) {
    state.readerReady = false;
    state.status = "offline";
        state.message = "No arrancó la sincronización NFC.";
    state.updatedAt = millis();
    publishedState = state;
    Serial.println("[NFC] ERROR: Could not create worker queue or state mutex");
    return;
  }

  Serial.println("[NFC] Configuring PN532 interface");
  pinMode(SEL0, OUTPUT);
  pinMode(SEL1, OUTPUT);
  digitalWrite(SEL0, HIGH);
  digitalWrite(SEL1, LOW);
  if (RSTPD_N >= 0) {
    pinMode(RSTPD_N, OUTPUT);
    digitalWrite(RSTPD_N, HIGH);
  }
  delay(100);

  SPI.begin(PN532_SCK, PN532_MISO, PN532_MOSI, PN532_SS);

  if (!nfc.begin()) {
    state.readerReady = false;
    state.status = "offline";
    state.message = "Falló el inicio del PN532.";
    state.updatedAt = millis();
    publishNfcState();
    Serial.println("[NFC] ERROR: nfc.begin() failed");
    return;
  }

  const uint32_t version = nfc.getFirmwareVersion();
  if (!version) {
    state.readerReady = false;
    state.status = "offline";
    state.message = "No se encontró el PN532. Revisa corriente, SPI y cables.";
    state.updatedAt = millis();
    publishNfcState();
    Serial.println("[NFC] ERROR: PN532 not found");
    return;
  }

  if (!configurePassiveReader("startup")) {
    publishNfcState();
    return;
  }

  state.readerReady = true;
  state.status = "idle";
  state.message = "Lector listo. Elige una acción y acerca un tag.";
  state.updatedAt = millis();
  publishNfcState();

  const BaseType_t taskCreated = xTaskCreatePinnedToCore(
      nfcWorkerTask,
      "nfc-worker",
      NFC_TASK_STACK_SIZE,
      nullptr,
      NFC_TASK_PRIORITY,
      &nfcTaskHandle,
      NFC_TASK_CORE);

  if (taskCreated != pdPASS) {
    state.readerReady = false;
    state.status = "offline";
    state.message = "No arrancó la tarea NFC.";
    state.updatedAt = millis();
    publishNfcState();
    Serial.println("[NFC] ERROR: Could not create dedicated NFC task");
    return;
  }

  Serial.printf("[NFC] Found PN5%02X firmware %u.%u\r\n", (version >> 24) & 0xFF,
                (version >> 16) & 0xFF, (version >> 8) & 0xFF);
  Serial.printf("[NFC] Advanced NFC serial diagnostics: %s\r\n",
                NFC_DEBUG_VERBOSE ? "enabled" : "disabled");
}

String getNfcStateJson() {
  return buildNfcStateJson(getPublishedStateSnapshot());
}

NfcTuiState getNfcTuiState() {
  const NfcState source = getPublishedStateSnapshot();
  NfcTuiState state;
  state.readerReady = source.readerReady;
  state.busy = source.busy;
  state.captureEnabled = source.captureEnabled;
  state.emulating = source.tagEmulationEnabled;
  state.wifiOnboarding = source.wifiOnboardingActive;
  state.captureCount = source.captureCount;
  state.tagScans = source.tagScans;
  state.status = source.status;
  state.message = source.message;
  state.payload = source.payload;
  state.emulatedRecordType = source.emulatedRecordType;
  state.emulatedPayload = source.emulatedPayload;
  return state;
}

bool setNfcCaptureEnabled(bool enabled) {
  if (!nfcStateMutex ||
      xSemaphoreTake(nfcStateMutex, portMAX_DELAY) != pdTRUE) {
    return false;
  }

  if (!publishedState.readerReady) {
    setPublishedErrorLocked("El lector PN532 no está disponible.");
    xSemaphoreGive(nfcStateMutex);
    return false;
  }
  NfcCommand command;
  command.type = NfcCommandType::SET_CAPTURE;
  command.flag = enabled;

  if (!sendCommandWhileLocked(command)) {
    setPublishedErrorLocked("La cola de acciones NFC está llena.");
    xSemaphoreGive(nfcStateMutex);
    return false;
  }

  xSemaphoreGive(nfcStateMutex);
  return true;
}

bool isNfcCaptureEnabled() {
  return getPublishedStateSnapshot().captureEnabled;
}

bool takeNfcCapture(NfcCapturedTag &tag) {
  if (!nfcCaptureQueue) return false;
  return xQueueReceive(nfcCaptureQueue, &tag, 0) == pdTRUE;
}

void armNfcPersistence() {
  nfcPersistenceArmed = true;
  // Seed the observation from what the boot restore actually produced, so the
  // first service pass has nothing to write.
  if (nfcStateMutex && xSemaphoreTake(nfcStateMutex, portMAX_DELAY) == pdTRUE) {
    haveObservedNfcSettings = nfcSettingsSnapshotLocked(observedNfcSettings);
    xSemaphoreGive(nfcStateMutex);
  }
  observedNfcSettingsAt = millis();
}

void serviceNfcPersistence() {
  if (!nfcPersistenceArmed) return;

  // Nothing here needs to run at loop speed, and it used to: a quarter second
  // is far finer than the 1.5s settle below and costs nothing.
  const uint32_t now = millis();
  if (now - lastNfcPersistencePoll < NFC_SETTINGS_POLL_MS) return;
  lastNfcPersistencePoll = now;

  StoredNfcSettings desired = {};
  bool readerReady = false;
  if (nfcStateMutex && xSemaphoreTake(nfcStateMutex, 0) == pdTRUE) {
    // A badge whose PN532 is missing or failed its startup check reports every
    // mode as stopped. Saving that would quietly turn a stored Wi-Fi or Text
    // record into "off" for every future boot, so a reader that is not there
    // gets no say in what is stored.
    readerReady = nfcSettingsSnapshotLocked(desired);
    xSemaphoreGive(nfcStateMutex);
  }
  if (!readerReady) return;

  if (!haveObservedNfcSettings || !sameNfcSettings(desired, observedNfcSettings)) {
    observedNfcSettings = desired;
    observedNfcSettingsAt = millis();
    haveObservedNfcSettings = true;
    return;
  }

  if (observedNfcSettingsAt == 0) return;
  if (static_cast<int32_t>(millis() - observedNfcSettingsAt) <
      static_cast<int32_t>(NFC_SETTINGS_SAVE_DELAY_MS)) {
    return;
  }

  observedNfcSettingsAt = 0;  // One write per settled change.
  if (saveNfcSettings(desired)) Serial.println("[NFC] Settings saved");
}

void noteNfcCapturePosted() {
  if (!nfcStateMutex ||
      xSemaphoreTake(nfcStateMutex, portMAX_DELAY) != pdTRUE) {
    return;
  }
  // The worker owns state.captureMessage and overwrites publishedState on its
  // next publish, so this only brightens the page between two polls. That is
  // enough: the count itself is authoritative on the worker side.
  publishedState.captureMessage = "Tag guardado en el registro NFC.";
  publishedState.updatedAt = millis();
  xSemaphoreGive(nfcStateMutex);
}

bool queueNfcRead() {
  if (!nfcStateMutex ||
      xSemaphoreTake(nfcStateMutex, portMAX_DELAY) != pdTRUE) {
    return false;
  }

  if (!publishedState.readerReady) {
    setPublishedErrorLocked("El lector PN532 no está disponible.");
    xSemaphoreGive(nfcStateMutex);
    return false;
  }
  if (publishedState.tagEmulationEnabled) {
    setPublishedErrorLocked(
        "Para la emulación antes de leer o escribir otro tag.");
    xSemaphoreGive(nfcStateMutex);
    return false;
  }
  if (publishedState.busy || commandPending) {
    setPublishedErrorLocked(
        "Ya hay otra acción NFC esperando un tag.");
    xSemaphoreGive(nfcStateMutex);
    return false;
  }

  const NfcState previous = publishedState;
  NfcCommand command;
  command.type = NfcCommandType::READ;

  publishedState.busy = true;
  publishedState.status = "queued";
  publishedState.message =
      "Acerca un tag NFC al lector del PCB para leerlo.";
  publishedState.updatedAt = millis();

  if (!sendCommandWhileLocked(command)) {
    publishedState = previous;
    setPublishedErrorLocked("La cola de acciones NFC está llena.");
    xSemaphoreGive(nfcStateMutex);
    return false;
  }

  xSemaphoreGive(nfcStateMutex);
  return true;
}

bool queueNfcWrite(const String &recordType, const String &payload) {
  String normalized = recordType;
  normalized.toLowerCase();
  if (normalized != "text" && normalized != "url") {
    setPublishedError("El tipo debe ser texto o URL.");
    return false;
  }
  if (payload.length() == 0 || payload.length() > MAX_WEB_PAYLOAD_BYTES) {
    setPublishedError(
        payload.length() == 0
            ? "Escribe texto o una URL antes de guardar."
            : "El contenido pasa el límite de 700 bytes.");
    return false;
  }

  if (!nfcStateMutex ||
      xSemaphoreTake(nfcStateMutex, portMAX_DELAY) != pdTRUE) {
    return false;
  }

  if (!publishedState.readerReady) {
    setPublishedErrorLocked("El lector PN532 no está disponible.");
    xSemaphoreGive(nfcStateMutex);
    return false;
  }
  if (publishedState.tagEmulationEnabled) {
    setPublishedErrorLocked(
        "Para la emulación antes de leer o escribir otro tag.");
    xSemaphoreGive(nfcStateMutex);
    return false;
  }
  if (publishedState.busy || commandPending) {
    setPublishedErrorLocked(
        "Ya hay otra acción NFC esperando un tag.");
    xSemaphoreGive(nfcStateMutex);
    return false;
  }

  const NfcState previous = publishedState;
  NfcCommand command;
  command.type = normalized == "url"
                     ? NfcCommandType::WRITE_URL
                     : NfcCommandType::WRITE_TEXT;
  if (!copyCommandString(payload, command.payload, sizeof(command.payload))) {
    setPublishedErrorLocked("No se pudo poner la acción NFC en cola.");
    xSemaphoreGive(nfcStateMutex);
    return false;
  }

  publishedState.busy = true;
  publishedState.status = "queued";
  publishedState.message =
      normalized == "url"
          ? "Acerca un tag Type 2 para guardar la URL."
          : "Acerca un tag Type 2 para guardar el texto.";
  publishedState.updatedAt = millis();

  if (!sendCommandWhileLocked(command)) {
    publishedState = previous;
    setPublishedErrorLocked("La cola de acciones NFC está llena.");
    xSemaphoreGive(nfcStateMutex);
    return false;
  }

  xSemaphoreGive(nfcStateMutex);
  return true;
}

bool startNfcTagEmulation(const String &recordType, const String &payload) {
  String normalized = recordType;
  normalized.toLowerCase();
  if (normalized != "text" && normalized != "url") {
    setPublishedError("El tipo debe ser texto o URL.");
    return false;
  }
  if (payload.length() == 0 ||
      payload.length() > MAX_TAG_EMULATION_PAYLOAD_BYTES) {
    setPublishedError(
        payload.length() == 0
            ? "Escribe texto o una URL antes de emular."
            : "La emulación acepta hasta 220 bytes.");
    return false;
  }

  if (!nfcStateMutex ||
      xSemaphoreTake(nfcStateMutex, portMAX_DELAY) != pdTRUE) {
    return false;
  }

  if (!publishedState.readerReady) {
    setPublishedErrorLocked("El lector PN532 no está disponible.");
    xSemaphoreGive(nfcStateMutex);
    return false;
  }
  if ((publishedState.busy && !publishedState.tagEmulationEnabled) ||
      commandPending) {
    setPublishedErrorLocked(
        "Espera a que termine la acción del tag.");
    xSemaphoreGive(nfcStateMutex);
    return false;
  }

  const NfcState previous = publishedState;
  NfcCommand command;
  command.type = normalized == "url"
                     ? NfcCommandType::START_URL_EMULATION
                     : NfcCommandType::START_TEXT_EMULATION;
  if (!copyCommandString(payload, command.payload, sizeof(command.payload))) {
    setPublishedErrorLocked("No se pudo poner la emulación en cola.");
    xSemaphoreGive(nfcStateMutex);
    return false;
  }

  publishedState.tagEmulationEnabled = true;
  publishedState.wifiOnboardingActive = false;
  publishedState.emulationReaderConnected = false;
  publishedState.busy = true;
  publishedState.status = "emulating";
  publishedState.emulatedRecordType =
      normalized == "url" ? "URL" : "Text";
  publishedState.emulatedPayload = payload;
    publishedState.emulationMessage =
        "Preparando el registro NDEF…";
  publishedState.message = publishedState.emulationMessage;
  publishedState.updatedAt = millis();

  if (!sendCommandWhileLocked(command)) {
    publishedState = previous;
    setPublishedErrorLocked("La cola de acciones NFC está llena.");
    xSemaphoreGive(nfcStateMutex);
    return false;
  }

  xSemaphoreGive(nfcStateMutex);
  return true;
}

bool startNfcWifiOnboarding(const String &ssid,
                            const String &password,
                            const uint8_t apMac[6]) {
  if (ssid.length() == 0 || ssid.length() >= NFC_WIFI_SSID_BUFFER_SIZE ||
      password.length() >= NFC_WIFI_PASSWORD_BUFFER_SIZE) {
    setPublishedError("Los datos de Wi-Fi no son válidos.");
    return false;
  }

  if (!nfcStateMutex ||
      xSemaphoreTake(nfcStateMutex, portMAX_DELAY) != pdTRUE) {
    return false;
  }

  if (!publishedState.readerReady) {
    setPublishedErrorLocked(
        "El PN532 no está disponible para pasar Wi-Fi.");
    xSemaphoreGive(nfcStateMutex);
    return false;
  }
  if ((publishedState.busy && !publishedState.tagEmulationEnabled) ||
      commandPending) {
    setPublishedErrorLocked(
        "Espera a que termine la acción NFC.");
    xSemaphoreGive(nfcStateMutex);
    return false;
  }

  const NfcState previous = publishedState;
  NfcCommand command;
  command.type = NfcCommandType::START_WIFI_ONBOARDING;
  if (!copyCommandString(ssid, command.ssid, sizeof(command.ssid)) ||
      !copyCommandString(password, command.password,
                         sizeof(command.password))) {
    setPublishedErrorLocked("No se pudo poner Wi-Fi en cola.");
    xSemaphoreGive(nfcStateMutex);
    return false;
  }
  if (apMac) {
    memcpy(command.apMac, apMac, sizeof(command.apMac));
    command.hasApMac = true;
  }

  publishedState.tagEmulationEnabled = true;
  publishedState.wifiOnboardingActive = true;
  publishedState.emulationReaderConnected = false;
  publishedState.busy = true;
  publishedState.status = "emulating";
  publishedState.emulatedRecordType = "Wi-Fi";
  publishedState.emulatedPayload = ssid;
    publishedState.emulationMessage =
        "Preparando los registros de Wi-Fi…";
  publishedState.message = publishedState.emulationMessage;
  publishedState.updatedAt = millis();

  if (!sendCommandWhileLocked(command)) {
    publishedState = previous;
    setPublishedErrorLocked("La cola de acciones NFC está llena.");
    xSemaphoreGive(nfcStateMutex);
    return false;
  }

  xSemaphoreGive(nfcStateMutex);
  return true;
}

bool isNfcWifiOnboardingActive() {
  const NfcState snapshot = getPublishedStateSnapshot();
  return snapshot.tagEmulationEnabled && snapshot.wifiOnboardingActive;
}

bool stopNfcTagEmulation() {
  if (!nfcStateMutex ||
      xSemaphoreTake(nfcStateMutex, portMAX_DELAY) != pdTRUE) {
    return false;
  }

  if (commandPending) {
    setPublishedErrorLocked("Todavía se está poniendo otra acción NFC en cola.");
    xSemaphoreGive(nfcStateMutex);
    return false;
  }

  const NfcState previous = publishedState;
  NfcCommand command;
  command.type = NfcCommandType::STOP_EMULATION;

  publishedState.tagEmulationEnabled = false;
  publishedState.wifiOnboardingActive = false;
  publishedState.emulationReaderConnected = false;
  publishedState.busy = false;
  publishedState.status = "stopping";
  publishedState.emulationMessage = "Parando la emulación…";
  publishedState.message = publishedState.emulationMessage;
  publishedState.updatedAt = millis();

  if (!sendCommandWhileLocked(command)) {
    publishedState = previous;
    setPublishedErrorLocked("La cola de acciones NFC está llena.");
    xSemaphoreGive(nfcStateMutex);
    return false;
  }

  xSemaphoreGive(nfcStateMutex);
  return true;
}
