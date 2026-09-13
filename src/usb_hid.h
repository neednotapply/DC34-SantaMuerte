#pragma once

#include <Arduino.h>

#include "usb_controls.h"

// Composite USB-HID subsystem: a rubber-ducky keyboard plus mouse, consumer
// (media), and system controls, riding on the same cable as the USB CDC console. Requires
// TinyUSB / USB-OTG mode (ARDUINO_USB_MODE=0); in the fixed USB-Serial-JTAG
// build every entry point below is a safe no-op.
//
// HID output is inert until a payload is deliberately run from the physical USB
// Altar console. Nothing is ever typed on its own, and the Wi-Fi portal can
// author and store payloads but can never fire one -- keystroke injection stays
// a local, physical-admin act, the same trust boundary the console already has.

// Chooses whether this boot reserves a HID interface. It must run before
// USB.begin(): the Wi-Fi adapter profile intentionally leaves HID out so the
// S3 has endpoints available for CDC serial + NCM.
void usbHidConfigure(bool enabled);

// Registers the keyboard/mouse/consumer devices and wires up their report
// path. Must be called from setup() before USB.begin().
void usbHidBegin();

// Advances a running payload one atomic step. Never blocks; call every loop().
void usbHidService();

// True while a payload is executing (including a parked LEDWAIT).
bool usbHidBusy();

// Runs the stored payload /payloads/<name>. Returns false with a human-readable
// reason in error (unknown name, already running, empty).
bool usbHidRunPayload(const String &name, String &error);

// Runs an inline Ducky script. Used for small built-in actions.
bool usbHidRunScript(const String &script, String &error);

// Aborts a running payload and releases every held key and mouse button.
void usbHidStop();

// The host keyboard's lock LEDs, as the OS last reported them: a real return
// channel from the machine the badge is plugged into. See LED_* below.
uint8_t usbHidHostLeds();

// Best-effort "a USB host is present" hint for the UI. True once the console
// port is open or the host has sent at least one keyboard LED report.
bool usbHidHostSeen();

// LED bitmap bits, matching the HID boot-keyboard output report.
constexpr uint8_t USB_HID_LED_NUMLOCK = 0x01;
constexpr uint8_t USB_HID_LED_CAPSLOCK = 0x02;
constexpr uint8_t USB_HID_LED_SCROLLLOCK = 0x04;

// ---- Payload storage on LittleFS (/payloads), shared by TUI and portal ----
constexpr size_t USB_HID_MAX_PAYLOADS = 16;
constexpr size_t USB_HID_MAX_PAYLOAD_BYTES = 2048;
constexpr size_t USB_HID_MAX_NAME_LENGTH = 32;

// Opens /payloads and writes the built-in seed payloads on first boot. Safe to
// call when LittleFS is unavailable (it then reports zero payloads).
void usbHidBeginStorage();

uint8_t usbHidPayloadCount();

// Name of the nth stored payload, sorted case-insensitively. Empty past the end.
String usbHidPayloadNameAt(uint8_t index);

bool usbHidPayloadExists(const String &name);
bool usbHidReadPayload(const String &name, String &outScript);

// Creates or overwrites a payload. Rejects an invalid name, an over-long body,
// or a new name once USB_HID_MAX_PAYLOADS is reached; error carries the reason.
bool usbHidSavePayload(const String &name, const String &script, String &error);
bool usbHidDeletePayload(const String &name, String &error);

// True when name is a legal payload file stem (letters, digits, - _ . space).
bool usbHidValidPayloadName(const String &name);

// One concise status line for the console and portal, e.g. "idle" or
// "running unlock 4/12".
String usbHidStatusLine();

// Sends one deliberate, immediate standard HID control. This never stores or
// types a script. LED_CONTROLS and NONE are button-only states and are rejected
// here; callers use them to leave the action to the badge itself.
bool usbHidRunControl(UsbControlAction action, String &error);
