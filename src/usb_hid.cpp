#include "usb_hid.h"

#include <LittleFS.h>

#include "usb_tui.h"

#include "ducky_map.h"
#include "usb_console.h"

// ===========================================================================
// Payload storage on LittleFS. Independent of the USB mode -- payloads are just
// small text files, so authoring them works even in a JTAG-only build.
// ===========================================================================
namespace {

constexpr char PAYLOAD_DIR[] = "/payloads";

String payloadPath(const String &name) {
  String path = PAYLOAD_DIR;
  path += '/';
  path += name;
  path += ".txt";
  return path;
}

// LittleFS::openNextFile() returns either a bare name or a full path depending
// on the core; reduce whichever to the payload's display name (no dir, no .txt).
// /payloads also holds .badusb files (usb_badusb.cpp's own payloads) -- those
// are deliberately rejected here so they never surface as a DuckyScript name.
String stemFromEntry(const String &raw) {
  int slash = raw.lastIndexOf('/');
  String stem = slash >= 0 ? raw.substring(slash + 1) : raw;
  if (!stem.endsWith(".txt")) return String();
  stem.remove(stem.length() - 4);
  return stem;
}

// Fills names[] (up to cap) with the stored payload stems, sorted
// case-insensitively, and returns how many there are.
uint8_t collectPayloadNames(String *names, uint8_t cap) {
  uint8_t count = 0;
  File dir = LittleFS.open(PAYLOAD_DIR);
  if (!dir || !dir.isDirectory()) return 0;
  for (File entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
    if (entry.isDirectory()) continue;
    const String stem = stemFromEntry(String(entry.name()));
    if (stem.isEmpty()) continue;
    if (count < cap) {
      // Insertion sort keeps the list ordered without a second pass.
      uint8_t i = count;
      while (i > 0 && strcasecmp(names[i - 1].c_str(), stem.c_str()) > 0) {
        names[i] = names[i - 1];
        --i;
      }
      names[i] = stem;
    }
    ++count;
  }
  return count < cap ? count : cap;
}

}  // namespace

bool usbHidValidPayloadName(const String &name) {
  if (name.isEmpty() || name.length() > USB_HID_MAX_NAME_LENGTH) return false;
  if (name[0] == '.') return false;  // no hidden files, no "." or ".."
  bool sawReal = false;
  for (size_t i = 0; i < name.length(); ++i) {
    const char c = name[i];
    const bool ok = isalnum(static_cast<unsigned char>(c)) || c == '_' ||
                    c == '-' || c == '.' || c == ' ';
    if (!ok) return false;
    if (c != '.' && c != ' ') sawReal = true;
  }
  return sawReal;
}

void usbHidBeginStorage() {
  if (!LittleFS.begin(false) && !LittleFS.begin(true)) return;
  if (!LittleFS.exists(PAYLOAD_DIR)) LittleFS.mkdir(PAYLOAD_DIR);

  // Seed a few illustrative, deliberately tame payloads on first boot only, so
  // the feature is discoverable without shipping anything weaponised. A user
  // deleting them all is respected -- seeding keys off an empty directory.
  if (usbHidPayloadCount() > 0) return;

  String error;
  usbHidSavePayload("hello",
                    "REM A harmless demo: types one line and presses Enter.\n"
                    "DEFAULTDELAY 20\n"
                    "STRINGLN Hello from the Santa Muerte badge.\n",
                    error);
  usbHidSavePayload("jiggle",
                    "REM Anti-idle mouse nudge; repeats a small square.\n"
                    "MOUSEMOVE 10 0\n"
                    "DELAY 150\n"
                    "MOUSEMOVE 0 10\n"
                    "DELAY 150\n"
                    "MOUSEMOVE -10 0\n"
                    "DELAY 150\n"
                    "MOUSEMOVE 0 -10\n"
                    "REPEAT 3\n",
                    error);
  usbHidSavePayload("on-capslock",
                    "REM Waits for you to toggle Caps Lock on the host, then\n"
                    "REM types -- a demo of the keyboard LED return channel.\n"
                    "LEDWAIT CAPS\n"
                    "STRINGLN Caps Lock seen; the badge was listening.\n",
                    error);
  usbHidSavePayload("win-run-echo",
                    "REM Windows only. Opens Run and echoes a line in cmd.\n"
                    "GUI r\n"
                    "DELAY 500\n"
                    "STRINGLN cmd\n"
                    "DELAY 800\n"
                    "STRINGLN echo Santa Muerte was here\n",
                    error);
}

uint8_t usbHidPayloadCount() {
  uint8_t count = 0;
  File dir = LittleFS.open(PAYLOAD_DIR);
  if (!dir || !dir.isDirectory()) return 0;
  for (File entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
    if (!entry.isDirectory() && !stemFromEntry(String(entry.name())).isEmpty()) {
      ++count;
    }
  }
  return count;
}

String usbHidPayloadNameAt(uint8_t index) {
  String names[USB_HID_MAX_PAYLOADS];
  const uint8_t count = collectPayloadNames(names, USB_HID_MAX_PAYLOADS);
  return index < count ? names[index] : String();
}

bool usbHidPayloadExists(const String &name) {
  return usbHidValidPayloadName(name) && LittleFS.exists(payloadPath(name));
}

bool usbHidReadPayload(const String &name, String &outScript) {
  outScript = String();
  if (!usbHidPayloadExists(name)) return false;
  File file = LittleFS.open(payloadPath(name), "r");
  if (!file) return false;
  outScript.reserve(file.size() + 1);
  while (file.available() && outScript.length() < USB_HID_MAX_PAYLOAD_BYTES) {
    outScript += static_cast<char>(file.read());
  }
  file.close();
  return true;
}

bool usbHidSavePayload(const String &name, const String &script, String &error) {
  if (!usbHidValidPayloadName(name)) {
    error = "Bad name. Use letters, digits, spaces, - _ . only.";
    return false;
  }
  if (script.length() > USB_HID_MAX_PAYLOAD_BYTES) {
    error = "Payload too large (max " + String(USB_HID_MAX_PAYLOAD_BYTES) + " bytes).";
    return false;
  }
  if (!LittleFS.exists(payloadPath(name)) && usbHidPayloadCount() >= USB_HID_MAX_PAYLOADS) {
    error = "Too many payloads (max " + String(USB_HID_MAX_PAYLOADS) + "). Delete one first.";
    return false;
  }
  if (!LittleFS.exists(PAYLOAD_DIR)) LittleFS.mkdir(PAYLOAD_DIR);
  File file = LittleFS.open(payloadPath(name), "w");
  if (!file) {
    error = "Could not open the payload for writing.";
    return false;
  }
  const size_t written = file.print(script);
  file.close();
  if (written != script.length()) {
    error = "Short write; the filesystem may be full.";
    return false;
  }
  return true;
}

bool usbHidDeletePayload(const String &name, String &error) {
  if (!usbHidPayloadExists(name)) {
    error = "No such payload.";
    return false;
  }
  if (!LittleFS.remove(payloadPath(name))) {
    error = "Could not delete the payload.";
    return false;
  }
  return true;
}

// ===========================================================================
// HID output. Only the TinyUSB / USB-OTG build can present HID; the JTAG build
// keeps the same API as inert stubs so the firmware always compiles.
// ===========================================================================
#if ARDUINO_USB_MODE  // 1 == Hardware CDC + JTAG: no HID peripheral available.

void usbHidConfigure(bool, UsbHidReportSet) {}
void usbHidBegin() {
  usbHidBeginStorage();
  usbTuiLog("HID", "USB HID needs OTG mode; not available in this build");
}
void *usbHidKeyboardHandle() { return nullptr; }
void *usbHidMouseHandle() { return nullptr; }
void *usbHidConsumerControlHandle() { return nullptr; }
void *usbHidSystemControlHandle() { return nullptr; }
void usbHidService() {}
bool usbHidBusy() { return false; }
bool usbHidRunPayload(const String &, String &error) {
  error = "HID needs USB-OTG mode (ARDUINO_USB_MODE=0).";
  return false;
}
bool usbHidRunScript(const String &, String &error) {
  error = "HID needs USB-OTG mode (ARDUINO_USB_MODE=0).";
  return false;
}
void usbHidStop() {}
uint8_t usbHidHostLeds() { return 0; }
bool usbHidHostSeen() { return false; }
String usbHidStatusLine() { return String("HID off (JTAG build)"); }
bool usbHidRunControl(UsbControlAction, String &error) {
  error = "Los controles del host necesitan modo OTG (ARDUINO_USB_MODE=0).";
  return false;
}

#else  // ---- TinyUSB / USB-OTG: full composite HID implementation ----

#include "USB.h"
#include "USBHIDConsumerControl.h"
#include "USBHIDKeyboard.h"
#include "USBHIDMouse.h"
#include "USBHIDSystemControl.h"

namespace {

// These used to be static objects, which made HID occupy an interface before
// NVS had told us which USB profile to expose. Construct them only for Drive
// boots, and crucially before USB.begin() builds the composite descriptor.
USBHIDKeyboard *keyboard = nullptr;
USBHIDMouse *mouse = nullptr;
USBHIDConsumerControl *consumer = nullptr;
USBHIDSystemControl *systemControl = nullptr;
bool hidConfigured = false;

volatile uint8_t hostLeds = 0;
volatile bool sawHostReport = false;

// The OS pushes the keyboard's lock-LED state here whenever it changes -- the
// return channel that lets a payload react to the machine it is plugged into.
void onKeyboardLed(void *, esp_event_base_t, int32_t, void *eventData) {
  if (!eventData) return;
  auto *data = static_cast<arduino_usb_hid_keyboard_event_data_t *>(eventData);
  hostLeds = data->leds;
  sawHostReport = true;
}

// ---- Ducky-Script runner state ----
enum class RunState : uint8_t { IDLE, RUNNING, WAIT_LED };

constexpr uint16_t TYPE_CHAR_MS = 4;      // paced so target OSes keep up
constexpr uint16_t DEFAULT_DELAY_MS = 5;  // between commands until DEFAULTDELAY
constexpr uint16_t MAX_REPEAT = 10000;

RunState runState = RunState::IDLE;
String script;
size_t cursor = 0;
uint32_t nextAt = 0;
uint16_t defaultDelayMs = DEFAULT_DELAY_MS;
String runName;

String prevLine;        // last real command, for REPEAT
String repeatLine;      // the line currently being repeated
uint16_t repeatLeft = 0;

bool typing = false;    // mid-STRING
String typeText;
size_t typeIdx = 0;
bool typeTrailingEnter = false;

uint8_t ledWaitMask = 0;
uint8_t ledWaitBaseline = 0;

uint16_t linesTotal = 0;
uint16_t linesDone = 0;

void schedule(uint32_t inMs) { nextAt = millis() + inMs; }

bool nextLine(String &out) {
  if (cursor >= script.length()) return false;
  const int nl = script.indexOf('\n', cursor);
  if (nl < 0) {
    out = script.substring(cursor);
    cursor = script.length();
  } else {
    out = script.substring(cursor, nl);
    cursor = nl + 1;
  }
  out.replace("\r", "");
  ++linesDone;
  return true;
}

// Pin ducky_map.h's pure codes to the library's real usage codes so the two can
// never drift; tests/ducky_test.cpp exercises that map on the host.
static_assert(ducky::K_LEFT_CTRL == KEY_LEFT_CTRL && ducky::K_LEFT_SHIFT == KEY_LEFT_SHIFT &&
                  ducky::K_LEFT_ALT == KEY_LEFT_ALT && ducky::K_LEFT_GUI == KEY_LEFT_GUI,
              "modifier codes drifted from USBHIDKeyboard");
static_assert(ducky::K_RETURN == KEY_RETURN && ducky::K_ESC == KEY_ESC &&
                  ducky::K_BACKSPACE == KEY_BACKSPACE && ducky::K_TAB == KEY_TAB &&
                  ducky::K_CAPS_LOCK == KEY_CAPS_LOCK && ducky::K_F1 == KEY_F1,
              "key codes drifted from USBHIDKeyboard");
static_assert(ducky::K_INSERT == KEY_INSERT && ducky::K_HOME == KEY_HOME &&
                  ducky::K_PAGE_UP == KEY_PAGE_UP && ducky::K_DELETE == KEY_DELETE &&
                  ducky::K_END == KEY_END && ducky::K_PAGE_DOWN == KEY_PAGE_DOWN,
              "navigation key codes drifted from USBHIDKeyboard");
static_assert(ducky::K_RIGHT == KEY_RIGHT_ARROW && ducky::K_LEFT == KEY_LEFT_ARROW &&
                  ducky::K_DOWN == KEY_DOWN_ARROW && ducky::K_UP == KEY_UP_ARROW,
              "arrow key codes drifted from USBHIDKeyboard");
static_assert(ducky::C_PLAY_PAUSE == CONSUMER_CONTROL_PLAY_PAUSE &&
                  ducky::C_NEXT == CONSUMER_CONTROL_SCAN_NEXT &&
                  ducky::C_PREV == CONSUMER_CONTROL_SCAN_PREVIOUS &&
                  ducky::C_STOP == CONSUMER_CONTROL_STOP && ducky::C_MUTE == CONSUMER_CONTROL_MUTE &&
                  ducky::C_VOL_UP == CONSUMER_CONTROL_VOLUME_INCREMENT &&
                  ducky::C_VOL_DOWN == CONSUMER_CONTROL_VOLUME_DECREMENT,
              "consumer codes drifted from USBHIDConsumerControl");

// Resolves a non-modifier key token. Returns false for an unknown token. A
// single printable character is passed through as its ASCII value, which
// USBHIDKeyboard::press() maps via its keymap.
// Splits a line into whitespace-separated tokens (up to cap).
uint8_t tokenize(const String &line, String *tokens, uint8_t cap) {
  uint8_t n = 0;
  size_t i = 0;
  while (i < line.length() && n < cap) {
    while (i < line.length() && line[i] == ' ') ++i;
    if (i >= line.length()) break;
    size_t start = i;
    while (i < line.length() && line[i] != ' ') ++i;
    tokens[n++] = line.substring(start, i);
  }
  return n;
}

void pressChord(const String &line) {
  String tokens[8];
  const uint8_t n = tokenize(line, tokens, 8);
  bool pressedAny = false;
  for (uint8_t i = 0; i < n; ++i) {
    String u = tokens[i];
    u.toUpperCase();
    const uint8_t mod = ducky::modifierFor(u);
    if (mod) {
      keyboard->press(mod);
      pressedAny = true;
      continue;
    }
    uint8_t code = 0;
    if (ducky::keyFor(tokens[i], code)) {
      keyboard->press(code);
      pressedAny = true;
    }
  }
  if (pressedAny) {
    delay(6);  // let the host register the chord before it lifts
    keyboard->releaseAll();
  }
}

bool tapConsumer(uint16_t code) {
  if (!consumer) return false;
  consumer->press(code);
  delay(6);
  consumer->release();
  return true;
}

bool tapSystem(uint8_t code) {
  if (!systemControl) return false;
  if (systemControl->press(code) == 0) return false;
  delay(6);
  return systemControl->release() != 0;
}

void finish(const char *why) {
  keyboard->releaseAll();
  if (mouse) mouse->release(MOUSE_ALL);
  if (consumer) consumer->release();
  runState = RunState::IDLE;
  typing = false;
  repeatLeft = 0;
  script = String();
  usbTuiLog("HID", String("payload ") + why);
}

// Splits the command word off a line; rest is everything after the first space
// (verbatim, for STRING). cmd is upper-cased for matching.
void splitCommand(const String &line, String &cmd, String &rest) {
  const int sp = line.indexOf(' ');
  if (sp < 0) {
    cmd = line;
    rest = String();
  } else {
    cmd = line.substring(0, sp);
    rest = line.substring(sp + 1);
  }
  cmd.toUpperCase();
}

void executeNextLine() {
  String line;
  if (repeatLeft > 0) {
    line = repeatLine;
    --repeatLeft;
  } else if (!nextLine(line)) {
    finish("done");
    return;
  }

  line.trim();
  if (line.isEmpty()) {
    schedule(0);
    return;
  }

  String cmd, rest;
  splitCommand(line, cmd, rest);

  if (cmd == "REM") {
    schedule(0);
    return;
  }
  if (cmd == "STRING") {
    typeText = rest;
    typeIdx = 0;
    typeTrailingEnter = false;
    typing = true;
    prevLine = line;
    schedule(0);
    return;
  }
  if (cmd == "STRINGLN") {
    typeText = rest;
    typeIdx = 0;
    typeTrailingEnter = true;
    typing = true;
    prevLine = line;
    schedule(0);
    return;
  }
  if (cmd == "DELAY") {
    schedule(static_cast<uint32_t>(rest.toInt()));
    return;
  }
  if (cmd == "DEFAULTDELAY" || cmd == "DEFAULT_DELAY") {
    defaultDelayMs = static_cast<uint16_t>(rest.toInt());
    schedule(0);
    return;
  }
  if (cmd == "REPEAT") {
    long n = rest.toInt();
    if (n < 0) n = 0;
    if (n > MAX_REPEAT) n = MAX_REPEAT;
    repeatLine = prevLine;
    repeatLeft = static_cast<uint16_t>(n);
    schedule(0);
    return;
  }
  if (cmd == "LEDWAIT") {
    String which = rest;
    which.toUpperCase();
    ledWaitMask = which.startsWith("NUM")    ? USB_HID_LED_NUMLOCK
                  : which.startsWith("SCROLL") ? USB_HID_LED_SCROLLLOCK
                                               : USB_HID_LED_CAPSLOCK;
    ledWaitBaseline = hostLeds;
    prevLine = line;
    runState = RunState::WAIT_LED;
    schedule(0);
    return;
  }
  if (cmd == "MOUSEMOVE") {
    if (!mouse) { finish("no mouse (keyboard-only USB identity)"); return; }
    String t[3];
    const uint8_t n = tokenize(rest, t, 3);
    const int8_t dx = ducky::clampInt8(n > 0 ? t[0].toInt() : 0);
    const int8_t dy = ducky::clampInt8(n > 1 ? t[1].toInt() : 0);
    mouse->move(dx, dy, 0, 0);
    prevLine = line;
    schedule(defaultDelayMs);
    return;
  }
  if (cmd == "MOUSESCROLL") {
    if (!mouse) { finish("no mouse (keyboard-only USB identity)"); return; }
    mouse->move(0, 0, ducky::clampInt8(rest.toInt()), 0);
    prevLine = line;
    schedule(defaultDelayMs);
    return;
  }
  if (cmd == "MOUSECLICK") {
    if (!mouse) { finish("no mouse (keyboard-only USB identity)"); return; }
    String b = rest;
    b.toUpperCase();
    const uint8_t button = b.startsWith("RIGHT")    ? MOUSE_RIGHT
                           : b.startsWith("MIDDLE") ? MOUSE_MIDDLE
                                                    : MOUSE_LEFT;
    mouse->click(button);
    prevLine = line;
    schedule(defaultDelayMs);
    return;
  }
  const uint16_t consumerUsage = ducky::consumerFor(cmd);
  if (consumerUsage && rest.isEmpty()) {
    if (!tapConsumer(consumerUsage)) { finish("no consumer control (keyboard-only USB identity)"); return; }
    prevLine = line;
    schedule(defaultDelayMs);
    return;
  }

  // Anything else is a key or a modifier chord (ENTER, GUI r, CTRL ALT DELETE).
  pressChord(line);
  prevLine = line;
  schedule(defaultDelayMs);
}

bool startRun(const String &newScript, const String &name, String &error) {
  if (runState != RunState::IDLE) {
    error = "Hay una carga en curso.";
    return false;
  }
  if (newScript.length() == 0) {
    error = "Payload is empty.";
    return false;
  }
  script = newScript;
  cursor = 0;
  defaultDelayMs = DEFAULT_DELAY_MS;
  typing = false;
  typeTrailingEnter = false;
  repeatLeft = 0;
  prevLine = String();
  runName = name;
  linesDone = 0;
  linesTotal = 1;
  for (size_t i = 0; i < script.length(); ++i) {
    if (script[i] == '\n') ++linesTotal;
  }
  keyboard->releaseAll();
  runState = RunState::RUNNING;
  schedule(0);
  usbTuiLog("HID", String("running ") + name);
  return true;
}

}  // namespace

void usbHidConfigure(bool enabled, UsbHidReportSet reportSet) {
  hidConfigured = enabled;
  if (!enabled) return;

  // USB has not started yet. These constructors reserve their report types so
  // the subsequent USB.begin() emits the Drive profile's HID interface.
  // KEYBOARD_ONLY skips the rest: a smaller composite descriptor, and a host
  // sees a device with no mouse or consumer-control usage at all -- the same
  // fingerprint-reduction reasoning as a custom USB identity.
  keyboard = new USBHIDKeyboard();
  if (reportSet == UsbHidReportSet::FULL) {
    mouse = new USBHIDMouse();
    consumer = new USBHIDConsumerControl();
    systemControl = new USBHIDSystemControl();
  }
}

// usb_badusb.cpp's independent script interpreter drives these same objects
// rather than constructing its own -- see the comment on the declarations in
// usb_hid.h for why a second HID device is never registered.
void *usbHidKeyboardHandle() { return keyboard; }
void *usbHidMouseHandle() { return mouse; }
void *usbHidConsumerControlHandle() { return consumer; }
void *usbHidSystemControlHandle() { return systemControl; }

void usbHidBegin() {
  usbHidBeginStorage();

  if (!hidConfigured) {
    usbTuiLog("HID", "off in WiFi Tethering profile");
    return;
  }

  keyboard->begin();
  if (mouse) mouse->begin();
  if (consumer) consumer->begin();
  if (systemControl) systemControl->begin();
  keyboard->onEvent(ARDUINO_USB_HID_KEYBOARD_LED_EVENT, onKeyboardLed);

  usbTuiLog("HID", mouse ? "CDC + keyboard/mouse/consumer prepared"
                         : "CDC + keyboard prepared (keyboard-only USB identity)");
}

void usbHidService() {
  if (!hidConfigured) return;
  if (runState == RunState::IDLE) return;
  if (static_cast<int32_t>(millis() - nextAt) < 0) return;

  if (runState == RunState::WAIT_LED) {
    if (((hostLeds ^ ledWaitBaseline) & ledWaitMask) == 0) {
      schedule(20);  // no toggle yet; re-poll shortly
      return;
    }
    runState = RunState::RUNNING;
    schedule(defaultDelayMs);
    return;
  }

  if (typing) {
    if (typeIdx < typeText.length()) {
      keyboard->write(static_cast<uint8_t>(typeText[typeIdx++]));
      schedule(TYPE_CHAR_MS);
      return;
    }
    typing = false;
    if (typeTrailingEnter) {
      keyboard->write(KEY_RETURN);
      typeTrailingEnter = false;
    }
    typeText = String();
    schedule(defaultDelayMs);
    return;
  }

  executeNextLine();
}

bool usbHidBusy() { return hidConfigured && runState != RunState::IDLE; }

bool usbHidRunScript(const String &scriptText, String &error) {
  if (!hidConfigured) {
    error = "HID controls are unavailable in WiFi Tethering mode.";
    return false;
  }
  return startRun(scriptText, "inline", error);
}

bool usbHidRunPayload(const String &name, String &error) {
  if (!hidConfigured) {
    error = "HID controls are unavailable in WiFi Tethering mode.";
    return false;
  }
  String body;
  if (!usbHidReadPayload(name, body)) {
    error = "No such payload.";
    return false;
  }
  return startRun(body, name, error);
}

void usbHidStop() {
  if (hidConfigured && runState != RunState::IDLE) finish("stopped");
}

uint8_t usbHidHostLeds() { return hostLeds; }

bool usbHidHostSeen() { return sawHostReport || static_cast<bool>(Serial); }

String usbHidStatusLine() {
  if (!hidConfigured) return "off (WiFi Tethering profile)";
  if (runState == RunState::WAIT_LED) return "waiting on host LED (" + runName + ")";
  if (runState == RunState::RUNNING) {
    return "running " + runName + " " + String(linesDone) + "/" + String(linesTotal);
  }
  return "idle";
}

bool usbHidRunControl(UsbControlAction action, String &error) {
  if (!hidConfigured) {
    error = "HID controls are unavailable in WiFi Tethering mode.";
    return false;
  }
  if (runState != RunState::IDLE) {
    error = "A payload is already running.";
    return false;
  }

  bool sent = false;
  switch (action) {
    case UsbControlAction::PLAY_PAUSE:
      sent = tapConsumer(CONSUMER_CONTROL_PLAY_PAUSE); break;
    case UsbControlAction::MUTE:
      sent = tapConsumer(CONSUMER_CONTROL_MUTE); break;
    case UsbControlAction::VOLUME_UP:
      sent = tapConsumer(CONSUMER_CONTROL_VOLUME_INCREMENT); break;
    case UsbControlAction::VOLUME_DOWN:
      sent = tapConsumer(CONSUMER_CONTROL_VOLUME_DECREMENT); break;
    case UsbControlAction::NEXT_TRACK:
      sent = tapConsumer(CONSUMER_CONTROL_SCAN_NEXT); break;
    case UsbControlAction::PREVIOUS_TRACK:
      sent = tapConsumer(CONSUMER_CONTROL_SCAN_PREVIOUS); break;
    case UsbControlAction::PRESENT_NEXT:
      sent = keyboard->write(KEY_RIGHT_ARROW) == 1; break;
    case UsbControlAction::PRESENT_PREVIOUS:
      sent = keyboard->write(KEY_LEFT_ARROW) == 1; break;
    case UsbControlAction::SYSTEM_SLEEP:
      sent = tapSystem(SYSTEM_CONTROL_STANDBY); break;
    case UsbControlAction::SYSTEM_WAKE:
      sent = tapSystem(SYSTEM_CONTROL_WAKE_HOST); break;
    case UsbControlAction::SYSTEM_POWER_OFF:
      sent = tapSystem(SYSTEM_CONTROL_POWER_OFF); break;
    case UsbControlAction::NONE:
    case UsbControlAction::LED_CONTROLS:
    case UsbControlAction::LIMIT:
      error = "Esta acción solo se puede usar al configurar el botón.";
      return false;
  }

  if (!sent) {
    error = "El host USB no aceptó ese control.";
    return false;
  }
  usbTuiLog("HID", String("control ") + usbControlActionKey(action));
  return true;
}

#endif  // ARDUINO_USB_MODE
