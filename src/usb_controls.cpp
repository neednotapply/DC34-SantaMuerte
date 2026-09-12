#include "usb_controls.h"

bool isUsbControlAction(UsbControlAction action) {
  return static_cast<uint8_t>(action) <
         static_cast<uint8_t>(UsbControlAction::LIMIT);
}

bool usbControlActionFromKey(const String &key, UsbControlAction &action) {
  String normalized = key;
  normalized.toLowerCase();
  if (normalized == "none") action = UsbControlAction::NONE;
  else if (normalized == "led") action = UsbControlAction::LED_CONTROLS;
  else if (normalized == "play") action = UsbControlAction::PLAY_PAUSE;
  else if (normalized == "mute") action = UsbControlAction::MUTE;
  else if (normalized == "volume-up") action = UsbControlAction::VOLUME_UP;
  else if (normalized == "volume-down") action = UsbControlAction::VOLUME_DOWN;
  else if (normalized == "next") action = UsbControlAction::NEXT_TRACK;
  else if (normalized == "previous") action = UsbControlAction::PREVIOUS_TRACK;
  else if (normalized == "present-next") action = UsbControlAction::PRESENT_NEXT;
  else if (normalized == "present-previous") action = UsbControlAction::PRESENT_PREVIOUS;
  else if (normalized == "sleep") action = UsbControlAction::SYSTEM_SLEEP;
  else if (normalized == "wake") action = UsbControlAction::SYSTEM_WAKE;
  else if (normalized == "power-off") action = UsbControlAction::SYSTEM_POWER_OFF;
  else return false;
  return true;
}

const char *usbControlActionKey(UsbControlAction action) {
  switch (action) {
    case UsbControlAction::NONE: return "none";
    case UsbControlAction::LED_CONTROLS: return "led";
    case UsbControlAction::PLAY_PAUSE: return "play";
    case UsbControlAction::MUTE: return "mute";
    case UsbControlAction::VOLUME_UP: return "volume-up";
    case UsbControlAction::VOLUME_DOWN: return "volume-down";
    case UsbControlAction::NEXT_TRACK: return "next";
    case UsbControlAction::PREVIOUS_TRACK: return "previous";
    case UsbControlAction::PRESENT_NEXT: return "present-next";
    case UsbControlAction::PRESENT_PREVIOUS: return "present-previous";
    case UsbControlAction::SYSTEM_SLEEP: return "sleep";
    case UsbControlAction::SYSTEM_WAKE: return "wake";
    case UsbControlAction::SYSTEM_POWER_OFF: return "power-off";
    case UsbControlAction::LIMIT: break;
  }
  return "none";
}

