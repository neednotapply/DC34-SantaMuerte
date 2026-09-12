#pragma once

#include <Arduino.h>

// NCM is a third function on the existing TinyUSB device.  It deliberately
// does not replace CDC serial or HID: the host sees all three at once.
struct UsbNetworkState {
  bool available;
  bool enabled;
  bool stationConnected;
  bool linkUp;
  uint32_t sentFrames;
  uint32_t receivedFrames;
  uint32_t droppedFrames;
};

// Must run before USB.begin(). It registers NCM only in the Network profile.
void usbNetworkConfigure(bool enabled);
void usbNetworkBegin();
void usbNetworkService();
void usbNetworkSetStationConnected(bool connected);
bool usbNetworkSetEnabled(bool enabled, String &error);
UsbNetworkState getUsbNetworkState();
