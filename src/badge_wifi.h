#pragma once

#include <stdint.h>
#include <Arduino.h>

struct WifiTuiState {
  bool accessPointActive;
  bool accessPointSelected;
  bool homeConfigured;
  bool homeConnected;
  bool hidden;
  String accessPointSsid;
  String homeSsid;
  String localIp;
  String hostname;
};

// Starts the ESP32 access point and HTTP server.
void setupWiFiAccessPoint();

// Services pending HTTP requests. Call this frequently from loop().
void updateWebServer();

// Posts anything the NFC reader captured while capture mode was on. Call this
// from loop(): it is what keeps board writes on a single task.
void serviceNfcCapture();

// Returns the unique access-point credentials generated during startup.
// These remain valid for the lifetime of the badge.
const char *getBadgeWifiSsid();
const char *getBadgeWifiPassword();

// Copies the six-byte SoftAP MAC address into outMac. Returns false if the
// access point has not been initialized or the destination is null.
bool getBadgeWifiApMac(uint8_t outMac[6]);

// Selects the badge SoftAP or the saved home Wi-Fi; only one is active at a
// time. The choice is saved and restored after reboot.
bool setBadgeAccessPointEnabled(bool enabled, String &error);
bool isBadgeAccessPointActive();
WifiTuiState getWifiTuiState();
bool setBadgeAccessPointSettings(const String &password, bool hidden,
                                 String &error);
bool setBadgeHomeWifiSettings(const String &ssid, const String &password,
                              String &error);
