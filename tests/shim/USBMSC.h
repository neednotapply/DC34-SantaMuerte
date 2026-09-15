// Host stand-in for the ESP32 USB mass-storage class. The drive only ever asks
// it to remember three callbacks and say that it started, so the shim keeps the
// read callback where a test can reach it and does nothing else.
#pragma once

#include <cstdint>

class USBMSC {
 public:
  using ReadCallback = int32_t (*)(uint32_t, uint32_t, void *, uint32_t);
  using WriteCallback = int32_t (*)(uint32_t, uint32_t, uint8_t *, uint32_t);
  using StartStopCallback = bool (*)(uint8_t, bool, bool);

  void vendorID(const char *) {}
  void productID(const char *) {}
  void productRevision(const char *) {}
  void onRead(ReadCallback callback) { read = callback; }
  void onWrite(WriteCallback) {}
  void onStartStop(StartStopCallback) {}
  void isWritable(bool) {}
  void mediaPresent(bool present) { medium = present; }
  bool begin(uint32_t, uint16_t) { return true; }

  // Set by onRead during usbDriveConfigure; the test reads sectors through it.
  static ReadCallback read;
  // Whether the drive is currently offering a medium a host could mount.
  static bool medium;
};
