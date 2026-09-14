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

// The bridge follows the saved Wi-Fi link; there is nothing to start by hand.
// Selecting the WiFi Tethering profile is itself the instruction to tether, and
// a profile that only tethered after a second, invisible switch looked exactly
// the same whether it was working or sitting idle: no badge access point, and
// a host that sees a network adapter carrying nothing.
void usbNetworkSetStationConnected(bool connected);
UsbNetworkState getUsbNetworkState();
