#pragma once

#include <Arduino.h>

// BadUSB script runner: executes BadUSB-format Ducky scripts with additional
// commands (HOLD, RELEASE, ALTCHAR, ALTSTRING, MEDIA, SYSRQ, GLOBE).
// Requires TinyUSB / USB-OTG mode (ARDUINO_USB_MODE=0).

// Configures whether this boot reserves HID interface for BadUSB.
void usbBadUSBConfigure(bool enabled);

// Registers the keyboard/mouse/consumer devices. Must be called from setup()
// before USB.begin().
void usbBadUSBBegin();

// Advances a running payload one atomic step. Never blocks; call every loop().
void usbBadUSBService();

// True while a payload is executing.
bool usbBadUSBBusy();

// Runs the stored payload /payloads/<name>.badusb. Returns false with a
// human-readable reason in error.
bool usbBadUSBRunPayload(const String &name, String &error);

// Runs an inline BadUSB script.
bool usbBadUSBRunScript(const String &script, String &error);

// Aborts a running payload and releases every held key and mouse button.
void usbBadUSBStop();

// Host keyboard's lock LEDs.
uint8_t usbBadUSBHostLeds();

// "A USB host is present" hint.
bool usbBadUSBHostSeen();

// Concise status line for the UI.
String usbBadUSBStatusLine();

// ---- BadUSB Payload storage (LittleFS) ----
constexpr size_t USB_BADUSB_MAX_PAYLOAD_BYTES = 8192;
constexpr size_t USB_BADUSB_MAX_NAME_LENGTH = 32;

uint16_t usbBadUSBPayloadCount();
String usbBadUSBPayloadNameAt(uint16_t index);
bool usbBadUSBPayloadExists(const String &name);
bool usbBadUSBReadPayload(const String &name, String &outScript);
bool usbBadUSBSavePayload(const String &name, const String &script, String &error);
bool usbBadUSBDeletePayload(const String &name, String &error);
bool usbBadUSBValidPayloadName(const String &name);

// LED bitmap bits, matching HID boot-keyboard output report.
constexpr uint8_t USB_BADUSB_LED_NUMLOCK = 0x01;
constexpr uint8_t USB_BADUSB_LED_CAPSLOCK = 0x02;
constexpr uint8_t USB_BADUSB_LED_SCROLLLOCK = 0x04;
