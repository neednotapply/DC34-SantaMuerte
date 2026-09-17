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
#include "nfc_log.h"
#include "usb_hid.h"
#include "usb_badusb.h"
#include "usb_network.h"
#include "usb_drive.h"
#include "usb_dropbox.h"
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
  NFC_LOG,
  FIELD_NOTES,
  FIELD_NOTE_DETAIL,
  SYSTEM,
  USB,
  USB_CONTROLS,
  USB_BUTTON,
  USB_BUTTON_SHORT,
  USB_BUTTON_LONG,
  USB_PROFILE,
  USB_NETWORK,
  PAYLOADS,
  BADUSB,
  LOGS,
  HELP
};
enum class Prompt : uint8_t { NONE, AP_SSID, AP_PASSWORD, HOME_SSID, HOME_PASSWORD, LED_HEX, LED_BRIGHTNESS, LED_SPEED, NFC_TEXT, NFC_URL, EMU_TEXT, EMU_URL, FIELD_NOTE };
enum class Action : uint8_t { NONE, SWITCH_AP, SWITCH_HOME, SAVE_AP, TOGGLE_AP_HIDDEN, SAVE_HOME, NFC_WRITE_TEXT, NFC_WRITE_URL, EMU_TEXT, EMU_URL, NFC_WIFI, CLEAR_BOARD, CLEAR_NFC_LOG, REBOOT, POST_FIELD_NOTE, RUN_PAYLOAD, RUN_BADUSB_PAYLOAD, USB_POWER_OFF };

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
// Set only while a key posted by the Terminal page is being dispatched, so the
// handlers below can tell the cable apart from an open web portal.
bool keyFromPortal = false;
// Portal keys are queued, not handled inside the HTTP call: handleKey() can
// redraw a whole screen or reboot the badge, and none of that belongs inside
// a request the browser is still waiting on.
constexpr size_t PORTAL_KEY_CAPACITY = 64;
char portalKeys[PORTAL_KEY_CAPACITY];
size_t portalKeyCount = 0;
Prompt prompt = Prompt::NONE;
Action pendingConfirm = Action::NONE;
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
    case Screen::NFC_LOG: return tr("NFC // REGISTRO", "NFC // LOG");
    case Screen::FIELD_NOTES: return tr("NOTAS DE CAMPO", "FIELD NOTES");
    case Screen::FIELD_NOTE_DETAIL: return tr("NOTA DE CAMPO", "FIELD NOTE");
    case Screen::SYSTEM: return tr("SISTEMA // SYSTEM", "SYSTEM");
    case Screen::USB: return tr("USB // HERRAMIENTAS", "USB TOOLS");
    case Screen::USB_CONTROLS: return tr("USB // CONTROL DEL HOST", "USB // HOST CONTROLS");
    case Screen::USB_BUTTON: return tr("USB // BOTÓN DEL BADGE", "USB // BADGE BUTTON");
    case Screen::USB_BUTTON_SHORT: return tr("USB // PULSACIÓN", "USB // SHORT PRESS");
    case Screen::USB_BUTTON_LONG: return tr("USB // MANTENER", "USB // LONG PRESS");
    case Screen::USB_PROFILE: return tr("USB // MODO", "USB // MODE");
    case Screen::USB_NETWORK: return tr("USB // RED WI-FI", "USB // WI-FI NETWORK");
    case Screen::PAYLOADS: return tr("USB // DUCKYSCRIPT", "USB // DUCKYSCRIPT");
    case Screen::BADUSB: return tr("USB // BADUSB", "USB // BADUSB");
    case Screen::LOGS: return tr("DIAGNOSTICOS // LOGS", "DIAGNOSTICS // LOGS");
    case Screen::HELP: return tr("AYUDA // HELP", "HELP");
  }
  return "SANTA MUERTE";
}

// The portal's Terminal page is a second window onto this console, not a
// console of its own: same globals, same screen, same staged input. What it
// shows is therefore the byte stream the cable receives rather than a second
// rendering of it, so ANSI colour and the note photo previews arrive in
// the browser exactly as they arrive in minicom.
constexpr size_t MIRROR_CAPACITY = 8192;
constexpr uint32_t MIRROR_IDLE_MS = 15000;
uint8_t mirror[MIRROR_CAPACITY];
size_t mirrorHead = 0;         // where the next byte lands
size_t mirrorHeld = 0;         // bytes still readable, up to MIRROR_CAPACITY
uint32_t mirrorSequence = 0;   // bytes ever written; the browser's cursor
uint32_t mirrorPolledAt = 0;
bool mirrorWanted = false;     // nothing captured while no page is open

void mirrorWrite(const uint8_t *bytes, size_t length) {
  if (!mirrorWanted || !bytes || length == 0) return;
  mirrorSequence += length;
  // A single write longer than the ring could only ever be shown from its
  // tail, so drop the head of it here rather than wrapping over ourselves.
  if (length >= MIRROR_CAPACITY) {
    bytes += length - MIRROR_CAPACITY;
    length = MIRROR_CAPACITY;
  }
  for (size_t i = 0; i < length; ++i) {
    mirror[mirrorHead] = bytes[i];
    mirrorHead = (mirrorHead + 1) % MIRROR_CAPACITY;
  }
  mirrorHeld = (mirrorHeld + length < MIRROR_CAPACITY) ? mirrorHeld + length
                                                       : MIRROR_CAPACITY;
}

// Every byte the console prints goes to the cable and, while the Terminal page
// is open, into the ring as well. Routing all output through one object is
// what keeps the two views identical: nothing below writes to Serial directly.
struct ConsoleOut {
  void write(uint8_t value) { Serial.write(value); mirrorWrite(&value, 1); }
  void write(const uint8_t *bytes, size_t length) {
    Serial.write(bytes, length);
    mirrorWrite(bytes, length);
  }
  void print(const char *value) {
    if (value) write(reinterpret_cast<const uint8_t *>(value), strlen(value));
  }
  void print(const String &value) { print(value.c_str()); }
  void print(char value) { write(static_cast<uint8_t>(value)); }
  // Arduino's println() is CRLF; matching it keeps `screen` and PuTTY happy
  // for the same reason tuiPrintf() below prefixes CR.
  void println() { print("\r\n"); }
  void println(const char *value) { print(value); println(); }
  void println(const String &value) { print(value.c_str()); println(); }
  void printf(const char *format, ...) {
    char buffer[768];
    va_list arguments;
    va_start(arguments, format);
    const int length = vsnprintf(buffer, sizeof(buffer), format, arguments);
    va_end(arguments);
    if (length > 0)
      write(reinterpret_cast<const uint8_t *>(buffer),
            min(length, static_cast<int>(sizeof(buffer) - 1)));
  }
};
ConsoleOut out;

void color(const char *code) { if (ansi) out.print(code); }
void resetColor() { if (ansi) out.print("\x1b[0m"); }
void rowStart() { out.write('\r'); }
void tuiLine(const char *value) { rowStart(); out.println(value); }
void tuiLine(const String &value) { rowStart(); out.println(value); }
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
  out.write('\r');
  if (length > 0) out.write(reinterpret_cast<const uint8_t *>(buffer),
                               min(length, static_cast<int>(sizeof(buffer) - 1)));
}
// A full-screen terminal only needs one control sequence per completed menu
// action. Unlike the old TUI, nothing redraws while the operator is typing.
void clearTerminal() { out.print("\x1b[2J\x1b[H"); }
void line() { rowStart(); color("\x1b[38;5;137m"); out.println("------------------------------------------------------------"); resetColor(); }
void title(const char *value) { rowStart(); color("\x1b[1;38;5;179m"); out.println(value); resetColor(); }
void muted(const String &value) { rowStart(); color("\x1b[38;5;245m"); out.println(value); resetColor(); }
void good(const String &value) { rowStart(); color("\x1b[38;5;150m"); out.println(value); resetColor(); }
void warn(const String &value) { rowStart(); color("\x1b[38;5;215m"); out.println(value); resetColor(); }
void danger(const String &value) { rowStart(); color("\x1b[1;38;5;203m"); out.println(value); resetColor(); }

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
        out.printf("\x1b[38;5;%d;48;5;%dm", nextForeground,
                      nextBackground);
        foreground = nextForeground;
        background = nextBackground;
      }
      out.print("\xE2\x96\x80");
    }
    out.print("\x1b[0m\r\n");
  }
}

// English is the badge's source language, so the NFC, network and board
// subsystems hand back English status lines and errors no matter which
// language the terminal is in. data/locale.js does the same job for the web
// portal; the Spanish column here matches it word for word so both surfaces
// say the same thing. A phrase with no row here simply stays English in a
// Spanish session, which is the harmless direction to fail in.
struct Phrase { const char *english; const char *spanish; };

const Phrase phrases[] = {
    {"Reader ready. Pick an action and present a tag.", "Lector listo. Elige una acción y acerca un tag."},
    {"Tag detected. Processing…", "Tag detectado. Procesando…"},
    {"Ready to read", "Listo para leer"},
    {"Emulating", "Emulación activa"},
    {"Emulation stopped", "Emulación parada"},
    {"Auto-scan on. Every tag read goes to the NFC log.", "Escaneo automático encendido. Cada tag que se lea va al registro NFC."},
    {"Auto-scan off.", "Escaneo automático apagado."},
    {"Tag saved to the NFC log.", "Tag guardado en el registro NFC."},
    {"Tag had no data; its UID stayed in the NFC log.", "Tag sin datos; su UID quedó en el registro NFC."},
    {"The NFC reader has not started.", "El lector NFC no ha iniciado."},
    {"The NFC reader is not available.", "El lector NFC no está disponible."},
    {"The PN532 did not return to reader mode.", "El PN532 no volvió al modo lector."},
    {"The PN532 is unavailable for Wi-Fi sharing.", "El PN532 no está disponible para pasar Wi-Fi."},
    {"PN532 not found. Check power, SPI and wiring.", "No se encontró el PN532. Revisa corriente, SPI y cables."},
    {"The PN532 failed to start.", "Falló el inicio del PN532."},
    {"PN532 SAM configuration failed.", "Falló la configuración SAM del PN532."},
    {"PN532 retry configuration failed.", "Falló la configuración de reintentos del PN532."},
    {"The NFC task did not start.", "No arrancó la tarea NFC."},
    {"NFC synchronisation did not start.", "No arrancó la sincronización NFC."},
    {"Stop acting as a tag before reading or writing another one.", "Deja de actuar como tag antes de leer o escribir otro."},
    {"Another NFC action is already waiting for a tag.", "Ya hay otra acción NFC esperando un tag."},
    {"Wait for the NFC action to finish.", "Espera a que termine la acción NFC."},
    {"Wait for the tag action to finish.", "Espera a que termine la acción del tag."},
    {"Type 2 memory seen, but no valid container.", "Se vio memoria Type 2, pero sin contenedor válido."},
    {"Saved settings are unavailable.", "Los ajustes guardados no están disponibles."},
    {"Could not open NVS to save the access-point setting.", "No se pudo abrir NVS para guardar el ajuste del punto de acceso."},
    {"Could not save the access-point setting.", "No se pudo guardar el ajuste del punto de acceso."},
    {"Could not open NVS to save the language.", "No se pudo abrir NVS para guardar el idioma."},
    {"Could not save the language.", "No se pudo guardar el idioma."},
    {"Could not open NVS to save the Wi-Fi.", "No se pudo abrir NVS para guardar el Wi-Fi."},
    {"The new password could not be saved.", "La contraseña nueva no se pudo guardar."},
    {"The hidden-SSID setting could not be saved.", "El ajuste de SSID oculto no se pudo guardar."},
    {"Could not open NVS to save saved Wi-Fi.", "No se pudo abrir NVS para guardar el Wi-Fi guardado."},
    {"Saved Wi-Fi could not be saved.", "El Wi-Fi guardado no se pudo guardar."},
    {"The password must be 8 to 63 characters.", "La contraseña debe tener 8 a 63 caracteres."},
    {"The saved Wi-Fi SSID must be 1 to 32 bytes with no control characters.", "El SSID guardado debe tener 1 a 32 bytes sin controles."},
    {"Use visible ASCII. Accents and emoji do not fit in a WPA2 password.", "Usa ASCII visible. Letras con acento y emoji no caben en la clave WPA2."},
    {"The board is unavailable.", "El tablero no está disponible."},
    {"Draw, write, or do both.", "Dibuja, escribe o haz las dos."},
    {"The note could not be saved.", "No se pudo guardar la nota."},
    {"Hold an NFC tag to the reader on the PCB to read it.", "Acerca un tag NFC al lector del PCB para leerlo."},
    {"Hold a Type 2 tag to save the text.", "Acerca un tag Type 2 para guardar el texto."},
    {"Hold a Type 2 tag to save the URL.", "Acerca un tag Type 2 para guardar la URL."},
    {"Hold a writable Type 2 tag to save the text.", "Acerca un tag Type 2 que se pueda escribir para guardar el texto."},
    {"Hold a writable Type 2 tag to save the URL.", "Acerca un tag Type 2 que se pueda escribir para guardar la URL."},
    {"The SSID is empty or longer than 32 bytes.", "El SSID está vacío o pasa de 32 bytes."},
    {"The content is over the 700-byte limit.", "El contenido pasa el límite de 700 bytes."},
    {"The Type 2 TLV length is over the tag's capacity.", "El largo TLV Type 2 pasa la capacidad del tag."},
    {"The NDEF record is too big for this tag.", "El registro NDEF pesa mucho para este tag."},
    {"The Type 4 tag carries an empty NDEF message.", "El tag Type 4 trae un mensaje NDEF vacío."},
    {"The tag has no user memory.", "El tag no tiene memoria de usuario."},
    {"The tag answers as Type 2 memory but is not in NDEF format. Its first bytes are shown below.", "El tag responde como memoria Type 2, pero no está en formato NDEF. Abajo salen sus primeros bytes."},
    {"The tag carries a TLV length larger than its data area.", "El tag trae un largo TLV mayor que su área de datos."},
    {"This tag reports its NDEF as read-only.", "Este tag dice que su NDEF es solo lectura."},
    {"This tag is not in NFC Forum Type 2 format. It is not formatted automatically, because that could alter an incompatible chip.", "Este tag no está en formato NFC Forum Type 2. No se formatea solo porque eso podría cambiar un chip incompatible."},
    {"This tag has no memory to write to.", "Este tag no tiene memoria para escribir."},
    {"This tag carries an empty NDEF message.", "Este tag trae un mensaje NDEF vacío."},
    {"The NDEF write to MIFARE Classic failed.", "Falló la escritura NDEF en MIFARE Classic."},
    {"A MIFARE Classic URL must be 1 to 38 characters after the prefix.", "La URL para MIFARE Classic debe medir entre 1 y 38 caracteres después del prefijo."},
    {"The NFC action queue is full.", "La cola de acciones NFC está llena."},
    {"Type something before you start.", "Escribe algo antes de empezar."},
    {"The content is too big to present.", "El contenido es demasiado grande para presentarlo."},
    {"Stopping.", "Dejando de actuar como tag."},
    {"NFC reader detected. Sending the content…", "Lector NFC detectado. Enviando el contenido…"},
    {"The NFC reader left before it finished.", "El lector NFC se fue antes de terminar."},
    {"Auto-scan switched off.", "El escaneo automático se apagó."},
    {"Only 220 characters fit.", "Solo caben 220 caracteres."},
    {"The badge is not acting as a tag.", "El badge no está actuando como tag."},
    {"You can only present text or a link.", "Solo puedes presentar texto o un enlace."},
    {"The Type 2 page is outside the NTAG2xx range.", "La página Type 2 queda fuera del rango NTAG2xx."},
    {"The MIFARE Classic card answers to neither factory keys nor NDEF; it cannot be formatted for writing.", "La tarjeta MIFARE Classic no responde a llaves de fábrica ni NDEF; no se puede formatear para escritura."},
    {"Ready. Hold a phone or an NFC reader to the badge.", "Listo. Acerca un teléfono o un lector NFC al badge."},
    {"The Wi-Fi details do not fit.", "Los datos de Wi-Fi no caben."},
    {"The Wi-Fi details are not valid.", "Los datos de Wi-Fi no son válidos."},
    {"MIFARE Classic read.", "MIFARE Classic leído."},
    {"No tag was detected in 15 seconds.", "No se detectó ningún tag en 15 segundos."},
    {"No NDEF message was found on the tag.", "No se encontró mensaje NDEF en el tag."},
    {"The Type 2 capability page could not be read.", "No se pudo leer la página de capacidad Type 2."},
    {"The NFC action could not be queued.", "No se pudo poner la acción NFC en cola."},
    {"Could not start acting as a tag.", "No se pudo empezar a actuar como tag."},
    {"On MIFARE Classic the badge writes URLs only; write text to an NTAG / Ultralight (Type 2) tag.", "Para MIFARE Classic el badge escribe solo URLs; escribe texto en un tag NTAG / Ultralight (Type 2)."},
    {"Stopping…", "Dejando de actuar como tag…"},
    {"A reader just read the badge.", "Un lector acaba de leer el badge."},
    {"The tag was detected, but its Type 2 memory could not be read.", "Se detectó el tag, pero no se pudo leer su memoria Type 2."},
    {"UTF-16 text was detected; only UTF-8 is shown here.", "Se detectó texto UTF-16; aquí solo se muestra UTF-8."},
    {"The UID was read. It answered as neither Type 4 nor opened with known MIFARE Classic keys.", "Se leyó el UID. No respondió como Type 4 ni abrió con llaves MIFARE Classic conocidas."},
    {"The UID was read. It carries no readable Type 2 NDEF and did not answer as Type 4.", "Se leyó el UID. No trae NDEF Type 2 legible ni respondió como Type 4."},
    {"A Type 4 tag was read, but its NDEF could not be interpreted.", "Se leyó un tag Type 4, pero el NDEF no se pudo interpretar."},
    {"Type 4 tag read.", "Tag Type 4 leído."},
    {"Tag read.", "Tag leído."},
    {"Another NFC action is still being queued.", "Todavía se está poniendo otra acción NFC en cola."},
    {"Read the tag ID.", "Se leyó el ID del tag."},
    {"That button action is not valid.", "Acción de botón no válida."},
    {"The button actions could not be saved.", "No se pudieron guardar las acciones del botón."},
};

String localized(const String &value) {
  if (english || !value.length()) return value;
  for (const Phrase &phrase : phrases) {
    if (value == phrase.english) return phrase.spanish;
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

// The portal has no login of its own: anyone who joins the badge's AP reaches
// it. So the actions that hand out something nobody can take back -- the saved
// passphrases, a keystroke payload typed into the attached computer, the
// field note ring, the host's power state -- answer to the cable only. Nothing
// is hidden from the page; the refusal simply says where to go instead.
bool portalMayNot(const char *spanish, const char *englishText) {
  if (!keyFromPortal) return false;
  setNotice(String(tr(spanish, englishText)) +
                tr(" Usa la consola USB.", " Use the USB console."),
            true);
  return true;
}

void revealPasswords() {
  if (portalMayNot("Las claves se muestran solo por cable.",
                   "Passwords are shown over the cable only.")) return;
  revealSecrets = true;
  revealUntil = millis() + REVEAL_MS;
  setNotice(tr("Claves visibles durante 10 segundos.",
               "Passwords visible for 10 seconds."));
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
  // Nine-wide label column: sized for the longest label so no row is shunted
  // out of line the way an overflowing one used to be.
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
  tuiPrintf("%-9s %s%s\n", "NFC", nfc.readerReady ? tr("listo", "ready") : tr("PN532 fuera", "PN532 offline"), nfc.emulating ? tr(" / emulando", " / emulating") : nfc.captureEnabled ? tr(" / escaneando", " / capturing") : "");
  muted(clipped(localized(nfc.message), 64));
  tuiPrintf("%-9s %u / %u %s // %u %s\n", "NOTES", boardStoredCount(),
            boardCapacity(), tr("textos", "texts"), boardImageCapacity(),
            tr("dibujos", "drawings"));
  tuiPrintf("%-9s %u / %u %s\n", "NFC LOG", nfcLogStoredCount(),
            nfcLogCapacity(), tr("tags vistos", "tags seen"));
  tuiPrintf("%-9s %u KB %s // %u KB // %lus\n", tr("MEMORIA", "MEMORY"), ESP.getFreeHeap() / 1024, tr("libres", "free"), ESP.getMaxAllocHeap() / 1024, millis() / 1000);
  tuiPrintf("%-9s %s\n", "HID", usbHidStatusLine().c_str());
  out.println();
  tuiLine(tr("1 Notas de Campo", "1 Field Notes"));
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
  // A remembered open network has no key at all. Saying so borrows the AP's
  // own wording: mask() reports an empty string as "(not saved)", which would
  // read as a credential that failed to save rather than as a network that
  // was deliberately joined without one.
  const char *homeKey = getPersistentStationWifiPassword();
  const String homePassword =
      !wifi.homeConfigured
          ? String(tr("(sin guardar)", "(not saved)"))
          : (homeKey[0] ? (revealSecrets ? String(homeKey) : mask(homeKey))
                        : String(tr("abierta", "open")));
  tuiPrintf("%s: %s\n\n", tr("MODO ACTIVO", "ACTIVE MODE"),
            wifi.accessPointActive ? "SANTA MUERTE AP"
                                   : tr("WI-FI GUARDADO", "SAVED WI-FI"));
  tuiPrintf("AP   %s // %s %s // %s\n", wifi.accessPointSsid.c_str(),
            tr("clave", "key"), apPassword.c_str(),
            wifi.hidden ? tr("oculta", "hidden") : tr("visible", "visible"));
  tuiPrintf("%s %s // %s %s // %s\n\n", tr("GUARDADO", "SAVED"),
            wifi.homeConfigured ? wifi.homeSsid.c_str()
                                : tr("sin guardar", "not saved"),
            tr("clave", "key"), homePassword.c_str(),
            wifi.homeConnected ? wifi.localIp.c_str()
                               : tr("sin conectar", "offline"));
  if (wifi.trialActive) {
    tuiPrintf("%-9s %s // %s\n\n", tr("PROBANDO", "TRYING"),
              wifi.trialSsid.c_str(),
              tr("se guarda solo si conecta", "only saved if it connects"));
  } else if (wifi.trialFailed) {
    tuiPrintf("%-9s %s // %s\n\n", tr("RECHAZADA", "REJECTED"),
              wifi.trialSsid.c_str(),
              tr("no conectó, no se guardó", "did not connect, not saved"));
  }
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
                         ? tr("Escaneo automático", "Auto-scan")
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
  tuiPrintf("%s  //  %u / %u\n", tr("5 Registro NFC", "5 NFC Log"),
            nfcLogStoredCount(), nfcLogCapacity());
}

void renderNfcMode() {
  const NfcTuiState nfc = getNfcTuiState();
  String current = nfc.captureEnabled
                       ? tr("Escaneo automático", "Auto-scan")
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
  tuiLine(tr("1 Escaneo automático", "1 Auto-scan"));
  tuiLine(tr("2 Wi-Fi por NFC", "2 Wi-Fi over NFC"));
  tuiLine(tr("3 Emular tag recordado", "3 Emulate remembered tag"));
  tuiLine(tr("4 Parar", "4 Stop"));
}

// The same encounter journal the portal shows, newest first. Identity on one
// line and whatever came off the tag beneath it: a UID and a card type do not
// leave room for the content on a single 80-column row, and the content is the
// half worth reading.
void renderNfcLog() {
  tuiPrintf("%u / %u %s\n\n", nfcLogStoredCount(), nfcLogCapacity(),
            tr("tags guardados", "stored tags"));
  uint32_t cursor = 0;
  NfcLogEntry entry;
  uint8_t shown = 0;
  while (shown < 6 && nfcLogReadNext(cursor, entry)) {
    String seen;
    if (entry.hitCount > 1) { seen = " x"; seen += entry.hitCount; }
    tuiPrintf("%-24s %s%s\n", clipped(entry.uid, 24).c_str(),
              clipped(entry.tagType.length() ? entry.tagType : String("ISO14443A"),
                      34).c_str(),
              seen.c_str());
    muted(String("  ") + (entry.content.length()
                              ? clipped(entry.content, 60)
                              : String(tr("[solo UID]", "[UID only]"))));
    ++shown;
  }
  if (!shown) {
    muted(tr("El registro está vacío. Enciende el escaneo automático en NFC // MODO.",
             "The log is empty. Turn on auto-scan in NFC // MODE."));
  } else if (nfcLogStoredCount() > shown) {
    muted(String(tr("El portal muestra el resto: ", "The portal shows the rest: ")) +
          "http://santamuerte.local/nfc-log");
  }
  out.println();
  tuiLine(tr("1 Borrar el registro", "1 Clear the log"));
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
    out.println();
  }
  tuiLine(tr("1 Emular texto", "1 Emulate text"));
  tuiLine(tr("2 Emular URL", "2 Emulate URL"));
}

void renderFieldNotes() {
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
  out.println();
  muted(tr("Escribe un ID de cuatro dígitos para abrir una nota.",
           "Enter a four-digit ID to open a note."));
  tuiLine(tr("1 Nueva nota de texto", "1 New text note"));
  tuiLine(tr("2 Borrar todas las notas", "2 Clear all Field Notes"));
}

void renderFieldNoteDetail() {
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
    out.println();
    tuiTextLines(post.text);
  } else if (!post.hasImage) {
    muted(tr("[nota vacía]", "[empty note]"));
  }
  if (post.hasImage) {
    out.println();
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
  tuiLine(tr("Cada acción se ejecuta al elegirla; borrar, reiniciar y apagar piden y/n.", "Each action runs when you choose it; erase, reboot and power off ask y/n."));
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
  tuiLine(tr("3 DuckyScript", "3 DuckyScript"));
  tuiLine(tr("4 BadUSB", "4 BadUSB"));
  tuiLine(tr("5 Modo USB", "5 USB mode"));
  if (getPersistentUsbDeviceProfile() == UsbDeviceProfile::NETWORK) {
    tuiLine(tr("6 WiFi Tethering", "6 WiFi Tethering"));
  }
}

void renderUsbProfile() {
  const UsbDeviceProfile profile = getPersistentUsbDeviceProfile();
  const UsbDriveState drive = getUsbDriveState();
  const UsbDropboxState dropbox = getUsbDropboxState();
  tuiPrintf("%-9s %s\n\n", "ACTIVO",
            profile == UsbDeviceProfile::NETWORK ? tr("WiFi Tethering", "WiFi Tethering")
                                                 : tr("Unidad Notas de Campo", "Field Notes Drive"));
  tuiLine(tr("1 WiFi Tethering: Serial + NCM", "1 WiFi Tethering: Serial + NCM"));
  tuiLine(tr("2 Unidad: Serial + HID + Notas (solo lectura) + DROP BOX",
             "2 Drive: Serial + HID + Field Notes (read-only) + DROP BOX"));
  if (profile == UsbDeviceProfile::DRIVE) {
    tuiPrintf("%-9s %u %s // %u ducky // %u badusb // %u tags\n", "UNIDAD", drive.noteCount,
              tr("notas", "notes"), drive.scriptCount, drive.badusbScriptCount,
              drive.tagCount);
    // Worth saying out loud: for the first seconds of a boot the interface is
    // up with nothing in it, and a host will show no drive until it is.
    if (!drive.mediaPresent) {
      muted(tr("Sin medio todavía; el badge sigue armando la instantánea.",
               "No medium yet; the badge is still building the snapshot."));
    }
    // The writable volume: drag DuckyScript/BadUSB files onto it and the badge
    // imports them into /payloads, where they join the read-only drive above.
    if (dropbox.available) {
      tuiPrintf("%-9s %u ducky // %u badusb %s\n", "DROPBOX", dropbox.importedDucky,
                dropbox.importedBadUSB, tr("importados", "imported"));
    } else {
      muted(tr("DROP BOX no disponible (falta la particion ffat).",
               "DROP BOX unavailable (no ffat partition)."));
    }
  }
  out.println();
  muted(tr("Cambiar el modo guarda la opción y reinicia el badge. Serial sigue disponible.",
           "Changing mode saves it and reboots the badge. Serial remains available."));
}

void renderUsbNetwork() {
  const UsbNetworkState state = getUsbNetworkState();
  tuiPrintf("%-9s %s\n", "NCM", state.available ? tr("instalado", "available")
                                                  : tr("no disponible", "unavailable"));
  tuiPrintf("%-9s %s\n", "MODE", state.enabled ? tr("puente activo", "bridge active")
                                                    : tr("en espera", "waiting"));
  tuiPrintf("%-9s %s\n\n", "LINK", state.linkUp ? tr("Wi-Fi guardado conectado", "saved Wi-Fi connected")
                                                     : tr("Wi-Fi guardado desconectado", "saved Wi-Fi disconnected"));
  muted(tr("Comparte el Wi-Fi guardado con el equipo por NCM.",
           "Shares saved Wi-Fi with the attached computer through NCM."));
  muted(tr("El puente sigue al Wi-Fi guardado: no hay nada que iniciar.",
           "The bridge follows saved Wi-Fi; there is nothing to start."));
  muted(tr("Mantén BOOT para salir a la Unidad Notas de Campo.",
           "Hold BOOT to leave for the Field Notes Drive."));
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
    muted(tr("Sin scripts. Créales en el portal: http://10.69.4.20/ducky",
             "No scripts. Author them in the portal: http://10.69.4.20/ducky"));
  } else {
    for (uint8_t i = 0; i < count && i < 16; ++i) {
      tuiPrintf("%u %s\n", i + 1, usbHidPayloadNameAt(i).c_str());
    }
  }
  out.println();
  muted(tr("17 detiene una carga en curso.", "17 stops a running payload."));
  danger(tr("TECLEA en la computadora conectada. Úsalo solo en la tuya.",
            "This TYPES into the attached computer. Use it only on your own."));
}

void renderBadUsb() {
  tuiPrintf("%-9s %s\n", tr("ESTADO", "STATUS"), usbBadUSBStatusLine().c_str());
  tuiPrintf("%-9s %s\n", "HOST", usbBadUSBHostSeen() ? tr("visto", "seen") : tr("sin señal", "no signal"));
  const uint8_t leds = usbBadUSBHostLeds();
  String locks;
  if (leds & USB_BADUSB_LED_CAPSLOCK) locks += "CAPS ";
  if (leds & USB_BADUSB_LED_NUMLOCK) locks += "NUM ";
  if (leds & USB_BADUSB_LED_SCROLLLOCK) locks += "SCROLL ";
  if (locks.isEmpty()) locks = tr("ninguno", "none");
  tuiPrintf("%-9s %s\n\n", tr("CANDADOS", "LOCKS"), locks.c_str());

  const uint8_t count = usbBadUSBPayloadCount();
  if (count == 0) {
    muted(tr("Sin scripts. Créales en el portal: http://10.69.4.20/badusb",
             "No scripts. Author them in the portal: http://10.69.4.20/badusb"));
  } else {
    for (uint8_t i = 0; i < count && i < 16; ++i) {
      tuiPrintf("%u %s\n", i + 1, usbBadUSBPayloadNameAt(i).c_str());
    }
  }
  out.println();
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
    case Screen::NFC_LOG: renderNfcLog(); break;
    case Screen::FIELD_NOTES: renderFieldNotes(); break;
    case Screen::FIELD_NOTE_DETAIL: renderFieldNoteDetail(); break;
    case Screen::SYSTEM: renderSystem(); break;
    case Screen::USB: renderUsbTools(); break;
    case Screen::USB_CONTROLS: renderUsbControls(); break;
    case Screen::USB_BUTTON: renderUsbButton(); break;
    case Screen::USB_BUTTON_SHORT:
    case Screen::USB_BUTTON_LONG: renderUsbButtonActions(); break;
    case Screen::USB_NETWORK: renderUsbNetwork(); break;
    case Screen::USB_PROFILE: renderUsbProfile(); break;
    case Screen::PAYLOADS: renderPayloads(); break;
    case Screen::BADUSB: renderBadUsb(); break;
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
  if (pendingConfirm != Action::NONE) {
    out.print(tr("Confirmar [y/n]> ", "Confirm [y/n]> "));
  } else if (prompt != Prompt::NONE) {
    const bool secret = prompt == Prompt::AP_PASSWORD || prompt == Prompt::HOME_PASSWORD;
    out.print(tr("Valor> ", "Value> "));
    if (secret) out.print(maskedInput()); else out.print(input);
  } else {
    out.print(tr("Selección> ", "Selection> "));
  }
  needsRedraw = false;
}

void enterRawStream() {
  rawStream = true;
  out.println(tr("\r\n[RAW LOGS] Pulsa cualquier tecla para volver al TUI.\r\n",
                    "\r\n[RAW LOGS] Press any key to return to the TUI.\r\n"));
}

void beginPrompt(Prompt next, const String &message) { prompt = next; input = String(); notice = message; needsRedraw = true; }
void beginConfirm(Action next, const String &message) { pendingConfirm = next; notice = message; needsRedraw = true; }

bool parseHex(const String &value, int &r, int &g, int &b) {
  String text = value; if (text.startsWith("#")) text.remove(0, 1);
  if (text.length() != 6) return false;
  char *end = nullptr; const long number = strtol(text.c_str(), &end, 16);
  if (!end || *end) return false;
  r = (number >> 16) & 0xFF; g = (number >> 8) & 0xFF; b = number & 0xFF; return true;
}

// Everything the console can do. A menu selection is already an instruction,
// so it is carried out where it is made. The exceptions are the three that
// cost work nobody can get back: erasing the field note ring, rebooting the
// badge, and powering off the attached computer all stop to ask first.
void performAction(Action action) {
  String error;
  bool ok = false;
  switch (action) {
    case Action::CLEAR_BOARD:
    case Action::CLEAR_NFC_LOG:
    case Action::RUN_PAYLOAD:
    case Action::USB_POWER_OFF:
      if (portalMayNot("Esa accion es solo por cable.",
                       "That action is cable-only.")) return;
      break;
    default: break;
  }
  switch (action) {
    case Action::SWITCH_AP: ok = setBadgeAccessPointEnabled(true, error); break;
    case Action::SWITCH_HOME: ok = setBadgeAccessPointEnabled(false, error); break;
    case Action::SAVE_AP:
      ok = setBadgeAccessPointSettings(stagedA, stagedB,
                                       getPersistentWifiHidden(), error);
      break;
    case Action::TOGGLE_AP_HIDDEN:
      ok = setBadgeAccessPointSettings(getBadgeWifiSsid(),
                                       getBadgeWifiPassword(),
                                       !getPersistentWifiHidden(), error);
      break;
    case Action::SAVE_HOME:
      if (setBadgeHomeWifiSettings(stagedA, stagedB, error)) {
        setNotice(tr("Probando la red… se guarda solo si conecta.",
                     "Testing the network… it is only saved if it connects."));
      } else {
        setNotice(error, true);
      }
      return;
    case Action::NFC_WRITE_TEXT: ok = queueNfcWrite("text", stagedA); break;
    case Action::NFC_WRITE_URL: ok = queueNfcWrite("url", stagedA); break;
    case Action::EMU_TEXT: ok = startNfcTagEmulation("text", stagedA); break;
    case Action::EMU_URL: ok = startNfcTagEmulation("url", stagedA); break;
    case Action::NFC_WIFI: {
      if (!isBadgeAccessPointActive()) {
        error = tr("Activa primero Santa Muerte AP; el Wi-Fi guardado no se comparte por NFC.",
                   "Turn on Santa Muerte AP first; saved Wi-Fi is not shared over NFC.");
        break;
      }
      uint8_t mac[6] = {}; ok = startNfcWifiOnboarding(getBadgeWifiSsid(), getBadgeWifiPassword(), getBadgeWifiApMac(mac) ? mac : nullptr); break;
    }
    case Action::CLEAR_BOARD: ok = clearBoard(); break;
    case Action::CLEAR_NFC_LOG: ok = clearNfcLog(); break;
    case Action::POST_FIELD_NOTE:
      ok = addBoardPost(stagedA, 0, nullptr, 0, error, USB_CONSOLE_AUTHOR_ID);
      break;
    case Action::RUN_PAYLOAD: ok = usbHidRunPayload(stagedA, error); break;
    case Action::RUN_BADUSB_PAYLOAD: ok = usbBadUSBRunPayload(stagedA, error); break;
    case Action::USB_POWER_OFF:
      ok = usbHidRunControl(UsbControlAction::SYSTEM_POWER_OFF, error);
      break;
    case Action::REBOOT: out.println(tr("[TUI] Reiniciando...", "[TUI] Rebooting...")); delay(80); ESP.restart(); return;
    default: break;
  }
  setNotice(ok ? tr("Hecho.", "Done.")
               : error.length() ? error : String(tr("No se pudo completar la acción.", "Could not complete the action.")),
            !ok);
}

void applyConfirm() {
  const Action action = pendingConfirm;
  pendingConfirm = Action::NONE;
  performAction(action);
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
    prompt = Prompt::NONE;
    performAction(Action::SAVE_AP);
  }
  else if (prompt == Prompt::HOME_SSID) { stagedA = input; beginPrompt(Prompt::HOME_PASSWORD, tr("Clave de Wi-Fi guardado (vacía = red abierta)", "Saved Wi-Fi password (empty = open network)")); }
  else if (prompt == Prompt::HOME_PASSWORD) { stagedB = input; prompt = Prompt::NONE; performAction(Action::SAVE_HOME); }
  else if (prompt == Prompt::LED_HEX) { int r, g, b; if (parseHex(input, r, g, b) && setLedTuiState("", r, g, b, -1, -1)) setNotice(tr("Color actualizado.", "Colour updated.")); else setNotice(tr("Usa #RRGGBB.", "Use #RRGGBB."), true); prompt = Prompt::NONE; }
  else if (prompt == Prompt::LED_BRIGHTNESS) { const int value = input.toInt(); if (value >= 0 && value <= 255 && setLedTuiState("", -1, -1, -1, value, -1)) setNotice(tr("Brillo actualizado.", "Brightness updated.")); else setNotice(tr("Brillo: 0 a 255.", "Brightness: 0 to 255."), true); prompt = Prompt::NONE; }
  else if (prompt == Prompt::LED_SPEED) { const int value = input.toInt(); if (value >= 1 && value <= 100 && setLedTuiState("", -1, -1, -1, -1, value)) setNotice(tr("Velocidad actualizada.", "Speed updated.")); else setNotice(tr("Velocidad: 1 a 100.", "Speed: 1 to 100."), true); prompt = Prompt::NONE; }
  else if (prompt == Prompt::NFC_TEXT) { stagedA = input; prompt = Prompt::NONE; performAction(Action::NFC_WRITE_TEXT); }
  else if (prompt == Prompt::NFC_URL) { stagedA = input; prompt = Prompt::NONE; performAction(Action::NFC_WRITE_URL); }
  else if (prompt == Prompt::EMU_TEXT) { stagedA = input; prompt = Prompt::NONE; performAction(Action::EMU_TEXT); }
  else if (prompt == Prompt::EMU_URL) { stagedA = input; prompt = Prompt::NONE; performAction(Action::EMU_URL); }
  else if (prompt == Prompt::FIELD_NOTE) {
    stagedA = input;
    prompt = Prompt::NONE;
    performAction(Action::POST_FIELD_NOTE);
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
    performAction(isBadgeAccessPointActive() ? Action::SWITCH_HOME : Action::SWITCH_AP);
  }
}

void handleScreenKey(char key) {
  if (key == 27 || key == 'q' || key == 'Q') { screen = Screen::DASHBOARD; needsRedraw = true; return; }
  if (screen == Screen::NETWORK) {
    const WifiTuiState wifi = getWifiTuiState();
    if (key == '1') beginPrompt(Prompt::AP_SSID,
                                 tr("Nombre nuevo del AP (SSID)",
                                    "New badge AP name (SSID)"));
    else if (key == '2') performAction(Action::TOGGLE_AP_HIDDEN);
    else if (key == '3') performAction(Action::SWITCH_AP);
    else if (key == '4') beginPrompt(Prompt::HOME_SSID, tr("SSID de Wi-Fi guardado", "Saved Wi-Fi SSID"));
    else if (key == '5') performAction(Action::SWITCH_HOME);
    else if (key == 'v' || key == 'V') revealPasswords();
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
    else if (key == '4') { const bool next = !isNfcCaptureEnabled(); setNotice(setNfcCaptureEnabled(next) ? next ? tr("Escaneo automático encendido.", "Auto-scan on.") : tr("Escaneo automático apagado.", "Auto-scan off.") : tr("No se pudo cambiar el escaneo automático.", "Could not change auto-scan."), false); }
    else if (key == '5') beginPrompt(Prompt::EMU_TEXT, tr("Texto para emular", "Text to emulate"));
    else if (key == '6') beginPrompt(Prompt::EMU_URL, tr("URL para emular", "URL to emulate"));
    else if (key == '7') performAction(Action::NFC_WIFI);
    else if (key == '8') setNotice(stopNfcTagEmulation() ? tr("Dejando de actuar como tag.", "Stopping.") : tr("No se pudo parar.", "Could not stop."), false);
  } else if (screen == Screen::NFC_LOG) {
    if (key == '1') beginConfirm(Action::CLEAR_NFC_LOG,
                                 tr("Borrar el registro NFC", "Clear the NFC Log"));
  } else if (screen == Screen::FIELD_NOTES) {
    if (key == '1') beginPrompt(Prompt::FIELD_NOTE,
                                 tr("Texto de la nota", "Field Note text"));
    else if (key == '2') beginConfirm(Action::CLEAR_BOARD,
                                      tr("Borrar todas las notas", "Clear all Field Notes"));
  } else if (screen == Screen::SYSTEM) {
    if (key == '1') { screen = Screen::LOGS; needsRedraw = true; }
    else if (key == '2') enterRawStream();
    else if (key == '3') { screen = Screen::NETWORK; revealPasswords(); }
    else if (key == '4') beginConfirm(Action::REBOOT, tr("Reiniciar el badge", "Reboot the badge"));
    else if (key == '5') routeGlobal('l');
  } else if (screen == Screen::PAYLOADS) {
    if (key == 's' || key == 'S') { usbHidStop(); setNotice(tr("Detenido.", "Stopped.")); }
    else {
      int index = -1;
      if (key >= '1' && key <= '9') index = key - '1';
      else if (key >= 'a' && key <= 'g') index = 9 + (key - 'a');
      if (index >= 0 && index < static_cast<int>(usbHidPayloadCount())) {
        stagedA = usbHidPayloadNameAt(static_cast<uint8_t>(index));
        performAction(Action::RUN_PAYLOAD);
      }
    }
  } else if (screen == Screen::BADUSB) {
    if (key == 's' || key == 'S') { usbBadUSBStop(); setNotice(tr("Detenido.", "Stopped.")); }
    else {
      int index = -1;
      if (key >= '1' && key <= '9') index = key - '1';
      else if (key >= 'a' && key <= 'g') index = 9 + (key - 'a');
      if (index >= 0 && index < static_cast<int>(usbBadUSBPayloadCount())) {
        stagedA = usbBadUSBPayloadNameAt(static_cast<uint8_t>(index));
        performAction(Action::RUN_BADUSB_PAYLOAD);
      }
    }
  }
  needsRedraw = true;
}

void handleDashboardSelection(int selection) {
  switch (selection) {
    case 1: screen = Screen::FIELD_NOTES; break;
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
    } else if (selection == 5) {
      screen = Screen::NFC_LOG;
    } else {
      setNotice(tr("Selección inválida.", "Invalid selection."), true);
    }
    needsRedraw = true;
    return;
  }

  if (screen == Screen::NFC_LOG) {
    if (selection == 1) {
      beginConfirm(Action::CLEAR_NFC_LOG,
                   tr("Borrar el registro NFC", "Clear the NFC Log"));
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
      setNotice(started ? tr("Escaneo automático encendido.", "Auto-scan on.")
                        : tr("No se pudo encender el escaneo automático.",
                             "Could not turn on auto-scan."),
                !started);
    } else if (selection == 2) {
      performAction(Action::NFC_WIFI);
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

  if (screen == Screen::FIELD_NOTES) {
    if (selection == 1) {
      beginPrompt(Prompt::FIELD_NOTE, tr("Texto de la nota", "Field Note text"));
    } else if (selection == 2) {
      beginConfirm(Action::CLEAR_BOARD,
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
    else if (selection == 4) screen = Screen::BADUSB;
    else if (selection == 5) screen = Screen::USB_PROFILE;
    else if (selection == 6 && getPersistentUsbDeviceProfile() == UsbDeviceProfile::NETWORK) screen = Screen::USB_NETWORK;
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
    // Nothing to choose here: the screen reports what the bridge is doing.
    setNotice(tr("El puente sigue al Wi-Fi guardado; no hay nada que iniciar.",
                 "The bridge follows saved Wi-Fi; there is nothing to start."),
              true);
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
        beginConfirm(Action::USB_POWER_OFF,
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
      performAction(Action::RUN_PAYLOAD);
    } else {
      setNotice(tr("Selección inválida.", "Invalid selection."), true);
    }
    needsRedraw = true;
    return;
  }

  if (screen == Screen::BADUSB) {
    if (selection == 17) {
      usbBadUSBStop();
      setNotice(tr("Detenido.", "Stopped."));
    } else if (selection >= 1 &&
               selection <= static_cast<int>(usbBadUSBPayloadCount())) {
      stagedA = usbBadUSBPayloadNameAt(static_cast<uint8_t>(selection - 1));
      performAction(Action::RUN_BADUSB_PAYLOAD);
    } else {
      setNotice(tr("Selección inválida.", "Invalid selection."), true);
    }
    needsRedraw = true;
    return;
  }

  if (screen == Screen::NETWORK && selection == 6) {
    revealPasswords();
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
  if (pendingConfirm != Action::NONE) {
    if (lower == "y" || lower == "yes") applyConfirm();
    else if (lower == "n" || lower == "no") {
      pendingConfirm = Action::NONE;
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
  // routeGlobal() has carried an ANSI toggle since the TUI was written, but
  // nothing ever called it with 'p': every caller passes 'l'. On a line
  // oriented console the letter has to arrive as a typed command like any
  // other, so this is the path that was missing. The Terminal page's Colour
  // button sends exactly this.
  if (lower == "p" || lower == "color" || lower == "colour") {
    routeGlobal('p');
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
    } else if (screen == Screen::FIELD_NOTE_DETAIL) {
      screen = Screen::FIELD_NOTES;
    } else if (screen == Screen::USB_BUTTON_SHORT ||
               screen == Screen::USB_BUTTON_LONG) {
      screen = Screen::USB_BUTTON;
    } else if (screen == Screen::USB_CONTROLS ||
               screen == Screen::USB_BUTTON || screen == Screen::USB_PROFILE ||
               screen == Screen::USB_NETWORK ||
               screen == Screen::PAYLOADS || screen == Screen::BADUSB) {
      screen = Screen::USB;
    } else {
      screen = Screen::DASHBOARD;
    }
    needsRedraw = true;
    return;
  }

  if (screen == Screen::FIELD_NOTES) {
    uint32_t postId = 0;
    if (parseBoardPostId(commandText, postId)) {
      BoardPost post;
      if (findBoardPost(postId, post)) {
        selectedPostId = postId;
        screen = Screen::FIELD_NOTE_DETAIL;
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
      pendingConfirm == Action::NONE) {
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
    pendingConfirm = Action::NONE;
    input = String();
    command = String();
    screen = Screen::DASHBOARD;
    setNotice(tr("Cancelado.", "Cancelled."));
    return;
  }
  if (prompt != Prompt::NONE) {
    if (key == '\r' || key == '\n') {
      // Both password prompts take an empty line as a deliberate answer: it
      // is how an open network is entered, for the badge's own AP and for a
      // saved network alike. Every other prompt still needs a value.
      if (input.length() || prompt == Prompt::AP_PASSWORD ||
          prompt == Prompt::HOME_PASSWORD) {
        completePrompt();
      } else {
        setNotice(tr("Escribe un valor.", "Enter a value."), true);
      }
      return;
    }
    if (key == 8 || key == 127) {
      if (input.length()) {
        input.remove(input.length() - 1);
        out.print("\b \b");
      }
      return;
    }
    if (key >= 32 && key <= 126 && input.length() < 700) {
      input += key;
      const bool secret = prompt == Prompt::AP_PASSWORD || prompt == Prompt::HOME_PASSWORD;
      out.print(secret ? '*' : key);
    }
    return;
  }

  if (key == '\r' || key == '\n') {
    if (command.length()) {
      out.println();
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
      out.print("\b \b");
    }
    return;
  }
  if (key >= 32 && key <= 126 && command.length() < 32) {
    command += key;
    out.write(key);
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
  if (portalKeyCount) {
    // Copied out first: handleKey() can append to the queue itself, and the
    // buffer must not move under the loop walking it.
    char pending[PORTAL_KEY_CAPACITY];
    const size_t count = portalKeyCount;
    memcpy(pending, portalKeys, count);
    portalKeyCount = 0;
    keyFromPortal = true;
    for (size_t i = 0; i < count; ++i) handleKey(pending[i]);
    keyFromPortal = false;
  }
  // A page that stopped polling has been closed or navigated away from. The
  // ring costs nothing to hold, but capturing into it for a reader that left
  // is pure work, so capture stops until the next poll asks for it again.
  if (mirrorWanted && millis() - mirrorPolledAt > MIRROR_IDLE_MS) {
    mirrorWanted = false;
  }
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

void usbTuiInjectKeys(const String &keys) {
  for (size_t i = 0; i < keys.length() && portalKeyCount < PORTAL_KEY_CAPACITY;
       ++i) {
    portalKeys[portalKeyCount++] = keys.charAt(i);
  }
}

void usbTuiMirrorOpen() {
  const bool wasClosed = !mirrorWanted;
  mirrorWanted = true;
  mirrorPolledAt = millis();
  // The ring holds whatever happened to scroll past, which is not necessarily
  // a whole screen. One redraw on open costs a single frame and leaves the
  // browser showing exactly what the cable shows. A session someone closed
  // with `q` stays closed -- the page shows that, and Enter reopens it, just
  // as it does over the cable.
  if (wasClosed && !rawStream && !sessionClosed) needsRedraw = true;
}

String usbTuiMirrorRead(uint32_t since, uint32_t &sequence,
                        bool &resynchronised) {
  mirrorPolledAt = millis();
  sequence = mirrorSequence;
  const uint32_t oldest = mirrorSequence - mirrorHeld;
  // Behind the ring means the page missed bytes; ahead of it means the badge
  // rebooted under a browser still holding the old cursor. Both are answered
  // the same way: hand back everything held and let the page start over.
  resynchronised = since < oldest || since > mirrorSequence;
  const uint32_t from = resynchronised ? oldest : since;
  String pending;
  if (from >= mirrorSequence) return pending;
  const size_t count = static_cast<size_t>(mirrorSequence - from);
  pending.reserve(count);
  size_t cursor = (mirrorHead + MIRROR_CAPACITY - count) % MIRROR_CAPACITY;
  for (size_t i = 0; i < count; ++i) {
    pending += static_cast<char>(mirror[cursor]);
    cursor = (cursor + 1) % MIRROR_CAPACITY;
  }
  return pending;
}
