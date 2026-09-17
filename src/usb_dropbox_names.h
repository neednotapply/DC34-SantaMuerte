#pragma once

#include <Arduino.h>

// Pure filename logic for the writable USB DROP BOX, split out so it carries no
// ESP-IDF flash includes and can be exercised host-side (tests/usb_dropbox_
// names_test.cpp). The rules mirror usbHidValidPayloadName()/usbBadUSBValid-
// PayloadName(): a legal payload stem is letters, digits, '_', '-', '.' and
// space, at most 32 characters, not beginning with '.', and containing at least
// one character that is neither '.' nor space.

// True when a directory entry is a real drop worth importing rather than the
// litter a desktop OS scatters onto any removable volume (hidden files, Apple
// double files, the Windows trash/index folders, Office lock files).
bool dropboxIsImportableFilename(const char *filename);

// Derives a valid payload name from a dropped file's name: strips a trailing
// script extension (.txt/.dd/.ducky/.badusb), replaces any character a payload
// name may not contain with '_', trims leading dots and surrounding spaces, and
// truncates to 32. Returns false (leaving `out` untouched) when nothing usable
// survives -- the caller then skips the file rather than inventing a name.
bool dropboxPayloadName(const char *filename, String &out);
