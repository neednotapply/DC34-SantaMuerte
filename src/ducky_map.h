#pragma once

#include <Arduino.h>

// Pure, hardware-independent Ducky-Script token mapping: key names, modifier
// names, and media names to their HID usage codes. Kept free of any USB/Arduino
// device dependency so it compiles on the host and is unit-tested in
// tests/ducky_test.cpp. usb_hid.cpp static_asserts every constant below against
// the real USBHIDKeyboard / USBHIDConsumerControl macros, so these can never
// silently drift from the library the firmware actually presses.
namespace ducky {

// Keyboard usage codes (mirror of Arduino KEY_*). ASCII keys are passed through
// as their character value, which USBHIDKeyboard::press() maps via its keymap.
constexpr uint8_t K_LEFT_CTRL = 0x80;
constexpr uint8_t K_LEFT_SHIFT = 0x81;
constexpr uint8_t K_LEFT_ALT = 0x82;
constexpr uint8_t K_LEFT_GUI = 0x83;
constexpr uint8_t K_RETURN = 0xB0;
constexpr uint8_t K_ESC = 0xB1;
constexpr uint8_t K_BACKSPACE = 0xB2;
constexpr uint8_t K_TAB = 0xB3;
constexpr uint8_t K_CAPS_LOCK = 0xC1;
constexpr uint8_t K_F1 = 0xC2;
constexpr uint8_t K_INSERT = 0xD1;
constexpr uint8_t K_HOME = 0xD2;
constexpr uint8_t K_PAGE_UP = 0xD3;
constexpr uint8_t K_DELETE = 0xD4;
constexpr uint8_t K_END = 0xD5;
constexpr uint8_t K_PAGE_DOWN = 0xD6;
constexpr uint8_t K_RIGHT = 0xD7;
constexpr uint8_t K_LEFT = 0xD8;
constexpr uint8_t K_DOWN = 0xD9;
constexpr uint8_t K_UP = 0xDA;

// Consumer-control usage codes (mirror of Arduino CONSUMER_CONTROL_*).
constexpr uint16_t C_PLAY_PAUSE = 0x00CD;
constexpr uint16_t C_NEXT = 0x00B5;
constexpr uint16_t C_PREV = 0x00B6;
constexpr uint16_t C_STOP = 0x00B7;
constexpr uint16_t C_MUTE = 0x00E2;
constexpr uint16_t C_VOL_UP = 0x00E9;
constexpr uint16_t C_VOL_DOWN = 0x00EA;

inline String toUpper(const String &value) {
  String upper = value;
  upper.toUpperCase();
  return upper;
}

// The KEY_LEFT_* code for a modifier token (already upper-cased), or 0.
inline uint8_t modifierFor(const String &upper) {
  if (upper == "CTRL" || upper == "CONTROL") return K_LEFT_CTRL;
  if (upper == "SHIFT") return K_LEFT_SHIFT;
  if (upper == "ALT" || upper == "OPTION") return K_LEFT_ALT;
  if (upper == "GUI" || upper == "WINDOWS" || upper == "WIN" ||
      upper == "COMMAND" || upper == "CMD" || upper == "META" || upper == "SUPER")
    return K_LEFT_GUI;
  return 0;
}

// Resolves a non-modifier key token to a usage code. A single printable
// character passes through as its ASCII value. Returns false for an unknown
// token.
inline bool keyFor(const String &token, uint8_t &code) {
  if (token.length() == 1) {
    const char c = token[0];
    if (c >= 32 && c <= 126) {
      code = static_cast<uint8_t>(c);
      return true;
    }
  }
  const String u = toUpper(token);
  if (u == "ENTER" || u == "RETURN") { code = K_RETURN; return true; }
  if (u == "TAB") { code = K_TAB; return true; }
  if (u == "ESC" || u == "ESCAPE") { code = K_ESC; return true; }
  if (u == "SPACE") { code = ' '; return true; }
  if (u == "BACKSPACE" || u == "BKSP") { code = K_BACKSPACE; return true; }
  if (u == "DELETE" || u == "DEL") { code = K_DELETE; return true; }
  if (u == "INSERT" || u == "INS") { code = K_INSERT; return true; }
  if (u == "HOME") { code = K_HOME; return true; }
  if (u == "END") { code = K_END; return true; }
  if (u == "PAGEUP" || u == "PGUP") { code = K_PAGE_UP; return true; }
  if (u == "PAGEDOWN" || u == "PGDN") { code = K_PAGE_DOWN; return true; }
  if (u == "UP" || u == "UPARROW") { code = K_UP; return true; }
  if (u == "DOWN" || u == "DOWNARROW") { code = K_DOWN; return true; }
  if (u == "LEFT" || u == "LEFTARROW") { code = K_LEFT; return true; }
  if (u == "RIGHT" || u == "RIGHTARROW") { code = K_RIGHT; return true; }
  if (u == "CAPSLOCK") { code = K_CAPS_LOCK; return true; }
  if (u.length() >= 2 && u[0] == 'F') {
    const int n = u.substring(1).toInt();
    if (n >= 1 && n <= 12) {
      code = static_cast<uint8_t>(K_F1 + (n - 1));
      return true;
    }
  }
  return false;
}

// A consumer/media code for a standalone token (already upper-cased), or 0.
inline uint16_t consumerFor(const String &upper) {
  if (upper == "MUTE") return C_MUTE;
  if (upper == "VOLUP" || upper == "VOLUMEUP") return C_VOL_UP;
  if (upper == "VOLDOWN" || upper == "VOLUMEDOWN") return C_VOL_DOWN;
  if (upper == "PLAY" || upper == "PLAYPAUSE" || upper == "PAUSE") return C_PLAY_PAUSE;
  if (upper == "NEXT" || upper == "NEXTTRACK") return C_NEXT;
  if (upper == "PREV" || upper == "PREVTRACK" || upper == "PREVIOUS") return C_PREV;
  if (upper == "STOP") return C_STOP;
  return 0;
}

// Clamps a mouse delta to the signed byte an HID mouse report carries.
inline int clampInt8(long value) {
  if (value > 127) return 127;
  if (value < -128) return -128;
  return static_cast<int>(value);
}

}  // namespace ducky
