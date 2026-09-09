#pragma once

#include <Arduino.h>

// Firmware-resident terminal UI on the USB CDC serial interface.
void usbTuiBegin();
void usbTuiService();

// Subsystems may add concise user-facing diagnostic lines without knowing how
// the active terminal is being rendered.
void usbTuiLog(const char *module, const String &message);
