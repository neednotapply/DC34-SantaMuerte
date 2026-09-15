#include "usb_badusb.h"

#include <LittleFS.h>

#include "usb_tui.h"
#include "ducky_map.h"
#include "usb_console.h"

namespace {

constexpr char PAYLOAD_DIR[] = "/payloads";

String payloadPath(const String &name) {
  String path = PAYLOAD_DIR;
  path += '/';
  path += name;
  path += ".badusb";
  return path;
}

String stemFromEntry(const String &raw) {
  int slash = raw.lastIndexOf('/');
  String stem = slash >= 0 ? raw.substring(slash + 1) : raw;
  if (stem.endsWith(".badusb")) stem.remove(stem.length() - 7);
  return stem;
}

uint8_t collectPayloadNames(String *names, uint8_t cap) {
  uint8_t count = 0;
  File dir = LittleFS.open(PAYLOAD_DIR);
  if (!dir || !dir.isDirectory()) return 0;
  for (File entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
    if (entry.isDirectory()) continue;
    const String raw = String(entry.name());
    if (!raw.endsWith(".badusb")) continue;
    const String stem = stemFromEntry(raw);
    if (stem.isEmpty()) continue;
    if (count < cap) {
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

bool usbBadUSBValidPayloadName(const String &name) {
  if (name.isEmpty() || name.length() > 32) return false;
  if (name[0] == '.') return false;
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

uint8_t usbBadUSBPayloadCount() {
  uint8_t count = 0;
  File dir = LittleFS.open(PAYLOAD_DIR);
  if (!dir || !dir.isDirectory()) return 0;
  for (File entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
    if (!entry.isDirectory()) {
      const String raw = String(entry.name());
      if (raw.endsWith(".badusb")) ++count;
    }
  }
  return count;
}

String usbBadUSBPayloadNameAt(uint8_t index) {
  String names[16];
  const uint8_t count = collectPayloadNames(names, 16);
  return index < count ? names[index] : String();
}

bool usbBadUSBPayloadExists(const String &name) {
  return usbBadUSBValidPayloadName(name) && LittleFS.exists(payloadPath(name));
}

bool usbBadUSBReadPayload(const String &name, String &outScript) {
  outScript = String();
  if (!usbBadUSBPayloadExists(name)) return false;
  File file = LittleFS.open(payloadPath(name), "r");
  if (!file) return false;
  outScript.reserve(file.size() + 1);
  while (file.available() && outScript.length() < 2048) {
    outScript += static_cast<char>(file.read());
  }
  file.close();
  return true;
}

bool usbBadUSBSavePayload(const String &name, const String &script, String &error) {
  if (!usbBadUSBValidPayloadName(name)) {
    error = "Bad name. Use letters, digits, spaces, - _ . only.";
    return false;
  }
  if (script.length() > 2048) {
    error = "Payload too large (max 2048 bytes).";
    return false;
  }
  if (!LittleFS.exists(payloadPath(name)) && usbBadUSBPayloadCount() >= 16) {
    error = "Too many payloads (max 16). Delete one first.";
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

bool usbBadUSBDeletePayload(const String &name, String &error) {
  if (!usbBadUSBPayloadExists(name)) {
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
// BadUSB script execution
// ===========================================================================
#if ARDUINO_USB_MODE  // 1 == Hardware CDC + JTAG: no HID peripheral available.

void usbBadusb Configure(bool) {}
void usbBadUSBBegin() {
  usbTuiLog("BadUSB", "needs OTG mode; not available in this build");
}
void usbBadUSBService() {}
bool usbBadUSBBusy() { return false; }
bool usbBadUSBRunPayload(const String &, String &error) {
  error = "BadUSB needs USB-OTG mode (ARDUINO_USB_MODE=0).";
  return false;
}
bool usbBadUSBRunScript(const String &, String &error) {
  error = "BadUSB needs USB-OTG mode (ARDUINO_USB_MODE=0).";
  return false;
}
void usbBadUSBStop() {}
uint8_t usbBadUSBHostLeds() { return 0; }
bool usbBadUSBHostSeen() { return false; }
String usbBadUSBStatusLine() { return String("BadUSB off (JTAG build)"); }

#else  // ---- TinyUSB / USB-OTG: full BadUSB implementation ----

#include "USB.h"
#include "USBHIDConsumerControl.h"
#include "USBHIDKeyboard.h"
#include "USBHIDMouse.h"
#include "USBHIDSystemControl.h"

namespace {

USBHIDKeyboard *keyboard = nullptr;
USBHIDMouse *mouse = nullptr;
USBHIDConsumerControl *consumer = nullptr;
USBHIDSystemControl *systemControl = nullptr;
bool badusb_configured = false;

volatile uint8_t hostLeds = 0;
volatile bool sawHostReport = false;

void onKeyboardLed(void *, esp_event_base_t, int32_t, void *eventData) {
  if (!eventData) return;
  auto *data = static_cast<arduino_usb_hid_keyboard_event_data_t *>(eventData);
  hostLeds = data->leds;
  sawHostReport = true;
}

enum class RunState : uint8_t { IDLE, RUNNING };

constexpr uint16_t TYPE_CHAR_MS = 4;
constexpr uint16_t DEFAULT_DELAY_MS = 5;
constexpr uint16_t MAX_REPEAT = 10000;

RunState runState = RunState::IDLE;
String script;
size_t cursor = 0;
uint32_t nextAt = 0;
uint16_t defaultDelayMs = DEFAULT_DELAY_MS;
uint16_t defaultStringDelayMs = TYPE_CHAR_MS;
String runName;

String prevLine;
String repeatLine;
uint16_t repeatLeft = 0;

bool typing = false;
String typeText;
size_t typeIdx = 0;
bool typeTrailingEnter = false;
uint16_t stringDelayMs = 0;

uint16_t linesTotal = 0;
uint16_t linesDone = 0;

// Held keys (HOLD/RELEASE state)
uint8_t heldModifiers = 0;
uint8_t heldKeys[6] = {0, 0, 0, 0, 0, 0};
uint8_t heldKeyCount = 0;

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

void pressChord(const String &line) {
  String tokens[8];
  uint8_t n = 0;
  size_t i = 0;
  while (i < line.length() && n < 8) {
    while (i < line.length() && line[i] == ' ') ++i;
    if (i >= line.length()) break;
    size_t start = i;
    while (i < line.length() && line[i] != ' ') ++i;
    tokens[n++] = line.substring(start, i);
  }

  bool pressedAny = false;
  for (uint8_t j = 0; j < n; ++j) {
    String u = tokens[j];
    u.toUpperCase();
    const uint8_t mod = ducky::modifierFor(u);
    if (mod) {
      keyboard->press(mod);
      pressedAny = true;
      continue;
    }
    uint8_t code = 0;
    if (ducky::keyFor(tokens[j], code)) {
      keyboard->press(code);
      pressedAny = true;
    }
  }
  if (pressedAny) {
    delay(6);
    keyboard->releaseAll();
  }
}

void tapConsumer(uint16_t code) {
  consumer->press(code);
  delay(6);
  consumer->release();
}

void finish(const char *why) {
  keyboard->releaseAll();
  mouse->release(MOUSE_ALL);
  consumer->release();
  heldModifiers = 0;
  heldKeyCount = 0;
  runState = RunState::IDLE;
  typing = false;
  repeatLeft = 0;
  script = String();
  usbTuiLog("BadUSB", String("payload ") + why);
}

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
    stringDelayMs = defaultStringDelayMs;
    typing = true;
    prevLine = line;
    schedule(0);
    return;
  }

  if (cmd == "STRINGLN") {
    typeText = rest;
    typeIdx = 0;
    typeTrailingEnter = true;
    stringDelayMs = defaultStringDelayMs;
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

  if (cmd == "STRINGDELAY" || cmd == "STRING_DELAY") {
    stringDelayMs = static_cast<uint16_t>(rest.toInt());
    schedule(0);
    return;
  }

  if (cmd == "DEFAULTSTRINGDELAY" || cmd == "DEFAULT_STRING_DELAY") {
    defaultStringDelayMs = static_cast<uint16_t>(rest.toInt());
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

  if (cmd == "HOLD") {
    String u = rest;
    u.toUpperCase();
    const uint8_t mod = ducky::modifierFor(u);
    if (mod) {
      heldModifiers |= mod;
      keyboard->press(mod);
    } else {
      uint8_t code = 0;
      if (ducky::keyFor(rest, code) && heldKeyCount < 6) {
        heldKeys[heldKeyCount++] = code;
        keyboard->press(code);
      }
    }
    prevLine = line;
    schedule(defaultDelayMs);
    return;
  }

  if (cmd == "RELEASE") {
    String u = rest;
    u.toUpperCase();
    const uint8_t mod = ducky::modifierFor(u);
    if (mod) {
      heldModifiers &= ~mod;
      keyboard->release(mod);
    } else {
      uint8_t code = 0;
      if (ducky::keyFor(rest, code)) {
        for (uint8_t i = 0; i < heldKeyCount; ++i) {
          if (heldKeys[i] == code) {
            keyboard->release(code);
            for (uint8_t j = i; j < heldKeyCount - 1; ++j) {
              heldKeys[j] = heldKeys[j + 1];
            }
            --heldKeyCount;
            break;
          }
        }
      }
    }
    prevLine = line;
    schedule(defaultDelayMs);
    return;
  }

  if (cmd == "ALTCHAR") {
    uint16_t code = rest.toInt();
    if (code > 0) {
      keyboard->press(KEY_LEFT_ALT);
      delay(2);
      String numStr = String(code);
      for (size_t i = 0; i < numStr.length(); ++i) {
        keyboard->press(numStr[i]);
        delay(10);
        keyboard->releaseAll();
        delay(5);
      }
      keyboard->release(KEY_LEFT_ALT);
    }
    prevLine = line;
    schedule(defaultDelayMs);
    return;
  }

  if (cmd == "ALTSTRING" || cmd == "ALTCODE") {
    keyboard->press(KEY_LEFT_ALT);
    delay(2);
    for (size_t i = 0; i < rest.length(); ++i) {
      keyboard->press(rest[i]);
      delay(10);
      keyboard->releaseAll();
      delay(5);
    }
    keyboard->release(KEY_LEFT_ALT);
    prevLine = line;
    schedule(defaultDelayMs);
    return;
  }

  if (cmd == "MOUSEMOVE") {
    String t[3];
    uint8_t n = 0;
    size_t i = 0;
    while (i < rest.length() && n < 3) {
      while (i < rest.length() && rest[i] == ' ') ++i;
      if (i >= rest.length()) break;
      size_t start = i;
      while (i < rest.length() && rest[i] != ' ') ++i;
      t[n++] = rest.substring(start, i);
    }
    const int8_t dx = ducky::clampInt8(n > 0 ? t[0].toInt() : 0);
    const int8_t dy = ducky::clampInt8(n > 1 ? t[1].toInt() : 0);
    mouse->move(dx, dy, 0, 0);
    prevLine = line;
    schedule(defaultDelayMs);
    return;
  }

  if (cmd == "MOUSESCROLL") {
    mouse->move(0, 0, ducky::clampInt8(rest.toInt()), 0);
    prevLine = line;
    schedule(defaultDelayMs);
    return;
  }

  if (cmd == "MOUSECLICK") {
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

  if (cmd == "MEDIA") {
    const uint16_t cons = ducky::consumerFor(rest);
    if (cons) {
      tapConsumer(cons);
      prevLine = line;
      schedule(defaultDelayMs);
      return;
    }
  }

  pressChord(line);
  prevLine = line;
  schedule(defaultDelayMs);
}

bool startRun(const String &newScript, const String &name, String &error) {
  if (runState != RunState::IDLE) {
    error = "A payload is already running.";
    return false;
  }
  if (newScript.length() == 0) {
    error = "Payload is empty.";
    return false;
  }
  script = newScript;
  cursor = 0;
  defaultDelayMs = DEFAULT_DELAY_MS;
  defaultStringDelayMs = TYPE_CHAR_MS;
  typing = false;
  typeTrailingEnter = false;
  repeatLeft = 0;
  heldModifiers = 0;
  heldKeyCount = 0;
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
  usbTuiLog("BadUSB", String("running ") + name);
  return true;
}

}  // namespace

void usbBadUSBConfigure(bool enabled) {
  badusb_configured = enabled;
  if (!enabled) return;

  keyboard = new USBHIDKeyboard();
  mouse = new USBHIDMouse();
  consumer = new USBHIDConsumerControl();
  systemControl = new USBHIDSystemControl();
}

void usbBadUSBBegin() {
  if (!badusb_configured) {
    usbTuiLog("BadUSB", "off in WiFi Tethering profile");
    return;
  }

  keyboard->begin();
  mouse->begin();
  consumer->begin();
  systemControl->begin();
  keyboard->onEvent(ARDUINO_USB_HID_KEYBOARD_LED_EVENT, onKeyboardLed);

  usbTuiLog("BadUSB", "CDC + keyboard/mouse/consumer prepared");
}

void usbBadUSBService() {
  if (!badusb_configured) return;
  if (runState == RunState::IDLE) return;
  if (static_cast<int32_t>(millis() - nextAt) < 0) return;

  if (typing) {
    if (typeIdx < typeText.length()) {
      keyboard->write(static_cast<uint8_t>(typeText[typeIdx++]));
      schedule(stringDelayMs);
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

bool usbBadUSBBusy() { return badusb_configured && runState != RunState::IDLE; }

bool usbBadUSBRunScript(const String &scriptText, String &error) {
  if (!badusb_configured) {
    error = "BadUSB is unavailable in WiFi Tethering mode.";
    return false;
  }
  return startRun(scriptText, "inline", error);
}

bool usbBadUSBRunPayload(const String &name, String &error) {
  if (!badusb_configured) {
    error = "BadUSB is unavailable in WiFi Tethering mode.";
    return false;
  }
  String body;
  if (!usbBadUSBReadPayload(name, body)) {
    error = "No such payload.";
    return false;
  }
  return startRun(body, name, error);
}

void usbBadUSBStop() {
  if (badusb_configured && runState != RunState::IDLE) finish("stopped");
}

uint8_t usbBadUSBHostLeds() { return hostLeds; }

bool usbBadUSBHostSeen() { return sawHostReport || static_cast<bool>(Serial); }

String usbBadUSBStatusLine() {
  if (!badusb_configured) return "off (WiFi Tethering profile)";
  if (runState == RunState::RUNNING) {
    return "running " + runName + " " + String(linesDone) + "/" + String(linesTotal);
  }
  return "idle";
}

#endif  // ARDUINO_USB_MODE
