#pragma once

#include <Arduino.h>
#include <USBCDC.h>

// CDC is created explicitly so the firmware can choose its other USB function
// (NCM or read-only storage) from NVS before calling USB.begin().
extern USBCDC UsbConsole;

#ifdef Serial
#undef Serial
#endif
#define Serial UsbConsole
