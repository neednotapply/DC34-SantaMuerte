#pragma once

#include <Arduino.h>

struct UsbDriveState {
  // The mass-storage interface is registered and enumerating.
  bool available;
  // The snapshot exists, so the drive is offering a medium a host can mount.
  // False for the first seconds of every boot, while LittleFS is mounted and
  // the rings are walked.
  bool mediaPresent;
  uint16_t noteCount;
  uint8_t scriptCount;
  uint8_t badusbScriptCount;
  uint16_t tagCount;
};

// Register the virtual read-only mass-storage interface before USB.begin().
// The interface comes up with no medium in it.
void usbDriveConfigure(bool enabled);
// Builds the snapshot of what the badge is holding and presents it as the
// drive's medium. Call once the filesystem, the board and the log are up.
void usbDriveRefresh();
// True for a short window after a host reads the virtual disk.
bool usbDriveReadActive();
UsbDriveState getUsbDriveState();
