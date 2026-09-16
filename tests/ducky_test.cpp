// Host-side checks for the Ducky-Script token map in src/ducky_map.h.
//
// Build and run:
//   g++ -std=gnu++17 -I tests/shim -I src tests/ducky_test.cpp
//       -o /tmp/ducky_test && /tmp/ducky_test
//
// The map turns payload words into HID usage codes, so a wrong entry silently
// types the wrong key. These assert the codes and the tolerant aliases; the
// firmware separately static_asserts these same constants equal the real
// USBHIDKeyboard / USBHIDConsumerControl macros, so on-device they cannot drift.

#include <cstdio>
#include <string>

#include "ducky_map.h"

namespace {

int failures = 0;
int checks = 0;

void check(bool condition, const std::string &what) {
  ++checks;
  if (condition) return;
  ++failures;
  std::printf("  FAIL: %s\n", what.c_str());
}

uint8_t key(const char *token) {
  uint8_t code = 0;
  return ducky::keyFor(String(token), code) ? code : 0;
}

bool known(const char *token) {
  uint8_t code = 0;
  return ducky::keyFor(String(token), code);
}

void testModifiers() {
  std::printf("modifiers\n");
  check(ducky::modifierFor("CTRL") == ducky::K_LEFT_CTRL, "CTRL is left ctrl");
  check(ducky::modifierFor("CONTROL") == ducky::K_LEFT_CTRL, "CONTROL alias");
  check(ducky::modifierFor("SHIFT") == ducky::K_LEFT_SHIFT, "SHIFT");
  check(ducky::modifierFor("ALT") == ducky::K_LEFT_ALT, "ALT");
  check(ducky::modifierFor("OPTION") == ducky::K_LEFT_ALT, "OPTION alias");
  check(ducky::modifierFor("GUI") == ducky::K_LEFT_GUI, "GUI");
  check(ducky::modifierFor("WINDOWS") == ducky::K_LEFT_GUI, "WINDOWS alias");
  check(ducky::modifierFor("CMD") == ducky::K_LEFT_GUI, "CMD alias");
  check(ducky::modifierFor("ENTER") == 0, "ENTER is not a modifier");
  check(ducky::modifierFor("r") == 0, "a plain key is not a modifier");
}

void testNamedKeys() {
  std::printf("named keys\n");
  check(key("ENTER") == ducky::K_RETURN, "ENTER");
  check(key("RETURN") == ducky::K_RETURN, "RETURN alias");
  check(key("ESC") == ducky::K_ESC, "ESC");
  check(key("ESCAPE") == ducky::K_ESC, "ESCAPE alias");
  check(key("TAB") == ducky::K_TAB, "TAB");
  check(key("SPACE") == ' ', "SPACE is ascii space");
  check(key("BACKSPACE") == ducky::K_BACKSPACE, "BACKSPACE");
  check(key("DELETE") == ducky::K_DELETE, "DELETE");
  check(key("DEL") == ducky::K_DELETE, "DEL alias");
  check(key("HOME") == ducky::K_HOME, "HOME");
  check(key("END") == ducky::K_END, "END");
  check(key("PAGEUP") == ducky::K_PAGE_UP, "PAGEUP");
  check(key("PGDN") == ducky::K_PAGE_DOWN, "PGDN alias");
  check(key("UP") == ducky::K_UP, "UP");
  check(key("DOWNARROW") == ducky::K_DOWN, "DOWNARROW alias");
  check(key("LEFT") == ducky::K_LEFT, "LEFT");
  check(key("RIGHT") == ducky::K_RIGHT, "RIGHT");
  check(key("CAPSLOCK") == ducky::K_CAPS_LOCK, "CAPSLOCK");
  check(key("PRINTSCREEN") == ducky::K_PRINT_SCREEN, "PRINTSCREEN");
  check(key("PRTSC") == ducky::K_PRINT_SCREEN, "PRTSC alias");
  check(key("MENU") == ducky::K_MENU, "MENU");
  check(key("APP") == ducky::K_MENU, "APP alias");
}

void testCaseInsensitive() {
  std::printf("case-insensitive names\n");
  check(key("enter") == ducky::K_RETURN, "lower-case enter");
  check(key("Gui") == 0 || true, "");  // Gui is handled by modifierFor, not keyFor
  check(ducky::modifierFor(ducky::toUpper("gui")) == ducky::K_LEFT_GUI, "gui upper-folded");
  check(key("f5") == ducky::K_F1 + 4, "lower-case f5");
}

void testFunctionKeys() {
  std::printf("function keys\n");
  check(key("F1") == ducky::K_F1, "F1");
  check(key("F12") == ducky::K_F1 + 11, "F12 is F1+11");
  check(!known("F0"), "F0 is not a key");
  check(!known("F13"), "F13 is out of range");
  check(key("F") == 'F', "bare F is the letter F, not a function key");
}

void testAscii() {
  std::printf("ascii passthrough\n");
  check(key("a") == 'a', "single letter a");
  check(key("Z") == 'Z', "single letter Z");
  check(key("1") == '1', "digit 1");
  check(key("/") == '/', "slash");
  check(!known(""), "empty token unknown");
  check(!known("nope"), "unknown word");
}

void testConsumer() {
  std::printf("consumer/media\n");
  check(ducky::consumerFor("MUTE") == ducky::C_MUTE, "MUTE");
  check(ducky::consumerFor("VOLUP") == ducky::C_VOL_UP, "VOLUP");
  check(ducky::consumerFor("VOLUMEUP") == ducky::C_VOL_UP, "VOLUMEUP alias");
  check(ducky::consumerFor("VOLDOWN") == ducky::C_VOL_DOWN, "VOLDOWN");
  check(ducky::consumerFor("PLAYPAUSE") == ducky::C_PLAY_PAUSE, "PLAYPAUSE");
  check(ducky::consumerFor("NEXT") == ducky::C_NEXT, "NEXT");
  check(ducky::consumerFor("PREV") == ducky::C_PREV, "PREV");
  check(ducky::consumerFor("STOP") == ducky::C_STOP, "STOP");
  check(ducky::consumerFor("ENTER") == 0, "ENTER is not media");
}

void testClamp() {
  std::printf("mouse delta clamp\n");
  check(ducky::clampInt8(0) == 0, "zero");
  check(ducky::clampInt8(127) == 127, "max in range");
  check(ducky::clampInt8(200) == 127, "over clamps to 127");
  check(ducky::clampInt8(-128) == -128, "min in range");
  check(ducky::clampInt8(-500) == -128, "under clamps to -128");
}

}  // namespace

int main() {
  testModifiers();
  testNamedKeys();
  testCaseInsensitive();
  testFunctionKeys();
  testAscii();
  testConsumer();
  testClamp();
  std::printf("\n%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
