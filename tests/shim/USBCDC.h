// Host stand-in for the TinyUSB CDC serial object. usb_console.h aliases Serial
// to it, so anything the firmware prints during a host test lands on stdout
// through the Arduino shim's own Serial.
#pragma once

#include <Arduino.h>

using USBCDC = SerialShim;
