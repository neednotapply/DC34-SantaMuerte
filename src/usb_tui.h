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

// The portal's Terminal page is a second window onto this same console: one
// session, one screen, shared with whatever is on the USB cable. Keys it posts
// are queued and dispatched from usbTuiService(), and output is captured as a
// byte stream the page replays, so both views stay identical.
void usbTuiInjectKeys(const String &keys);

// Starts (or keeps alive) capture and asks for a fresh frame. Capture stops on
// its own once a page stops polling.
void usbTuiMirrorOpen();

// Returns the bytes printed since `since`. `sequence` comes back as the new
// cursor; `resynchronised` reports that the gap was too large and the caller
// was handed the whole buffer instead.
String usbTuiMirrorRead(uint32_t since, uint32_t &sequence,
                        bool &resynchronised);
