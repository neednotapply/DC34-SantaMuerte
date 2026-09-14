#pragma once

#include <stdint.h>
#include <Arduino.h>

struct WifiTuiState {
  bool accessPointActive;
  bool accessPointSelected;
  bool homeConfigured;
  bool homeConnected;
  // A network being tried is not a saved one yet. trialSsid names whichever
  // network that is, or the last one that failed and was therefore discarded.
  bool trialActive;
  bool trialFailed;
  String trialSsid;
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

// Selects the badge SoftAP or saved Wi-Fi; only one is active at a
// time. The choice is saved and restored after reboot.
bool setBadgeAccessPointEnabled(bool enabled, String &error);
bool isBadgeAccessPointActive();
WifiTuiState getWifiTuiState();
// Applies the same complete access-point form exposed by the web portal.
// Keeping SSID, password, and visibility together makes USB serial a full
// substitute when the badge is not reachable over Wi-Fi.
bool setBadgeAccessPointSettings(const String &ssid, const String &password,
                                 bool hidden, String &error);
// Tries a network and remembers it only if the badge associates with it, so
// the saved list stays a list of networks that have actually worked. The
// credentials are validated up front and then held in RAM until the join
// succeeds; a failed attempt is discarded. Returns whether the attempt could
// be started, not whether it connected -- watch getWifiTuiState() or
// /api/wifi/client for the verdict.
//
// apHandoverDelayMs is how long the badge's own access point stays up after
// the switch is requested. The web portal needs a wide enough gap for its
// reply to reach a browser that is still connected to that access point;
// USB serial has no such constraint.
bool setBadgeHomeWifiSettings(const String &ssid, const String &password,
                              String &error,
                              uint32_t apHandoverDelayMs = 50);
