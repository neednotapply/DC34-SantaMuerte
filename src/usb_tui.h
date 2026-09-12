#pragma once

#include <Arduino.h>

// Firmware-resident terminal UI on the USB CDC serial interface.
void usbTuiBegin();
void usbTuiService();

// Requests one fresh menu after a completed physical action, such as a BOOT
// button LED change. The console never redraws continuously while idle.
void usbTuiRefresh();

// Subsystems may add concise user-facing diagnostic lines without knowing how
// the active terminal is being rendered.
void usbTuiLog(const char *module, const String &message);
