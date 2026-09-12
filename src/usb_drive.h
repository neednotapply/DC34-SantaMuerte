#pragma once

#include <Arduino.h>

struct UsbDriveState {
  bool available;
  uint16_t noteCount;
  uint8_t scriptCount;
};

// Register the virtual read-only mass-storage interface before USB.begin().
void usbDriveConfigure(bool enabled);
void usbDriveRefresh();
UsbDriveState getUsbDriveState();
