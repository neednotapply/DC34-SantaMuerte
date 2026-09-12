#pragma once

#include <Arduino.h>

// A compact, explicit action vocabulary shared by the USB remote, the physical
// BOOT button, and the serial console. Values are persisted, so append only.
enum class UsbControlAction : uint8_t {
  NONE = 0,
  LED_CONTROLS,
  PLAY_PAUSE,
  MUTE,
  VOLUME_UP,
  VOLUME_DOWN,
  NEXT_TRACK,
  PREVIOUS_TRACK,
  PRESENT_NEXT,
  PRESENT_PREVIOUS,
  SYSTEM_SLEEP,
  SYSTEM_WAKE,
  SYSTEM_POWER_OFF,
  LIMIT,
};

bool isUsbControlAction(UsbControlAction action);
bool usbControlActionFromKey(const String &key, UsbControlAction &action);
const char *usbControlActionKey(UsbControlAction action);

