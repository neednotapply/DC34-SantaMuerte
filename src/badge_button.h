#pragma once

#include <Arduino.h>

#include "usb_controls.h"

struct UsbButtonState {
  UsbControlAction shortPress;
  UsbControlAction longPress;
};

// The physical BOOT button is owned by main.cpp because LED control lives
// there. These accessors let the portal and serial console configure it without
// duplicating its behavior or persisting a second source of truth.
UsbButtonState getUsbButtonState();
bool setUsbButtonState(UsbControlAction shortPress,
                       UsbControlAction longPress,
                       String &error);

