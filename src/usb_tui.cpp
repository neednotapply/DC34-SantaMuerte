#include "usb_tui.h"

#include <LittleFS.h>
#include <JPEGDEC.h>
#include <esp_system.h>
#include <stdarg.h>

#include "badge_button.h"
#include "badge_led.h"
#include "badge_settings.h"
#include "badge_wifi.h"
#include "board.h"
#include "nfc.h"
#include "usb_hid.h"
#include "usb_network.h"
#include "usb_drive.h"
#include "usb_console.h"

namespace {

constexpr uint8_t LOG_CAPACITY = 28;
constexpr uint8_t LOG_LINE_LENGTH = 96;
constexpr uint32_t REVEAL_MS = 10000;

enum class Screen : uint8_t {
  DASHBOARD,
  NETWORK,
  LED,
  LED_ANIMATIONS,
  LED_BRIGHTNESS,
  LED_COLOUR,
  LED_SPEED,
  NFC,
  NFC_MODE,
  NFC_WRITE,
  NFC_EMULATE,
  OFFERINGS,
  OFFERING_DETAIL,
  SYSTEM,
  USB,
  USB_CONTROLS,
  USB_BUTTON,
  USB_BUTTON_SHORT,
  USB_BUTTON_LONG,
  USB_PROFILE,
  USB_NETWORK,
  PAYLOADS,
  LOGS,
  HELP
};
enum class Prompt : uint8_t { NONE, AP_SSID, AP_PASSWORD, HOME_SSID, HOME_PASSWORD, LED_HEX, LED_BRIGHTNESS, LED_SPEED, NFC_TEXT, NFC_URL, EMU_TEXT, EMU_URL, OFFERING };
enum class Confirm : uint8_t { NONE, SWITCH_AP, SWITCH_HOME, SAVE_AP, TOGGLE_AP_HIDDEN, SAVE_HOME, NFC_WRITE_TEXT, NFC_WRITE_URL, EMU_TEXT, EMU_URL, NFC_WIFI, CLEAR_BOARD, REBOOT, POST_OFFERING, RUN_PAYLOAD, USB_POWER_OFF };

struct LogLine { char module[12]; char text[LOG_LINE_LENGTH]; uint32_t at; };
LogLine logs[LOG_CAPACITY] = {};
uint8_t logHead = 0;
uint8_t logCount = 0;

Screen screen = Screen::DASHBOARD;
// This is a line-oriented serial console, not a full-screen terminal app.
// ANSI colour remains available in the helpers but starts off so it behaves
// cleanly in PuTTY, minicom, screen, a basic serial monitor, or a log file.
bool ansi = false;
bool english = false;
bool rawStream = false;
bool sessionClosed = false;
bool needsRedraw = true;
bool revealSecrets = false;
uint32_t revealUntil = 0;
Prompt prompt = Prompt::NONE;
Confirm confirm = Confirm::NONE;
String input;
String command;
String stagedA;
String stagedB;
String notice;
uint32_t selectedPostId = 0;

const char *tr(const char *spanish, const char *englishText) {
  return english ? englishText : spanish;
}

const char *screenName(Screen value) {
  switch (value) {
    case Screen::DASHBOARD: return tr("ALTAR // DASHBOARD", "ALTAR // DASHBOARD");
    case Screen::NETWORK: return tr("RED", "NETWORK");
    case Screen::LED: return tr("VELAS // HERRAMIENTAS LED", "CANDLES // LED TOOLS");
    case Screen::LED_ANIMATIONS: return tr("LED // ANIMACIONES", "LED // ANIMATIONS");
    case Screen::LED_BRIGHTNESS: return tr("LED // BRILLO", "LED // BRIGHTNESS");
    case Screen::LED_COLOUR: return tr("LED // COLOR", "LED // COLOUR");
    case Screen::LED_SPEED: return tr("LED // VELOCIDAD", "LED // SPEED");
    case Screen::NFC: return tr("NFC // HERRAMIENTAS", "NFC // TOOLS");
    case Screen::NFC_MODE: return tr("NFC // MODO", "NFC // MODE");
    case Screen::NFC_WRITE: return tr("NFC // ESCRIBIR", "NFC // WRITE");
    case Screen::NFC_EMULATE: return tr("NFC // EMULAR", "NFC // EMULATE");
    case Screen::OFFERINGS: return "FIELD NOTES";
    case Screen::OFFERING_DETAIL: return "FIELD NOTE";
    case Screen::SYSTEM: return tr("SISTEMA // SYSTEM", "SYSTEM");
    case Screen::USB: return tr("USB // HERRAMIENTAS", "USB TOOLS");
    case Screen::USB_CONTROLS: return tr("USB // CONTROL DEL HOST", "USB // HOST CONTROLS");
    case Screen::USB_BUTTON: return tr("USB // BOTÓN DEL BADGE", "USB // BADGE BUTTON");
    case Screen::USB_BUTTON_SHORT: return tr("USB // PULSACIÓN", "USB // SHORT PRESS");
    case Screen::USB_BUTTON_LONG: return tr("USB // MANTENER", "USB // LONG PRESS");
    case Screen::USB_PROFILE: return tr("USB // MODO", "USB // MODE");
    case Screen::USB_NETWORK: return tr("USB // RED WI-FI", "USB // WI-FI NETWORK");
    case Screen::PAYLOADS: return tr("USB // SCRIPTS", "USB // SCRIPTS");
    case Screen::LOGS: return tr("DIAGNOSTICOS // LOGS", "DIAGNOSTICS // LOGS");
    case Screen::HELP: return tr("AYUDA // HELP", "HELP");
  }
  return "SANTA MUERTE";
}

void color(const char *code) { if (ansi) Serial.print(code); }
void resetColor() { if (ansi) Serial.print("\x1b[0m"); }
void rowStart() { Serial.write('\r'); }
void tuiLine(const char *value) { rowStart(); Serial.println(value); }
void tuiLine(const String &value) { rowStart(); Serial.println(value); }
// HardwareSerial::printf emits whatever the format string says, and `screen`
// treats a bare LF as "down one row, same column" — which staircases output
// across the width of the terminal. Subsystem logs terminate with CRLF for the
// same reason; prefixing each TUI row with CR additionally guarantees a clean
// column zero even when a half-finished line was already on screen.
void tuiPrintf(const char *format, ...) {
  char buffer[768];
  va_list arguments;
  va_start(arguments, format);
  const int length = vsnprintf(buffer, sizeof(buffer), format, arguments);
  va_end(arguments);
  Serial.write('\r');
  if (length > 0) Serial.write(reinterpret_cast<const uint8_t *>(buffer),
                               min(length, static_cast<int>(sizeof(buffer) - 1)));
}
// A full-screen terminal only needs one control sequence per completed menu
// action. Unlike the old TUI, nothing redraws while the operator is typing.
void clearTerminal() { Serial.print("\x1b[2J\x1b[H"); }
void line() { rowStart(); color("\x1b[38;5;137m"); Serial.println("------------------------------------------------------------"); resetColor(); }
void title(const char *value) { rowStart(); color("\x1b[1;38;5;179m"); Serial.println(value); resetColor(); }
void muted(const String &value) { rowStart(); color("\x1b[38;5;245m"); Serial.println(value); resetColor(); }
void good(const String &value) { rowStart(); color("\x1b[38;5;150m"); Serial.println(value); resetColor(); }
void warn(const String &value) { rowStart(); color("\x1b[38;5;215m"); Serial.println(value); resetColor(); }
void danger(const String &value) { rowStart(); color("\x1b[1;38;5;203m"); Serial.println(value); resetColor(); }

String mask(const char *value) {
  if (!value || !value[0]) return tr("(sin guardar)", "(not saved)");
  String result;
  for (size_t i = 0; value[i] && i < 63; ++i) result += '*';
  return result;
}

String maskedInput() {
  String result;
  for (size_t i = 0; i < input.length(); ++i) result += '*';
  return result;
}

String clipped(const String &value, size_t limit) {
  if (value.length() <= limit) return value;
  return value.substring(0, limit - 2) + "..";
}

// Board text can contain intentional newlines.  Re-emit every logical line
// through tuiLine() so basic serial terminals keep each one at column zero.
void tuiTextLines(const String &value) {
  size_t start = 0;
  while (start <= value.length()) {
    const int end = value.indexOf('\n', start);
    String text = end < 0 ? value.substring(start) : value.substring(start, end);
    if (text.endsWith("\r")) text.remove(text.length() - 1);
    tuiLine(text);
    if (end < 0) return;
    start = static_cast<size_t>(end) + 1;
  }
}

bool findBoardPost(uint32_t id, BoardPost &found) {
  uint32_t cursor = 0;
  BoardPost candidate;
  while (readNextBoardPost(cursor, candidate)) {
    if (candidate.id == id) {
      found = candidate;
      return true;
    }
  }
  return false;
}

bool parseBoardPostId(const String &value, uint32_t &id) {
  // Menu actions are one or two digits.  A post is rendered as 0001, 0002,
  // etc., so a four-or-more digit entry is unambiguously a note lookup.
  if (value.length() < 4) return false;
  for (size_t i = 0; i < value.length(); ++i) {
    if (value[i] < '0' || value[i] > '9') return false;
  }
  char *end = nullptr;
  const unsigned long parsed = strtoul(value.c_str(), &end, 10);
  if (!end || *end || parsed == 0 || parsed > UINT32_MAX) return false;
  id = static_cast<uint32_t>(parsed);
  return true;
}

// A 48-pixel edge yields at most 48 terminal cells across and 24 rows when
// rendered with upper-half blocks.  It keeps a photo legible while avoiding a
// multi-second wall of ANSI escape codes at 115200 baud.
constexpr int TERMINAL_IMAGE_EDGE = 48;
static uint8_t boardImageScratch[BOARD_MAX_IMAGE_BYTES] = {};
static uint16_t terminalImagePixels[TERMINAL_IMAGE_EDGE * TERMINAL_IMAGE_EDGE] = {};
static int terminalImageWidth = 0;
static int terminalImageHeight = 0;
static JPEGDEC terminalImageDecoder;

int drawTerminalImageBlock(JPEGDRAW *draw) {
  if (!draw || !draw->pPixels) return 0;
  for (int y = 0; y < draw->iHeight; ++y) {
    const int targetY = draw->y + y;
    if (targetY < 0 || targetY >= terminalImageHeight) continue;
    for (int x = 0; x < draw->iWidthUsed; ++x) {
      const int targetX = draw->x + x;
      if (targetX < 0 || targetX >= terminalImageWidth) continue;
      terminalImagePixels[targetY * TERMINAL_IMAGE_EDGE + targetX] =
          draw->pPixels[y * draw->iWidth + x];
    }
  }
  return 1;
}

uint8_t rgb565ToXterm256(uint16_t pixel) {
  const uint8_t red = static_cast<uint8_t>(((pixel >> 11) & 0x1F) * 255 / 31);
  const uint8_t green = static_cast<uint8_t>(((pixel >> 5) & 0x3F) * 255 / 63);
  const uint8_t blue = static_cast<uint8_t>((pixel & 0x1F) * 255 / 31);
  const uint8_t high = max(red, max(green, blue));
  const uint8_t low = min(red, min(green, blue));
  if (high - low < 14) {
    const uint8_t gray = static_cast<uint8_t>((red * 30 + green * 59 + blue * 11) / 100);
    if (gray < 8) return 16;
    if (gray > 248) return 231;
    return static_cast<uint8_t>(232 + ((gray - 8) * 24) / 240);
  }
  const uint8_t r = static_cast<uint8_t>((red * 5 + 127) / 255);
  const uint8_t g = static_cast<uint8_t>((green * 5 + 127) / 255);
  const uint8_t b = static_cast<uint8_t>((blue * 5 + 127) / 255);
  return static_cast<uint8_t>(16 + 36 * r + 6 * g + b);
}

void renderBoardImagePreview(uint32_t postId) {
  const size_t length = readBoardImage(postId, boardImageScratch,
                                       sizeof(boardImageScratch));
  if (!length || !terminalImageDecoder.openRAM(boardImageScratch, length,
                                               drawTerminalImageBlock)) {
    muted(tr("No se pudo abrir la imagen de esta nota.",
             "Could not open this note's image."));
    return;
  }

  const int sourceWidth = terminalImageDecoder.getWidth();
  const int sourceHeight = terminalImageDecoder.getHeight();
  const int longestEdge = max(sourceWidth, sourceHeight);
  int divisor = 1;
  int options = 0;
  if (longestEdge > TERMINAL_IMAGE_EDGE * 4) {
    divisor = 8;
    options = JPEG_SCALE_EIGHTH;
  } else if (longestEdge > TERMINAL_IMAGE_EDGE * 2) {
    divisor = 4;
    options = JPEG_SCALE_QUARTER;
  } else if (longestEdge > TERMINAL_IMAGE_EDGE) {
    divisor = 2;
    options = JPEG_SCALE_HALF;
  }
  terminalImageWidth = min(TERMINAL_IMAGE_EDGE,
                           (sourceWidth + divisor - 1) / divisor);
  terminalImageHeight = min(TERMINAL_IMAGE_EDGE,
                            (sourceHeight + divisor - 1) / divisor);
  memset(terminalImagePixels, 0, sizeof(terminalImagePixels));
  terminalImageDecoder.setPixelType(RGB565_LITTLE_ENDIAN);
  const bool decoded = terminalImageDecoder.decode(0, 0, options);
  terminalImageDecoder.close();
  if (!decoded || terminalImageWidth <= 0 || terminalImageHeight <= 0) {
    muted(tr("No se pudo descodificar la imagen de esta nota.",
             "Could not decode this note's image."));
    return;
  }

  tuiPrintf("%s: %dx%d\n", tr("IMAGEN", "IMAGE"), terminalImageWidth,
            terminalImageHeight);
  for (int y = 0; y < terminalImageHeight; y += 2) {
    int foreground = -1;
    int background = -1;
    rowStart();
    for (int x = 0; x < terminalImageWidth; ++x) {
      const int nextForeground = rgb565ToXterm256(
          terminalImagePixels[y * TERMINAL_IMAGE_EDGE + x]);
      const int nextBackground = rgb565ToXterm256(
          terminalImagePixels[min(y + 1, terminalImageHeight - 1) *
                              TERMINAL_IMAGE_EDGE + x]);
      if (nextForeground != foreground || nextBackground != background) {
        Serial.printf("\x1b[38;5;%d;48;5;%dm", nextForeground,
                      nextBackground);
        foreground = nextForeground;
        background = nextBackground;
      }
      Serial.print("\xE2\x96\x80");
    }
    Serial.print("\x1b[0m\r\n");
  }
}

// Spanish is the badge's source language, so the NFC, network and board
// subsystems hand back Spanish status lines and errors no matter which
// language the terminal is in. data/locale.js does the same job for the web
// portal; the English column here matches it word for word so both surfaces
// say the same thing.
struct Phrase { const char *spanish; const char *english; };

const Phrase phrases[] = {
    {"Lector listo. Elige una acción y acerca un tag.", "Reader ready. Pick an action and present a tag."},
    {"Tag detectado. Procesando…", "Tag detected. Processing…"},
    {"Listo para leer", "Ready to read"},
    {"Emulación activa", "Emulating"},
    {"Emulación parada", "Emulation stopped"},
    {"Ofrenda NFC encendida. Cada tag que se lea se va a las ofrendas.", "NFC Offering on. Every tag read joins the offerings."},
    {"Ofrenda NFC apagada.", "NFC Offering off."},
    {"Notas NFC encendidas. Cada tag que se lea va a Field Notes.", "NFC Notes on. Every tag read goes to Field Notes."},
    {"Notas NFC apagadas.", "NFC Notes off."},
    {"Tag guardado en Field Notes.", "Tag saved to Field Notes."},
    {"Tag sin datos. Su UID se guardó en Field Notes.", "Tag had no data. Its UID was saved to Field Notes."},
    {"El PN532 no ha iniciado.", "The PN532 has not started."},
    {"El lector PN532 no está disponible.", "The PN532 reader is unavailable."},
    {"El PN532 no volvió al modo lector.", "The PN532 did not return to reader mode."},
    {"El PN532 no está disponible para pasar Wi-Fi.", "The PN532 is unavailable for Wi-Fi sharing."},
    {"No se encontró el PN532. Revisa corriente, SPI y cables.", "PN532 not found. Check power, SPI and wiring."},
    {"Falló el inicio del PN532.", "The PN532 failed to start."},
    {"Falló la configuración SAM del PN532.", "PN532 SAM configuration failed."},
    {"Falló la configuración de reintentos del PN532.", "PN532 retry configuration failed."},
    {"No arrancó la tarea NFC.", "The NFC task did not start."},
    {"No arrancó la sincronización NFC.", "NFC synchronisation did not start."},
    {"Para la emulación antes de leer o escribir otro tag.", "Stop emulation before reading or writing another tag."},
    {"Ya hay otra acción NFC esperando un tag.", "Another NFC action is already waiting for a tag."},
    {"Espera a que termine la acción NFC.", "Wait for the NFC action to finish."},
    {"Espera a que termine la acción del tag.", "Wait for the tag action to finish."},
    {"Se vio memoria Type 2, pero sin contenedor válido.", "Type 2 memory seen, but no valid container."},
    {"Los ajustes guardados no están disponibles.", "Saved settings are unavailable."},
    {"No se pudo abrir NVS para guardar el ajuste del punto de acceso.", "Could not open NVS to save the access-point setting."},
    {"No se pudo guardar el ajuste del punto de acceso.", "Could not save the access-point setting."},
    {"No se pudo abrir NVS para guardar el idioma.", "Could not open NVS to save the language."},
    {"No se pudo guardar el idioma.", "Could not save the language."},
    {"No se pudo abrir NVS para guardar el Wi-Fi.", "Could not open NVS to save the Wi-Fi."},
    {"La contraseña nueva no se pudo guardar.", "The new password could not be saved."},
    {"El ajuste de SSID oculto no se pudo guardar.", "The hidden-SSID setting could not be saved."},
    {"No se pudo abrir NVS para guardar el Wi-Fi guardado.", "Could not open NVS to save saved Wi-Fi."},
    {"El Wi-Fi guardado no se pudo guardar.", "Saved Wi-Fi could not be saved."},
    {"La contraseña debe tener 8 a 63 caracteres.", "The password must be 8 to 63 characters."},
    {"El SSID guardado debe tener 1 a 32 bytes sin controles.", "The saved Wi-Fi SSID must be 1 to 32 bytes with no control characters."},
    {"Usa ASCII visible. Letras con acento y emoji no caben en la clave WPA2.", "Use visible ASCII. Accents and emoji do not fit in a WPA2 password."},
    {"El tablero no está disponible.", "The board is unavailable."},
    {"Dibuja, escribe o haz las dos.", "Draw, write, or do both."},
    {"No se pudo guardar la ofrenda.", "The offering could not be saved."},
};

String localized(const String &value) {
  if (!english || !value.length()) return value;
  for (const Phrase &phrase : phrases) {
    if (value == phrase.spanish) return phrase.english;
  }
  return value;
}

const char *ledPatternLabel(const char *pattern) {
  if (!pattern) return "—";
  if (!strcmp(pattern, "ofrenda")) return tr("Ofrenda", "Offering");
  if (!strcmp(pattern, "solid")) return tr("Fijo", "Solid");
  if (!strcmp(pattern, "pulse")) return tr("Pulso", "Pulse");
  if (!strcmp(pattern, "aureola")) return tr("Aureola", "Aperture");
  if (!strcmp(pattern, "corona")) return tr("Corona", "Crown");
  if (!strcmp(pattern, "encuentro")) return tr("Encuentro", "Collide");
  if (!strcmp(pattern, "escaner")) return tr("Escáner", "Scanner");
  if (!strcmp(pattern, "chase")) return tr("Carrera", "Chase");
  if (!strcmp(pattern, "manos")) return tr("Manos", "Hands");
  if (!strcmp(pattern, "theater")) return tr("Teatro", "Theater");
  if (!strcmp(pattern, "twinkle")) return tr("Destello", "Twinkle");
  if (!strcmp(pattern, "rainbow")) return tr("Arcoíris", "Rainbow");
  if (!strcmp(pattern, "aurora")) return tr("Ola morada", "Purple Wave");
  if (!strcmp(pattern, "plasma")) return "Plasma";
  if (!strcmp(pattern, "deriva")) return tr("Deriva", "Drift");
  if (!strcmp(pattern, "candle")) return tr("Vela", "Candle");
  if (!strcmp(pattern, "breath")) return tr("Respira", "Breathe");
  if (!strcmp(pattern, "embers")) return tr("Brasas", "Embers");
  if (!strcmp(pattern, "tide")) return tr("Marea", "Tide");
  if (!strcmp(pattern, "vigil")) return tr("Vigilia", "Vigil");
  if (!strcmp(pattern, "comet")) return tr("Cometa", "Comet");
  if (!strcmp(pattern, "rosary")) return tr("Rosario", "Rosary");
  if (!strcmp(pattern, "veil")) return tr("Velo", "Veil");
  if (!strcmp(pattern, "prism")) return tr("Prisma", "Prism");
  if (!strcmp(pattern, "sunset")) return tr("Ocaso", "Sunset");
  if (!strcmp(pattern, "ocean")) return tr("Océano", "Ocean");
  if (!strcmp(pattern, "nebula")) return tr("Nebulosa", "Nebula");
  if (!strcmp(pattern, "orbit")) return tr("Órbita", "Orbit");
  if (!strcmp(pattern, "bloom")) return tr("Florecer", "Bloom");
  if (!strcmp(pattern, "mirage")) return tr("Espejismo", "Mirage");
  if (!strcmp(pattern, "cosmos")) return "Cosmos";
  if (!strcmp(pattern, "off")) return tr("Apagado", "Off");
  return pattern;
}

void appendLog(const char *module, const String &message) {
  LogLine &line = logs[logHead];
  strlcpy(line.module, module ? module : "TUI", sizeof(line.module));
  strlcpy(line.text, message.c_str(), sizeof(line.text));
  line.at = millis();
  logHead = (logHead + 1) % LOG_CAPACITY;
  if (logCount < LOG_CAPACITY) ++logCount;
}

void setNotice(const String &message, bool error = false) {
  // Subsystem errors arrive in Spanish; TUI-owned notices arrive already
  // translated by tr(). localized() is a no-op on the latter.
  notice = localized(message);
  appendLog(error ? "ERROR" : "TUI", notice);
  needsRedraw = true;
}

void header() {
  line();
  title("SANTA MUERTE // USB CONSOLE // 115200");
  tuiPrintf("[%s]\n", screenName(screen));
}

void footer() {
  line();
  muted(tr("Escribe un número y pulsa Enter.  0 Atrás  ? Ayuda  Q cerrar sesión",
           "Type a number, then press Enter.  0 Back  ? Help  Q close session"));
}

void renderDashboard() {
  const WifiTuiState wifi = getWifiTuiState();
  const LedTuiState led = getLedTuiState();
  const NfcTuiState nfc = getNfcTuiState();
  // Nine-wide label column: English "OFFERINGS" is the longest label and
  // overflowed the old eight-wide field, shunting that one row out of line.
  tuiPrintf("%-9s %s\n", tr("RED", "NETWORK"), wifi.accessPointActive ? "Santa Muerte AP" : tr("Wi-Fi guardado", "Saved Wi-Fi"));
  if (wifi.accessPointActive) tuiPrintf("%-9s %s  //  10.69.4.20\n", "SSID", wifi.accessPointSsid.c_str());
  // Print "offline" rather than an empty IP field: a failed join used to render
  // as "SAVED ssid //  // SantaMuerte.local", which reads like a connection
  // still settling instead of one that is not happening. The Network screen
  // already got this right.
  else tuiPrintf("%-9s %s // %s // %s.local\n", tr("GUARDADO", "SAVED"), wifi.homeSsid.c_str(),
                 wifi.homeConnected ? wifi.localIp.c_str() : tr("sin conectar", "offline"),
                 wifi.hostname.c_str());
  tuiPrintf("%-9s %s  RGB %u,%u,%u  B:%u V:%u\n", "LED",
            ledPatternLabel(led.pattern), led.red, led.green, led.blue,
            led.brightness, led.speed);
  tuiPrintf("%-9s %s%s\n", "NFC", nfc.readerReady ? tr("listo", "ready") : tr("PN532 fuera", "PN532 offline"), nfc.emulating ? tr(" / emulando", " / emulating") : nfc.captureEnabled ? tr(" / ofrendando", " / capturing") : "");
  muted(clipped(localized(nfc.message), 64));
  tuiPrintf("%-9s %u / %u %s // %u %s\n", "NOTES", boardStoredCount(),
            boardCapacity(), tr("textos", "texts"), boardImageCapacity(),
            tr("dibujos", "drawings"));
  tuiPrintf("%-9s %u KB %s // %u KB // %lus\n", tr("MEMORIA", "MEMORY"), ESP.getFreeHeap() / 1024, tr("libres", "free"), ESP.getMaxAllocHeap() / 1024, millis() / 1000);
  tuiPrintf("%-9s %s\n", "HID", usbHidStatusLine().c_str());
  Serial.println();
  tuiLine("1 Field Notes");
  tuiLine(tr("2 Herramientas LED", "2 LED Tools"));
  tuiLine(tr("3 Herramientas NFC", "3 NFC Tools"));
  tuiLine(tr("4 Herramientas USB", "4 USB Tools"));
      tuiLine(tr("5 Red", "5 Network"));
  tuiLine(tr("6 Sistema", "6 System"));
  tuiLine(tr("7 Ayuda", "7 Help"));
  tuiLine(english ? "8 Cambiar a Español" : "8 Switch to English");
}

void renderNetwork() {
  const WifiTuiState wifi = getWifiTuiState();
  const String apPassword = getBadgeWifiPassword()[0]
                                ? (revealSecrets ? getBadgeWifiPassword()
                                                 : mask(getBadgeWifiPassword()))
                                : tr("abierta", "open");
  tuiPrintf("%s: %s\n\n", tr("MODO ACTIVO", "ACTIVE MODE"),
            wifi.accessPointActive ? "SANTA MUERTE AP"
                                   : tr("WI-FI GUARDADO", "SAVED WI-FI"));
  tuiPrintf("AP   %s // %s %s // %s\n", wifi.accessPointSsid.c_str(),
            tr("clave", "key"), apPassword.c_str(),
            wifi.hidden ? tr("oculta", "hidden") : tr("visible", "visible"));
  tuiPrintf("%s %s // %s %s // %s\n\n", tr("GUARDADO", "SAVED"),
            wifi.homeConfigured ? wifi.homeSsid.c_str()
                                : tr("sin guardar", "not saved"),
            tr("clave", "key"),
            revealSecrets ? getPersistentStationWifiPassword()
                          : mask(getPersistentStationWifiPassword()).c_str(),
            wifi.homeConnected ? wifi.localIp.c_str()
                               : tr("sin conectar", "offline"));
  tuiLine(tr("1 Editar Wi-Fi del badge (SSID y clave)",
             "1 Edit badge Wi-Fi (SSID and password)"));
  tuiLine(tr("2 Alternar SSID del badge visible/oculto",
             "2 Toggle badge SSID visible/hidden"));
  tuiLine(tr("3 Usar punto de acceso del badge", "3 Use badge access point"));
  tuiLine(tr("4 Editar Wi-Fi guardado y cambiar",
             "4 Edit saved Wi-Fi and switch"));
  tuiLine(tr("5 Usar Wi-Fi guardado", "5 Use saved Wi-Fi"));
  tuiLine(tr("6 Revelar claves por 10 segundos",
             "6 Reveal passwords for 10 seconds"));
}

void renderLed() {
  const LedTuiState led = getLedTuiState();
  tuiPrintf("%s: %s\n", tr("ANIMACIÓN", "ANIMATION"),
            ledPatternLabel(led.pattern));
  tuiPrintf("%s: %u / 255\n", tr("BRILLO", "BRIGHTNESS"), led.brightness);
  tuiPrintf("%s: #%02X%02X%02X\n", tr("COLOR", "COLOUR"), led.red,
            led.green, led.blue);
  tuiPrintf("%s: %u / 100\n\n", tr("VELOCIDAD", "SPEED"), led.speed);
  tuiLine(tr("1 Animación", "1 Animation"));
  tuiLine(tr("2 Brillo", "2 Brightness"));
  tuiLine(tr("3 Color", "3 Colour"));
  tuiLine(tr("4 Velocidad", "4 Speed"));
  tuiLine(tr("5 Identificar LEDs", "5 Identify LEDs"));
}

void renderLedAnimations() {
  const LedTuiState led = getLedTuiState();
  tuiPrintf("%s: %s\n\n", tr("ACTUAL", "CURRENT"),
            ledPatternLabel(led.pattern));
  tuiLine(tr("1 Ofrenda", "1 Offering"));
  tuiLine(tr("2 Sólido", "2 Solid"));
  tuiLine(tr("3 Pulso", "3 Pulse"));
  tuiLine(tr("4 Aureola", "4 Aperture"));
  tuiLine(tr("5 Corona", "5 Crown"));
  tuiLine(tr("6 Encuentro", "6 Collide"));
  tuiLine(tr("7 Escáner", "7 Scanner"));
  tuiLine(tr("8 Carrera", "8 Chase"));
  tuiLine(tr("9 Manos", "9 Hands"));
  tuiLine(tr("10 Teatro", "10 Theater"));
  tuiLine(tr("11 Destello", "11 Twinkle"));
  tuiLine(tr("12 Arcoíris", "12 Rainbow"));
  tuiLine("13 Aurora");
  tuiLine("14 Plasma");
  tuiLine(tr("15 Deriva", "15 Drift"));
  tuiLine(tr("16 Vela", "16 Candle"));
  tuiLine(tr("17 Respira", "17 Breathe"));
  tuiLine(tr("18 Brasas", "18 Embers"));
  tuiLine(tr("19 Marea", "19 Tide"));
  tuiLine(tr("20 Vigilia", "20 Vigil"));
  tuiLine(tr("21 Cometa", "21 Comet"));
  tuiLine(tr("22 Rosario", "22 Rosary"));
  tuiLine(tr("23 Velo", "23 Veil"));
  tuiLine(tr("24 Prisma", "24 Prism"));
  tuiLine(tr("25 Ocaso", "25 Sunset"));
  tuiLine(tr("26 Océano", "26 Ocean"));
  tuiLine(tr("27 Nebulosa", "27 Nebula"));
  tuiLine(tr("28 Órbita", "28 Orbit"));
  tuiLine(tr("29 Florecer", "29 Bloom"));
  tuiLine(tr("30 Espejismo", "30 Mirage"));
  tuiLine("31 Cosmos");
  tuiLine(tr("32 Apagado", "32 Off"));
}

void renderLedBrightness() {
  const LedTuiState led = getLedTuiState();
  tuiPrintf("%s: %u / 255\n\n", tr("BRILLO ACTUAL", "CURRENT BRIGHTNESS"),
            led.brightness);
  tuiLine(tr("1 Asignar brillo nuevo", "1 Set new brightness"));
}

void renderLedColour() {
  const LedTuiState led = getLedTuiState();
  tuiPrintf("%s: #%02X%02X%02X\n\n", tr("COLOR ACTUAL", "CURRENT COLOUR"),
            led.red, led.green, led.blue);
  tuiLine(tr("1 Asignar color hexadecimal", "1 Set hexadecimal colour"));
}

void renderLedSpeed() {
  const LedTuiState led = getLedTuiState();
  tuiPrintf("%s: %u / 100\n\n", tr("VELOCIDAD ACTUAL", "CURRENT SPEED"),
            led.speed);
  tuiLine(tr("1 Asignar velocidad nueva", "1 Set new speed"));
}

void renderNfc() {
  const NfcTuiState nfc = getNfcTuiState();
  const char *mode = nfc.captureEnabled
                         ? tr("Notas NFC", "NFC Notes")
                         : nfc.wifiOnboarding
                               ? tr("Wi-Fi por NFC", "Wi-Fi over NFC")
                               : nfc.emulating ? tr("Emulando tag", "Emulating tag")
                                               : tr("Parado", "Stopped");
  tuiPrintf("%s: %s // %s\n", tr("LECTOR", "READER"),
            nfc.readerReady ? tr("listo", "ready") : tr("PN532 fuera", "PN532 offline"),
            clipped(localized(nfc.message), 48).c_str());
  tuiPrintf("%s: %s\n\n", tr("MODO", "MODE"), mode);
  tuiLine(tr("1 Modo", "1 Mode"));
  tuiLine(tr("2 Leer tag", "2 Read Tag"));
  tuiLine(tr("3 Escribir tag", "3 Write Tag"));
  tuiLine(tr("4 Emular tag", "4 Emulate Tag"));
}

void renderNfcMode() {
  const NfcTuiState nfc = getNfcTuiState();
  String current = nfc.captureEnabled
                       ? tr("Notas NFC", "NFC Notes")
                       : nfc.wifiOnboarding
                             ? tr("Wi-Fi por NFC", "Wi-Fi over NFC")
                             : nfc.emulating
                                   ? String(tr("Emulando ", "Emulating ")) +
                                         (nfc.emulatedRecordType.length()
                                              ? nfc.emulatedRecordType
                                              : tr("tag", "tag"))
                                   : tr("Parado", "Stopped");
  tuiPrintf("%s: %s\n", tr("MODO ACTUAL", "CURRENT MODE"), current.c_str());
  tuiPrintf("%s: %s\n\n", tr("PN532", "PN532"),
            nfc.readerReady ? tr("listo", "ready") : tr("fuera", "offline"));
  tuiLine(tr("1 Notas NFC", "1 NFC Notes"));
  tuiLine(tr("2 Wi-Fi por NFC", "2 Wi-Fi over NFC"));
  tuiLine(tr("3 Emular tag recordado", "3 Emulate remembered tag"));
  tuiLine(tr("4 Parar", "4 Stop"));
}

void renderNfcWrite() {
  tuiLine(tr("1 Escribir texto", "1 Write text"));
  tuiLine(tr("2 Escribir URL", "2 Write URL"));
}

void renderNfcEmulate() {
  const NfcTuiState nfc = getNfcTuiState();
  if (nfc.emulatedPayload.length()) {
    tuiPrintf("%s: %s // %s\n\n", tr("RECORDADO", "REMEMBERED"),
              nfc.emulatedRecordType.c_str(),
              clipped(nfc.emulatedPayload, 42).c_str());
  } else {
    muted(tr("No hay un tag recordado todavía.",
             "There is no remembered tag yet."));
    Serial.println();
  }
  tuiLine(tr("1 Emular texto", "1 Emulate text"));
  tuiLine(tr("2 Emular URL", "2 Emulate URL"));
}

void renderOfferings() {
  tuiPrintf("%u / %u %s // %s %u %s\n\n", boardStoredCount(),
            boardCapacity(), tr("notas guardadas", "stored notes"),
            tr("ring", "ring"), boardImageCapacity(),
            tr("dibujos", "drawings"));
  uint32_t cursor = 0;
  BoardPost post;
  uint8_t shown = 0;
  while (shown < 7 && readNextBoardPost(cursor, post)) {
    tuiPrintf("%04lu  %s%s\n", static_cast<unsigned long>(post.id),
              clipped(post.text.length() ? post.text
                                         : String(tr("[dibujo sin texto]",
                                                     "[drawing, no text]")),
                      46)
                  .c_str(),
              post.hasImage ? tr("  [imagen]", "  [image]") : "");
    ++shown;
  }
  if (!shown) muted(tr("La pared está vacía.", "The wall is empty."));
  Serial.println();
  muted(tr("Escribe un ID de cuatro dígitos para abrir una nota.",
           "Enter a four-digit ID to open a note."));
  tuiLine(tr("1 Nueva nota de texto", "1 New text note"));
  tuiLine(tr("2 Borrar todas las notas", "2 Clear all Field Notes"));
}

void renderOfferingDetail() {
  BoardPost post;
  if (!selectedPostId || !findBoardPost(selectedPostId, post)) {
    muted(tr("Esta nota ya no está guardada.", "This note is no longer stored."));
    return;
  }

  tuiPrintf("ID: %04lu\n", static_cast<unsigned long>(post.id));
  if (post.authorId == NFC_CAPTURE_AUTHOR_ID) {
    tuiLine("SOURCE: NFC");
  } else if (post.authorId == USB_CONSOLE_AUTHOR_ID) {
    tuiLine("SOURCE: USB");
  }
  if (post.text.length()) {
    Serial.println();
    tuiTextLines(post.text);
  } else if (!post.hasImage) {
    muted(tr("[nota vacía]", "[empty note]"));
  }
  if (post.hasImage) {
    Serial.println();
    renderBoardImagePreview(post.id);
  }
}

void renderSystem() {
  const WifiTuiState wifi = getWifiTuiState();
  tuiPrintf("USB 115200 // %s %s // %s %s.local\n", tr("modo", "mode"), wifi.accessPointActive ? "AP" : tr("guardado", "saved"), tr("hostname", "hostname"), wifi.hostname.c_str());
  tuiPrintf("LittleFS %u / %u KB %s\n", static_cast<unsigned>(LittleFS.usedBytes() / 1024), static_cast<unsigned>(LittleFS.totalBytes() / 1024), tr("usados", "used"));
  tuiPrintf("Heap %u KB // %s %u KB\n\n", ESP.getFreeHeap() / 1024, tr("bloque mayor", "largest block"), ESP.getMaxAllocHeap() / 1024);
  tuiLine(tr("1 Diagnósticos capturados", "1 Captured diagnostics"));
  tuiLine(tr("2 Flujo de logs crudos", "2 Raw log stream"));
  tuiLine(tr("3 Mostrar credenciales", "3 Show credentials"));
  tuiLine(tr("4 Reiniciar badge", "4 Reboot badge"));
  tuiLine(tr("5 Cambiar idioma (English)", "5 Switch language (Español)"));
}

void renderLogs() {
  tuiLine(tr("Últimos eventos del USB Console:", "Latest USB Console events:"));
  for (uint8_t i = 0; i < logCount; ++i) {
    const uint8_t index = (logHead + LOG_CAPACITY - logCount + i) % LOG_CAPACITY;
    const LogLine &entry = logs[index];
    tuiPrintf("%6lus %-10s %s\n", static_cast<unsigned long>(entry.at / 1000), entry.module, entry.text);
  }
}

void renderHelp() {
  tuiLine(tr("Elige una sección con su número y pulsa Enter.", "Choose a section by number, then press Enter."));
  tuiLine(tr("Cada sección imprime una lista nueva de acciones numeradas.", "Each section prints a fresh numbered action list."));
  tuiLine(tr("0 vuelve al menú principal. ? muestra esta ayuda.", "0 returns to the main menu. ? shows this help."));
  tuiLine(tr("Desconectar, NFC, borrar y reiniciar piden y/n.", "Network, NFC, erase, and reboot require y/n."));
  tuiLine(tr("USB admite texto; dibujos/fotos y flasheo son del portal.", "USB supports text; drawings/photos and flashing stay on the web."));
}

const char *usbActionName(UsbControlAction action) {
  switch (action) {
    case UsbControlAction::NONE: return tr("Sin acción", "No action");
    case UsbControlAction::LED_CONTROLS: return tr("Controles LED", "LED controls");
    case UsbControlAction::PLAY_PAUSE: return tr("Reproducir / Pausar", "Play / Pause");
    case UsbControlAction::MUTE: return tr("Silenciar", "Mute");
    case UsbControlAction::VOLUME_UP: return tr("Subir volumen", "Volume up");
    case UsbControlAction::VOLUME_DOWN: return tr("Bajar volumen", "Volume down");
    case UsbControlAction::NEXT_TRACK: return tr("Pista siguiente", "Next track");
    case UsbControlAction::PREVIOUS_TRACK: return tr("Pista anterior", "Previous track");
    case UsbControlAction::PRESENT_NEXT: return tr("Presentación siguiente", "Next slide");
    case UsbControlAction::PRESENT_PREVIOUS: return tr("Presentación anterior", "Previous slide");
    case UsbControlAction::SYSTEM_SLEEP: return tr("Suspender equipo", "Sleep computer");
    case UsbControlAction::SYSTEM_WAKE: return tr("Despertar equipo", "Wake computer");
    case UsbControlAction::SYSTEM_POWER_OFF: return tr("Apagar equipo", "Power off computer");
    case UsbControlAction::LIMIT: break;
  }
  return tr("Sin acción", "No action");
}

bool buttonActionForSelection(int selection, UsbControlAction &action) {
  switch (selection) {
    case 1: action = UsbControlAction::LED_CONTROLS; return true;
    case 2: action = UsbControlAction::NONE; return true;
    case 3: action = UsbControlAction::PLAY_PAUSE; return true;
    case 4: action = UsbControlAction::MUTE; return true;
    case 5: action = UsbControlAction::VOLUME_UP; return true;
    case 6: action = UsbControlAction::VOLUME_DOWN; return true;
    case 7: action = UsbControlAction::NEXT_TRACK; return true;
    case 8: action = UsbControlAction::PREVIOUS_TRACK; return true;
    case 9: action = UsbControlAction::PRESENT_NEXT; return true;
    case 10: action = UsbControlAction::PRESENT_PREVIOUS; return true;
    case 11: action = UsbControlAction::SYSTEM_SLEEP; return true;
    case 12: action = UsbControlAction::SYSTEM_WAKE; return true;
    default: return false;
  }
}

void renderUsbTools() {
  tuiPrintf("%-9s %s\n", "HID", usbHidStatusLine().c_str());
  tuiPrintf("%-9s %s\n\n", "SERIAL", tr("activo (CDC)", "available (CDC)"));
  tuiLine(tr("1 Control del host", "1 Host controls"));
  tuiLine(tr("2 Botón del badge", "2 Badge button"));
  tuiLine(tr("3 Scripts", "3 Scripting"));
  tuiLine(tr("4 Modo USB", "4 USB mode"));
  if (getPersistentUsbDeviceProfile() == UsbDeviceProfile::NETWORK) {
    tuiLine(tr("5 Red Wi-Fi USB", "5 USB Wi-Fi network"));
  }
}

void renderUsbProfile() {
  const UsbDeviceProfile profile = getPersistentUsbDeviceProfile();
  const UsbDriveState drive = getUsbDriveState();
  tuiPrintf("%-9s %s\n\n", "ACTIVO",
            profile == UsbDeviceProfile::NETWORK ? tr("Red Wi-Fi", "Wi-Fi network")
                                                 : tr("Unidad Field Notes", "Field Notes Drive"));
  tuiLine(tr("1 Red Wi-Fi: Serial + HID + NCM", "1 Wi-Fi: Serial + HID + NCM"));
  tuiLine(tr("2 Unidad: Serial + HID + almacenamiento solo lectura", "2 Drive: Serial + HID + read-only storage"));
  if (profile == UsbDeviceProfile::DRIVE) {
    tuiPrintf("%-9s %u %s // %u %s\n", "UNIDAD", drive.noteCount,
              tr("notas", "notes"), drive.scriptCount, tr("scripts", "scripts"));
  }
  Serial.println();
  muted(tr("Cambiar el modo guarda la opción y reinicia el badge. Serial sigue disponible.",
           "Changing mode saves it and reboots the badge. Serial remains available."));
}

void renderUsbNetwork() {
  const UsbNetworkState state = getUsbNetworkState();
  tuiPrintf("%-9s %s\n", "NCM", state.available ? tr("instalado", "available")
                                                  : tr("no disponible", "unavailable"));
  tuiPrintf("%-9s %s\n", "MODE", state.enabled ? tr("puente activo", "bridge active")
                                                    : tr("detenido", "stopped"));
  tuiPrintf("%-9s %s\n\n", "LINK", state.linkUp ? tr("Wi-Fi guardado conectado", "saved Wi-Fi connected")
                                                     : tr("Wi-Fi guardado desconectado", "saved Wi-Fi disconnected"));
  tuiLine(tr("1 Iniciar puente Wi-Fi USB", "1 Start USB Wi-Fi bridge"));
  tuiLine(tr("2 Detener puente Wi-Fi USB", "2 Stop USB Wi-Fi bridge"));
  Serial.println();
  muted(tr("Comparte el Wi-Fi guardado con el equipo por NCM.",
           "Shares saved Wi-Fi with the attached computer through NCM."));
  muted(tr("Al iniciarlo, el puente toma el tráfico del Wi-Fi guardado. Deténlo para usar ese enlace normalmente desde el badge.",
           "When started, the bridge owns saved Wi-Fi traffic. Stop it before using that link normally from the badge."));
}

void renderUsbControls() {
  tuiPrintf("%-9s %s\n\n", "HID", usbHidStatusLine().c_str());
  tuiLine(tr("1 Reproducir / Pausar", "1 Play / Pause"));
  tuiLine(tr("2 Pista anterior", "2 Previous track"));
  tuiLine(tr("3 Pista siguiente", "3 Next track"));
  tuiLine(tr("4 Silenciar", "4 Mute"));
  tuiLine(tr("5 Bajar volumen", "5 Volume down"));
  tuiLine(tr("6 Subir volumen", "6 Volume up"));
  tuiLine(tr("7 Presentación anterior", "7 Previous slide"));
  tuiLine(tr("8 Presentación siguiente", "8 Next slide"));
  tuiLine(tr("9 Suspender equipo", "9 Sleep computer"));
  tuiLine(tr("10 Despertar equipo", "10 Wake computer"));
  danger(tr("11 Apagar equipo (pide confirmación)",
            "11 Power off computer (requires confirmation)"));
}

void renderUsbButton() {
  const UsbButtonState state = getUsbButtonState();
  tuiPrintf("%-9s %s\n", tr("PULSAR", "PRESS"), usbActionName(state.shortPress));
  tuiPrintf("%-9s %s\n\n", tr("MANTENER", "HOLD"), usbActionName(state.longPress));
  tuiLine(tr("1 Cambiar pulsación", "1 Change press"));
  tuiLine(tr("2 Cambiar mantener", "2 Change hold"));
}

void renderUsbButtonActions() {
  tuiLine(tr("1 Controles LED actuales", "1 Current LED controls"));
  tuiLine(tr("2 Sin acción", "2 No action"));
  tuiLine(tr("3 Reproducir / Pausar", "3 Play / Pause"));
  tuiLine(tr("4 Silenciar", "4 Mute"));
  tuiLine(tr("5 Subir volumen", "5 Volume up"));
  tuiLine(tr("6 Bajar volumen", "6 Volume down"));
  tuiLine(tr("7 Pista siguiente", "7 Next track"));
  tuiLine(tr("8 Pista anterior", "8 Previous track"));
  tuiLine(tr("9 Presentación siguiente", "9 Next slide"));
  tuiLine(tr("10 Presentación anterior", "10 Previous slide"));
  tuiLine(tr("11 Suspender equipo", "11 Sleep computer"));
  tuiLine(tr("12 Despertar equipo", "12 Wake computer"));
  muted(tr("Apagar equipo no se puede asignar al botón.",
           "Power off cannot be assigned to the badge button."));
}

void renderPayloads() {
  tuiPrintf("%-9s %s\n", tr("ESTADO", "STATUS"), usbHidStatusLine().c_str());
  tuiPrintf("%-9s %s\n", "HOST", usbHidHostSeen() ? tr("visto", "seen") : tr("sin señal", "no signal"));
  const uint8_t leds = usbHidHostLeds();
  String locks;
  if (leds & USB_HID_LED_CAPSLOCK) locks += "CAPS ";
  if (leds & USB_HID_LED_NUMLOCK) locks += "NUM ";
  if (leds & USB_HID_LED_SCROLLLOCK) locks += "SCROLL ";
  if (locks.isEmpty()) locks = tr("ninguno", "none");
  tuiPrintf("%-9s %s\n\n", tr("CANDADOS", "LOCKS"), locks.c_str());

  const uint8_t count = usbHidPayloadCount();
  if (count == 0) {
    muted(tr("Sin scripts. Créales en el portal: http://10.69.4.20/scripting",
             "No scripts. Author them in the portal: http://10.69.4.20/scripting"));
  } else {
    for (uint8_t i = 0; i < count && i < 16; ++i) {
      tuiPrintf("%u %s\n", i + 1, usbHidPayloadNameAt(i).c_str());
    }
  }
  Serial.println();
  muted(tr("17 detiene una carga en curso.", "17 stops a running payload."));
  danger(tr("TECLEA en la computadora conectada. Úsalo solo en la tuya.",
            "This TYPES into the attached computer. Use it only on your own."));
}

void render() {
  if (rawStream) return;
  clearTerminal();
  header();
  switch (screen) {
    case Screen::DASHBOARD: renderDashboard(); break;
    case Screen::NETWORK: renderNetwork(); break;
    case Screen::LED: renderLed(); break;
    case Screen::LED_ANIMATIONS: renderLedAnimations(); break;
    case Screen::LED_BRIGHTNESS: renderLedBrightness(); break;
    case Screen::LED_COLOUR: renderLedColour(); break;
    case Screen::LED_SPEED: renderLedSpeed(); break;
    case Screen::NFC: renderNfc(); break;
    case Screen::NFC_MODE: renderNfcMode(); break;
    case Screen::NFC_WRITE: renderNfcWrite(); break;
    case Screen::NFC_EMULATE: renderNfcEmulate(); break;
    case Screen::OFFERINGS: renderOfferings(); break;
    case Screen::OFFERING_DETAIL: renderOfferingDetail(); break;
    case Screen::SYSTEM: renderSystem(); break;
    case Screen::USB: renderUsbTools(); break;
    case Screen::USB_CONTROLS: renderUsbControls(); break;
    case Screen::USB_BUTTON: renderUsbButton(); break;
    case Screen::USB_BUTTON_SHORT:
    case Screen::USB_BUTTON_LONG: renderUsbButtonActions(); break;
    case Screen::USB_NETWORK: renderUsbNetwork(); break;
    case Screen::USB_PROFILE: renderUsbProfile(); break;
    case Screen::PAYLOADS: renderPayloads(); break;
    case Screen::LOGS: renderLogs(); break;
    case Screen::HELP: renderHelp(); break;
  }
  if (notice.length()) {
    line();
    warn(String(tr("Resultado: ", "Result: ")) + notice);
    notice = String();
  }
  footer();
  rowStart();
  if (confirm != Confirm::NONE) {
    Serial.print(tr("Confirmar [y/n]> ", "Confirm [y/n]> "));
  } else if (prompt != Prompt::NONE) {
    const bool secret = prompt == Prompt::AP_PASSWORD || prompt == Prompt::HOME_PASSWORD;
    Serial.print(tr("Valor> ", "Value> "));
    if (secret) Serial.print(maskedInput()); else Serial.print(input);
  } else {
    Serial.print(tr("Selección> ", "Selection> "));
  }
  needsRedraw = false;
}

void enterRawStream() {
  rawStream = true;
  Serial.println(tr("\r\n[RAW LOGS] Pulsa cualquier tecla para volver al TUI.\r\n",
                    "\r\n[RAW LOGS] Press any key to return to the TUI.\r\n"));
}

void beginPrompt(Prompt next, const String &message) { prompt = next; input = String(); notice = message; needsRedraw = true; }
void beginConfirm(Confirm next, const String &message) { confirm = next; notice = message; needsRedraw = true; }

bool parseHex(const String &value, int &r, int &g, int &b) {
  String text = value; if (text.startsWith("#")) text.remove(0, 1);
  if (text.length() != 6) return false;
  char *end = nullptr; const long number = strtol(text.c_str(), &end, 16);
  if (!end || *end) return false;
  r = (number >> 16) & 0xFF; g = (number >> 8) & 0xFF; b = number & 0xFF; return true;
}

void applyConfirm() {
  String error;
  bool ok = false;
  switch (confirm) {
    case Confirm::SWITCH_AP: ok = setBadgeAccessPointEnabled(true, error); break;
    case Confirm::SWITCH_HOME: ok = setBadgeAccessPointEnabled(false, error); break;
    case Confirm::SAVE_AP:
      ok = setBadgeAccessPointSettings(stagedA, stagedB,
                                       getPersistentWifiHidden(), error);
      break;
    case Confirm::TOGGLE_AP_HIDDEN:
      ok = setBadgeAccessPointSettings(getBadgeWifiSsid(),
                                       getBadgeWifiPassword(),
                                       !getPersistentWifiHidden(), error);
      break;
    case Confirm::SAVE_HOME: ok = setBadgeHomeWifiSettings(stagedA, stagedB, error); break;
    case Confirm::NFC_WRITE_TEXT: ok = queueNfcWrite("text", stagedA); break;
    case Confirm::NFC_WRITE_URL: ok = queueNfcWrite("url", stagedA); break;
    case Confirm::EMU_TEXT: ok = startNfcTagEmulation("text", stagedA); break;
    case Confirm::EMU_URL: ok = startNfcTagEmulation("url", stagedA); break;
    case Confirm::NFC_WIFI: {
      if (!isBadgeAccessPointActive()) {
        error = tr("Activa primero Santa Muerte AP; el Wi-Fi guardado no se comparte por NFC.",
                   "Turn on Santa Muerte AP first; saved Wi-Fi is not shared over NFC.");
        break;
      }
      uint8_t mac[6] = {}; ok = startNfcWifiOnboarding(getBadgeWifiSsid(), getBadgeWifiPassword(), getBadgeWifiApMac(mac) ? mac : nullptr); break;
    }
    case Confirm::CLEAR_BOARD: ok = clearBoard(); break;
    case Confirm::POST_OFFERING:
      ok = addBoardPost(stagedA, 0, nullptr, 0, error, USB_CONSOLE_AUTHOR_ID);
      break;
    case Confirm::RUN_PAYLOAD: ok = usbHidRunPayload(stagedA, error); break;
    case Confirm::USB_POWER_OFF:
      ok = usbHidRunControl(UsbControlAction::SYSTEM_POWER_OFF, error);
      break;
    case Confirm::REBOOT: Serial.println(tr("[TUI] Reiniciando...", "[TUI] Rebooting...")); delay(80); ESP.restart(); return;
    default: break;
  }
  setNotice(ok ? tr("Hecho.", "Done.")
               : error.length() ? error : String(tr("No se pudo completar la acción.", "Could not complete the action.")),
            !ok);
  confirm = Confirm::NONE;
}

void completePrompt() {
  if (prompt == Prompt::AP_SSID) {
    stagedA = input;
    beginPrompt(Prompt::AP_PASSWORD,
                tr("Clave nueva del AP (vacía = red abierta)",
                   "New AP password (empty = open network)"));
  }
  else if (prompt == Prompt::AP_PASSWORD) {
    stagedB = input;
    beginConfirm(Confirm::SAVE_AP,
                 tr("Guardar SSID y clave nuevos del AP",
                    "Save the new AP SSID and password"));
  }
  else if (prompt == Prompt::HOME_SSID) { stagedA = input; beginPrompt(Prompt::HOME_PASSWORD, tr("Clave de Wi-Fi guardado", "Saved Wi-Fi password")); }
  else if (prompt == Prompt::HOME_PASSWORD) { stagedB = input; beginConfirm(Confirm::SAVE_HOME, tr("Guardar y cambiar a Wi-Fi guardado", "Save and switch to saved Wi-Fi")); }
  else if (prompt == Prompt::LED_HEX) { int r, g, b; if (parseHex(input, r, g, b) && setLedTuiState("", r, g, b, -1, -1)) setNotice(tr("Color actualizado.", "Colour updated.")); else setNotice(tr("Usa #RRGGBB.", "Use #RRGGBB."), true); prompt = Prompt::NONE; }
  else if (prompt == Prompt::LED_BRIGHTNESS) { const int value = input.toInt(); if (value >= 0 && value <= 255 && setLedTuiState("", -1, -1, -1, value, -1)) setNotice(tr("Brillo actualizado.", "Brightness updated.")); else setNotice(tr("Brillo: 0 a 255.", "Brightness: 0 to 255."), true); prompt = Prompt::NONE; }
  else if (prompt == Prompt::LED_SPEED) { const int value = input.toInt(); if (value >= 1 && value <= 100 && setLedTuiState("", -1, -1, -1, -1, value)) setNotice(tr("Velocidad actualizada.", "Speed updated.")); else setNotice(tr("Velocidad: 1 a 100.", "Speed: 1 to 100."), true); prompt = Prompt::NONE; }
  else if (prompt == Prompt::NFC_TEXT) { stagedA = input; prompt = Prompt::NONE; beginConfirm(Confirm::NFC_WRITE_TEXT, tr("Escribir texto en el próximo tag", "Write text to the next tag")); }
  else if (prompt == Prompt::NFC_URL) { stagedA = input; prompt = Prompt::NONE; beginConfirm(Confirm::NFC_WRITE_URL, tr("Escribir URL en el próximo tag", "Write a URL to the next tag")); }
  else if (prompt == Prompt::EMU_TEXT) { stagedA = input; prompt = Prompt::NONE; beginConfirm(Confirm::EMU_TEXT, tr("Emular tag de texto", "Emulate a text tag")); }
  else if (prompt == Prompt::EMU_URL) { stagedA = input; prompt = Prompt::NONE; beginConfirm(Confirm::EMU_URL, tr("Emular tag de URL", "Emulate a URL tag")); }
  else if (prompt == Prompt::OFFERING) {
    stagedA = input;
    prompt = Prompt::NONE;
    beginConfirm(Confirm::POST_OFFERING,
                 tr("Guardar esta nota de texto", "Save this text note"));
  }
  needsRedraw = true;
}

void routeGlobal(char key) {
  if (key == '?') { screen = Screen::HELP; needsRedraw = true; return; }
  if (key == 'p' || key == 'P') { ansi = !ansi; needsRedraw = true; return; }
  if (key == 'l' || key == 'L') {
    String error;
    const bool next = !english;
    if (setPersistentEnglishLanguage(next, error)) {
      english = next;
      setNotice(english ? "Language: English" : "Idioma: Español");
    } else {
      setNotice(error, true);
    }
    return;
  }
  if (key == 'r' || key == 'R') { if (rawStream) { rawStream = false; needsRedraw = true; } else enterRawStream(); return; }
  // Capital W is deliberately available everywhere as a recovery shortcut.
  // It stays capital so it can never collide with the lowercase letters the
  // per-screen menus use, and it is not A-D because those are exactly the
  // bytes that terminate an arrow-key sequence.
  if (key == 'W') {
    beginConfirm(isBadgeAccessPointActive() ? Confirm::SWITCH_HOME : Confirm::SWITCH_AP,
                 isBadgeAccessPointActive()
                     ? tr("Cambiar a Wi-Fi guardado (apaga el AP)", "Switch to saved Wi-Fi (turns the AP off)")
                     : tr("Cambiar a Santa Muerte AP (corta Wi-Fi guardado)", "Switch to Santa Muerte AP (drops saved Wi-Fi)"));
  }
}

void handleScreenKey(char key) {
  if (key == 27 || key == 'q' || key == 'Q') { screen = Screen::DASHBOARD; needsRedraw = true; return; }
  if (screen == Screen::NETWORK) {
    const WifiTuiState wifi = getWifiTuiState();
    if (key == '1') beginPrompt(Prompt::AP_SSID,
                                 tr("Nombre nuevo del AP (SSID)",
                                    "New badge AP name (SSID)"));
    else if (key == '2') beginConfirm(Confirm::TOGGLE_AP_HIDDEN, wifi.hidden ? tr("Hacer visible el SSID Santa Muerte", "Make the Santa Muerte SSID visible") : tr("Ocultar el SSID Santa Muerte", "Hide the Santa Muerte SSID"));
    else if (key == '3') beginConfirm(Confirm::SWITCH_AP, tr("Usar el punto de acceso Santa Muerte (corta Wi-Fi guardado)", "Use the Santa Muerte access point (drops saved Wi-Fi)"));
    else if (key == '4') beginPrompt(Prompt::HOME_SSID, tr("SSID de Wi-Fi guardado", "Saved Wi-Fi SSID"));
    else if (key == '5') beginConfirm(Confirm::SWITCH_HOME, tr("Usar el Wi-Fi guardado (apaga el AP)", "Use saved Wi-Fi (turns the AP off)"));
    else if (key == 'v' || key == 'V') { revealSecrets = true; revealUntil = millis() + REVEAL_MS; setNotice(tr("Claves visibles durante 10 segundos.", "Passwords visible for 10 seconds.")); }
  } else if (screen == Screen::LED) {
    const char *patterns[] = {
        "solid","rainbow","chase","pulse","twinkle","theater","aurora","off",
        "ofrenda","corona","aureola","encuentro","manos","escaner","plasma","deriva",
        "candle","breath","embers","tide","vigil","comet","rosary","veil",
        "prism","sunset","ocean","nebula","orbit","bloom","mirage","cosmos"};
    int index = -1;
    if (key >= '1' && key <= '9') index = key - '1';
    else if (key >= 'a' && key <= 'g') index = 9 + key - 'a';
    if (index >= 0 && index < static_cast<int>(sizeof(patterns) / sizeof(patterns[0]))) {
      const bool applied = setLedTuiState(patterns[index], -1, -1, -1, -1, -1);
      setNotice(applied ? tr("Patrón actualizado.", "Pattern updated.") : tr("Patrón inválido.", "Invalid pattern."), !applied);
    }
    else if (key == 'h') beginPrompt(Prompt::LED_HEX, tr("Color hexadecimal", "Hex colour"));
    else if (key == 'i') beginPrompt(Prompt::LED_BRIGHTNESS, tr("Brillo 0-255", "Brightness 0-255"));
    else if (key == 'j') beginPrompt(Prompt::LED_SPEED, tr("Velocidad 1-100", "Speed 1-100"));
    else if (key == 'k') { const LedTuiState led = getLedTuiState(); setLedTuiIdentifyFrame((led.identifyFrame + 1) % 4); setNotice(tr("Marco de identificación cambiado.", "Identify frame changed.")); }
  } else if (screen == Screen::NFC) {
    if (key == '1') setNotice(queueNfcRead() ? tr("Lectura en cola. Acerca un tag.", "Read queued. Present a tag.") : tr("No se pudo iniciar lectura.", "Could not start the read."), false);
    else if (key == '2') beginPrompt(Prompt::NFC_TEXT, tr("Texto para escribir", "Text to write"));
    else if (key == '3') beginPrompt(Prompt::NFC_URL, tr("URL para escribir", "URL to write"));
    else if (key == '4') { const bool next = !isNfcCaptureEnabled(); setNotice(setNfcCaptureEnabled(next) ? next ? tr("NFC Offering encendida.", "NFC Offering on.") : tr("NFC Offering apagada.", "NFC Offering off.") : tr("No se pudo cambiar NFC Offering.", "Could not change NFC Offering."), false); }
    else if (key == '5') beginPrompt(Prompt::EMU_TEXT, tr("Texto para emular", "Text to emulate"));
    else if (key == '6') beginPrompt(Prompt::EMU_URL, tr("URL para emular", "URL to emulate"));
    else if (key == '7') beginConfirm(Confirm::NFC_WIFI, tr("Emular Wi-Fi del badge", "Emulate badge Wi-Fi"));
    else if (key == '8') setNotice(stopNfcTagEmulation() ? tr("Parando emulación.", "Stopping emulation.") : tr("No se pudo parar.", "Could not stop."), false);
  } else if (screen == Screen::OFFERINGS) {
    if (key == '1') beginPrompt(Prompt::OFFERING,
                                 tr("Texto de la nota", "Field Note text"));
    else if (key == '2') beginConfirm(Confirm::CLEAR_BOARD,
                                      tr("Borrar todas las notas", "Clear all Field Notes"));
  } else if (screen == Screen::SYSTEM) {
    if (key == '1') { screen = Screen::LOGS; needsRedraw = true; }
    else if (key == '2') enterRawStream();
    else if (key == '3') { revealSecrets = true; revealUntil = millis() + REVEAL_MS; screen = Screen::NETWORK; setNotice(tr("Claves visibles durante 10 segundos.", "Passwords visible for 10 seconds.")); }
    else if (key == '4') beginConfirm(Confirm::REBOOT, tr("Reiniciar el badge", "Reboot the badge"));
    else if (key == '5') routeGlobal('l');
  } else if (screen == Screen::PAYLOADS) {
    if (key == 's' || key == 'S') { usbHidStop(); setNotice(tr("Detenido.", "Stopped.")); }
    else {
      int index = -1;
      if (key >= '1' && key <= '9') index = key - '1';
      else if (key >= 'a' && key <= 'g') index = 9 + (key - 'a');
      if (index >= 0 && index < static_cast<int>(usbHidPayloadCount())) {
        stagedA = usbHidPayloadNameAt(static_cast<uint8_t>(index));
        beginConfirm(Confirm::RUN_PAYLOAD,
                     String(tr("Ejecutar carga ", "Run payload ")) + stagedA +
                         tr(": TECLEARA en el host conectado", ": it will TYPE into the attached host"));
      }
    }
  }
  needsRedraw = true;
}

void handleDashboardSelection(int selection) {
  switch (selection) {
    case 1: screen = Screen::OFFERINGS; break;
    case 2: screen = Screen::LED; break;
    case 3: screen = Screen::NFC; break;
    case 4: screen = Screen::USB; break;
    case 5: screen = Screen::NETWORK; break;
    case 6: screen = Screen::SYSTEM; break;
    case 7:
    screen = Screen::HELP;
    break;
    case 8:
    routeGlobal('l');
    break;
    default:
      setNotice(tr("Selección inválida.", "Invalid selection."), true);
      break;
  }
  needsRedraw = true;
}

void handleScreenSelection(int selection) {
  if (screen == Screen::DASHBOARD) {
    handleDashboardSelection(selection);
    return;
  }

  if (screen == Screen::LED) {
    if (selection == 1) screen = Screen::LED_ANIMATIONS;
    else if (selection == 2) screen = Screen::LED_BRIGHTNESS;
    else if (selection == 3) screen = Screen::LED_COLOUR;
    else if (selection == 4) screen = Screen::LED_SPEED;
    else if (selection == 5) {
      const LedTuiState led = getLedTuiState();
      setLedTuiIdentifyFrame((led.identifyFrame + 1) % 4);
      setNotice(tr("Marco de identificación cambiado.", "Identify frame changed."));
    } else {
      setNotice(tr("Selección inválida.", "Invalid selection."), true);
    }
    needsRedraw = true;
    return;
  }

  if (screen == Screen::LED_ANIMATIONS) {
    const char *patterns[] = {
        "ofrenda", "solid", "pulse", "aureola", "corona", "encuentro",
        "escaner", "chase", "manos", "theater", "twinkle", "rainbow",
        "aurora", "plasma", "deriva", "candle", "breath", "embers",
        "tide", "vigil", "comet", "rosary", "veil", "prism", "sunset",
        "ocean", "nebula", "orbit", "bloom", "mirage", "cosmos", "off"};
    if (selection >= 1 &&
        selection <= static_cast<int>(sizeof(patterns) / sizeof(patterns[0]))) {
      const bool applied = setLedTuiState(patterns[selection - 1], -1, -1, -1,
                                          -1, -1);
      setNotice(applied ? tr("Patrón actualizado.", "Pattern updated.")
                        : tr("Patrón inválido.", "Invalid pattern."),
                !applied);
    } else {
      setNotice(tr("Selección inválida.", "Invalid selection."), true);
    }
    needsRedraw = true;
    return;
  }

  if (screen == Screen::LED_BRIGHTNESS || screen == Screen::LED_COLOUR ||
      screen == Screen::LED_SPEED) {
    if (selection != 1) {
      setNotice(tr("Selección inválida.", "Invalid selection."), true);
    } else if (screen == Screen::LED_BRIGHTNESS) {
      beginPrompt(Prompt::LED_BRIGHTNESS,
                  tr("Brillo nuevo 0-255", "New brightness 0-255"));
    } else if (screen == Screen::LED_COLOUR) {
      beginPrompt(Prompt::LED_HEX,
                  tr("Color nuevo #RRGGBB", "New colour #RRGGBB"));
    } else {
      beginPrompt(Prompt::LED_SPEED,
                  tr("Velocidad nueva 1-100", "New speed 1-100"));
    }
    needsRedraw = true;
    return;
  }

  if (screen == Screen::NFC) {
    if (selection == 1) {
      screen = Screen::NFC_MODE;
    } else if (selection == 2) {
      const bool queued = queueNfcRead();
      setNotice(queued ? tr("Lectura en cola. Acerca un tag.",
                            "Read queued. Present a tag.")
                       : tr("No se pudo iniciar lectura.",
                            "Could not start the read."),
                !queued);
    } else if (selection == 3) {
      screen = Screen::NFC_WRITE;
    } else if (selection == 4) {
      screen = Screen::NFC_EMULATE;
    } else {
      setNotice(tr("Selección inválida.", "Invalid selection."), true);
    }
    needsRedraw = true;
    return;
  }

  if (screen == Screen::NFC_MODE) {
    const NfcTuiState nfc = getNfcTuiState();
    if (selection == 1) {
      const bool started = setNfcCaptureEnabled(true);
      setNotice(started ? tr("Notas NFC activadas.", "NFC Notes enabled.")
                        : tr("No se pudieron activar las Notas NFC.",
                             "Could not enable NFC Notes."),
                !started);
    } else if (selection == 2) {
      beginConfirm(Confirm::NFC_WIFI,
                   tr("Compartir el Wi-Fi del badge por NFC",
                      "Share the badge Wi-Fi over NFC"));
    } else if (selection == 3) {
      String recordType = nfc.emulatedRecordType;
      recordType.toLowerCase();
      if ((recordType == "text" || recordType == "url") &&
          nfc.emulatedPayload.length()) {
        const bool started =
            startNfcTagEmulation(recordType, nfc.emulatedPayload);
        setNotice(started ? tr("Emulación del tag recordado en cola.",
                               "Remembered-tag emulation queued.")
                          : tr("No se pudo emular el tag recordado.",
                               "Could not emulate the remembered tag."),
                  !started);
      } else {
        setNotice(tr("No hay un tag de texto o URL recordado.",
                     "There is no remembered Text or URL tag."), true);
      }
    } else if (selection == 4) {
      const bool stopped = nfc.captureEnabled ? setNfcCaptureEnabled(false)
                                              : stopNfcTagEmulation();
      setNotice(stopped ? tr("Modo NFC detenido.", "NFC mode stopped.")
                        : tr("No se pudo detener el modo NFC.",
                             "Could not stop the NFC mode."),
                !stopped);
    } else {
      setNotice(tr("Selección inválida.", "Invalid selection."), true);
    }
    needsRedraw = true;
    return;
  }

  if (screen == Screen::NFC_WRITE) {
    if (selection == 1) {
      beginPrompt(Prompt::NFC_TEXT, tr("Texto para escribir", "Text to write"));
    } else if (selection == 2) {
      beginPrompt(Prompt::NFC_URL, tr("URL para escribir", "URL to write"));
    } else {
      setNotice(tr("Selección inválida.", "Invalid selection."), true);
    }
    needsRedraw = true;
    return;
  }

  if (screen == Screen::NFC_EMULATE) {
    if (selection == 1) {
      beginPrompt(Prompt::EMU_TEXT, tr("Texto para emular", "Text to emulate"));
    } else if (selection == 2) {
      beginPrompt(Prompt::EMU_URL, tr("URL para emular", "URL to emulate"));
    } else {
      setNotice(tr("Selección inválida.", "Invalid selection."), true);
    }
    needsRedraw = true;
    return;
  }

  if (screen == Screen::OFFERINGS) {
    if (selection == 1) {
      beginPrompt(Prompt::OFFERING, tr("Texto de la nota", "Field Note text"));
    } else if (selection == 2) {
      beginConfirm(Confirm::CLEAR_BOARD,
                   tr("Borrar todas las notas", "Clear all Field Notes"));
    } else {
      setNotice(tr("Selección inválida.", "Invalid selection."), true);
    }
    needsRedraw = true;
    return;
  }

  if (screen == Screen::USB) {
    if (selection == 1) screen = Screen::USB_CONTROLS;
    else if (selection == 2) screen = Screen::USB_BUTTON;
    else if (selection == 3) screen = Screen::PAYLOADS;
    else if (selection == 4) screen = Screen::USB_PROFILE;
    else if (selection == 5 && getPersistentUsbDeviceProfile() == UsbDeviceProfile::NETWORK) screen = Screen::USB_NETWORK;
    else {
      setNotice(tr("Selección inválida.", "Invalid selection."), true);
    }
    needsRedraw = true;
    return;
  }

  if (screen == Screen::USB_PROFILE) {
    UsbDeviceProfile target;
    if (selection == 1) target = UsbDeviceProfile::NETWORK;
    else if (selection == 2) target = UsbDeviceProfile::DRIVE;
    else {
      setNotice(tr("Selección inválida.", "Invalid selection."), true);
      needsRedraw = true;
      return;
    }
    String error;
    if (!setPersistentUsbDeviceProfile(target, error)) {
      setNotice(error, true);
      needsRedraw = true;
      return;
    }
    tuiLine(tr("Modo USB guardado; reiniciando…", "USB mode saved; restarting…"));
    delay(100);
    ESP.restart();
    return;
  }

  if (screen == Screen::USB_NETWORK) {
    String error;
    bool changed = false;
    if (selection == 1) changed = usbNetworkSetEnabled(true, error);
    else if (selection == 2) changed = usbNetworkSetEnabled(false, error);
    else error = tr("Selección inválida.", "Invalid selection.");
    setNotice(changed ? (selection == 1
                             ? tr("Puente Wi-Fi USB iniciado.",
                                  "USB Wi-Fi bridge started.")
                             : tr("Puente Wi-Fi USB detenido.",
                                  "USB Wi-Fi bridge stopped."))
                      : error,
              !changed);
    needsRedraw = true;
    return;
  }

  if (screen == Screen::USB_CONTROLS) {
    UsbControlAction action = UsbControlAction::NONE;
    switch (selection) {
      case 1: action = UsbControlAction::PLAY_PAUSE; break;
      case 2: action = UsbControlAction::PREVIOUS_TRACK; break;
      case 3: action = UsbControlAction::NEXT_TRACK; break;
      case 4: action = UsbControlAction::MUTE; break;
      case 5: action = UsbControlAction::VOLUME_DOWN; break;
      case 6: action = UsbControlAction::VOLUME_UP; break;
      case 7: action = UsbControlAction::PRESENT_PREVIOUS; break;
      case 8: action = UsbControlAction::PRESENT_NEXT; break;
      case 9: action = UsbControlAction::SYSTEM_SLEEP; break;
      case 10: action = UsbControlAction::SYSTEM_WAKE; break;
      case 11:
        beginConfirm(Confirm::USB_POWER_OFF,
                     tr("Apagar el equipo conectado", "Power off the connected computer"));
        needsRedraw = true;
        return;
      default:
        setNotice(tr("Selección inválida.", "Invalid selection."), true);
        needsRedraw = true;
        return;
    }
    String error;
    const bool sent = usbHidRunControl(action, error);
    setNotice(sent ? String(usbActionName(action)) + tr(" enviado.", " sent.")
                   : error.length() ? error
                                    : String(tr("No se pudo enviar el control USB.",
                                                "Could not send the USB control.")),
              !sent);
    needsRedraw = true;
    return;
  }

  if (screen == Screen::USB_BUTTON) {
    if (selection == 1) screen = Screen::USB_BUTTON_SHORT;
    else if (selection == 2) screen = Screen::USB_BUTTON_LONG;
    else setNotice(tr("Selección inválida.", "Invalid selection."), true);
    needsRedraw = true;
    return;
  }

  if (screen == Screen::USB_BUTTON_SHORT || screen == Screen::USB_BUTTON_LONG) {
    UsbControlAction action = UsbControlAction::NONE;
    if (!buttonActionForSelection(selection, action)) {
      setNotice(tr("Selección inválida.", "Invalid selection."), true);
    } else {
      UsbButtonState state = getUsbButtonState();
      if (screen == Screen::USB_BUTTON_SHORT) state.shortPress = action;
      else state.longPress = action;
      String error;
      const bool saved = setUsbButtonState(state.shortPress, state.longPress,
                                           error);
      setNotice(saved ? tr("Acción del botón guardada.",
                           "Button action saved.")
                      : error.length() ? error
                                       : String(tr("No se pudo guardar la acción del botón.",
                                                   "Could not save the button action.")),
                !saved);
      if (saved) screen = Screen::USB_BUTTON;
    }
    needsRedraw = true;
    return;
  }

  if (screen == Screen::PAYLOADS) {
    if (selection == 17) {
      usbHidStop();
      setNotice(tr("Detenido.", "Stopped."));
    } else if (selection >= 1 &&
               selection <= static_cast<int>(usbHidPayloadCount())) {
      stagedA = usbHidPayloadNameAt(static_cast<uint8_t>(selection - 1));
      beginConfirm(Confirm::RUN_PAYLOAD,
                   String(tr("Ejecutar carga ", "Run payload ")) + stagedA +
                       tr(": TECLEARÁ en el host conectado",
                          ": it will TYPE into the attached host"));
    } else {
      setNotice(tr("Selección inválida.", "Invalid selection."), true);
    }
    needsRedraw = true;
    return;
  }

  if (screen == Screen::NETWORK && selection == 6) {
    revealSecrets = true;
    revealUntil = millis() + REVEAL_MS;
    setNotice(tr("Claves visibles durante 10 segundos.",
                 "Passwords visible for 10 seconds."));
  } else if (selection >= 1 && selection <= 9) {
    // The remaining sections already use one-digit numbered actions.
    handleScreenKey(static_cast<char>('0' + selection));
    return;
  } else {
    setNotice(tr("Selección inválida.", "Invalid selection."), true);
  }
  needsRedraw = true;
}

bool parseSelection(const String &value, int &selection) {
  if (!value.length()) return false;
  char *end = nullptr;
  const long parsed = strtol(value.c_str(), &end, 10);
  if (!end || *end || parsed < 0 || parsed > 99) return false;
  selection = static_cast<int>(parsed);
  return true;
}

void handleCommand(const String &value) {
  String commandText = value;
  commandText.trim();
  if (!commandText.length()) return;

  String lower = commandText;
  lower.toLowerCase();
  if (confirm != Confirm::NONE) {
    if (lower == "y" || lower == "yes") applyConfirm();
    else if (lower == "n" || lower == "no") {
      confirm = Confirm::NONE;
      setNotice(tr("Cancelado.", "Cancelled."));
    } else {
      setNotice(tr("Responde y o n.", "Answer y or n."), true);
    }
    return;
  }

  if (lower == "?" || lower == "help") {
    screen = Screen::HELP;
    needsRedraw = true;
    return;
  }
  if (lower == "0" || lower == "m" || lower == "menu" || lower == "back") {
    if (screen == Screen::LED_ANIMATIONS ||
        screen == Screen::LED_BRIGHTNESS || screen == Screen::LED_COLOUR ||
        screen == Screen::LED_SPEED) {
      screen = Screen::LED;
    } else if (screen == Screen::NFC_MODE || screen == Screen::NFC_WRITE ||
               screen == Screen::NFC_EMULATE) {
      screen = Screen::NFC;
    } else if (screen == Screen::OFFERING_DETAIL) {
      screen = Screen::OFFERINGS;
    } else if (screen == Screen::USB_BUTTON_SHORT ||
               screen == Screen::USB_BUTTON_LONG) {
      screen = Screen::USB_BUTTON;
    } else if (screen == Screen::USB_CONTROLS ||
               screen == Screen::USB_BUTTON || screen == Screen::USB_PROFILE ||
               screen == Screen::USB_NETWORK ||
               screen == Screen::PAYLOADS) {
      screen = Screen::USB;
    } else {
      screen = Screen::DASHBOARD;
    }
    needsRedraw = true;
    return;
  }

  if (screen == Screen::OFFERINGS) {
    uint32_t postId = 0;
    if (parseBoardPostId(commandText, postId)) {
      BoardPost post;
      if (findBoardPost(postId, post)) {
        selectedPostId = postId;
        screen = Screen::OFFERING_DETAIL;
      } else {
        setNotice(tr("No se encontró esa nota.", "That Field Note was not found."),
                  true);
      }
      needsRedraw = true;
      return;
    }
  }

  int selection = 0;
  if (!parseSelection(commandText, selection)) {
    setNotice(tr("Escribe el número de una acción.",
                 "Enter an action number."), true);
    return;
  }
  handleScreenSelection(selection);
}

void handleKey(char key) {
  if (sessionClosed) {
    if (key == '\r' || key == '\n') {
      sessionClosed = false;
      needsRedraw = true;
    }
    return;
  }
  if (rawStream) { rawStream = false; needsRedraw = true; return; }
  if ((key == 'q' || key == 'Q') && prompt == Prompt::NONE &&
      confirm == Confirm::NONE) {
    command = String();
    clearTerminal();
    muted(tr("Sesión del badge cerrada. Pulsa Enter para abrirla otra vez.",
             "Badge session closed. Press Enter to open it again."));
    sessionClosed = true;
    needsRedraw = false;
    return;
  }
  if (key == 27) {
    prompt = Prompt::NONE;
    confirm = Confirm::NONE;
    input = String();
    command = String();
    screen = Screen::DASHBOARD;
    setNotice(tr("Cancelado.", "Cancelled."));
    return;
  }
  if (prompt != Prompt::NONE) {
    if (key == '\r' || key == '\n') {
      if (input.length() || prompt == Prompt::AP_PASSWORD) completePrompt();
      else setNotice(tr("Escribe un valor.", "Enter a value."), true);
      return;
    }
    if (key == 8 || key == 127) {
      if (input.length()) {
        input.remove(input.length() - 1);
        Serial.print("\b \b");
      }
      return;
    }
    if (key >= 32 && key <= 126 && input.length() < 700) {
      input += key;
      const bool secret = prompt == Prompt::AP_PASSWORD || prompt == Prompt::HOME_PASSWORD;
      Serial.print(secret ? '*' : key);
    }
    return;
  }

  if (key == '\r' || key == '\n') {
    if (command.length()) {
      Serial.println();
      handleCommand(command);
      command = String();
    } else {
      // A terminal may attach after boot and miss the initial menu. Pressing
      // Enter is a familiar, harmless way to ask a line-oriented program to
      // print its prompt again.
      needsRedraw = true;
    }
    return;
  }
  if (key == 8 || key == 127) {
    if (command.length()) {
      command.remove(command.length() - 1);
      Serial.print("\b \b");
    }
    return;
  }
  if (key >= 32 && key <= 126 && command.length() < 32) {
    command += key;
    Serial.write(key);
  }
}

}  // namespace

void usbTuiBegin() {
  english = getPersistentEnglishLanguage();
  appendLog("TUI", tr("USB Altar listo", "USB Altar ready"));
  needsRedraw = true;
}

void usbTuiLog(const char *module, const String &message) {
  appendLog(module, message);
  if (rawStream) tuiPrintf("[%s] %s\n", module ? module : "LOG", message.c_str());
}

void usbTuiRefresh() {
  if (!rawStream) needsRedraw = true;
}

void usbTuiService() {
  while (Serial.available()) handleKey(static_cast<char>(Serial.read()));
  // The web portal can change the language too, and it writes the setting
  // directly. usbTuiBegin() only reads it once, so without this the TUI keeps
  // rendering in whatever language it booted with until the badge reboots.
  // The getter returns a cached value, so this costs nothing to poll.
  const bool storedEnglish = getPersistentEnglishLanguage();
  if (storedEnglish != english) {
    english = storedEnglish;
    needsRedraw = true;
  }
  // The console is intentionally event-driven: it prints a menu once, then
  // waits for a complete line. Nothing redraws while an operator is typing.
  if (revealSecrets && static_cast<int32_t>(millis() - revealUntil) >= 0) {
    revealSecrets = false;
  }
  if (!rawStream && needsRedraw) render();
}
