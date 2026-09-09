#pragma once

#include <Arduino.h>

struct NfcTuiState {
  bool readerReady;
  bool busy;
  bool captureEnabled;
  bool emulating;
  bool wifiOnboarding;
  uint32_t captureCount;
  uint32_t tagScans;
  String status;
  String message;
  String payload;
};

// Initializes the PN532 and starts its dedicated FreeRTOS worker task.
// Failure is reported through getNfcStateJson() and does not stop the LED or
// Wi-Fi controller.
void setupNFC();

// JSON state consumed by the NFC webpage.
String getNfcStateJson();

// Queue external-tag operations. The user has 15 seconds to present a tag.
bool queueNfcRead();
bool queueNfcWrite(const String &recordType, const String &payload);

// Make the PN532 emulate a read-only NFC Forum Type 4 NDEF tag so an NFC
// reader, such as a phone, can scan a Text or URL record from the board.
// External-tag operations are unavailable until tag emulation is stopped.
bool startNfcTagEmulation(const String &recordType, const String &payload);

// Starts a standards-based Wi-Fi Simple Configuration NDEF record. Android
// devices that support application/vnd.wfa.wsc can offer to join the network.
bool startNfcWifiOnboarding(const String &ssid,
                            const String &password,
                            const uint8_t apMac[6]);

// Used by the Wi-Fi settings API so credential changes rebuild only the
// active Wi-Fi onboarding record and never replace a manual Text/URL record.
bool isNfcWifiOnboardingActive();

bool stopNfcTagEmulation();

// Capture mode. While it is on, the reader polls continuously on its own and
// every Text or URL record it decodes is handed to the board as a text post.
// The reader task never touches LittleFS itself: captures are queued here and
// drained by serviceNfcCapture() on the Arduino loop task, which is the only
// task that writes the board.
bool setNfcCaptureEnabled(bool enabled);
bool isNfcCaptureEnabled();

// Pops one captured payload. Returns false when nothing is waiting.
bool takeNfcCapture(String &text);

// Called by the capture drain once a post has been stored, so the page can
// show how many tags have made it onto the board.
void noteNfcCapturePosted();

// Saves what the radio is doing once it has settled, so the badge comes back
// from a reboot the way it was left. Call from loop(); it writes NVS, which
// the reader task must not do.
//
// Saving stays off until armed, so the blank state the badge holds between
// setupNFC() and the boot restore is never written over what was stored.
void armNfcPersistence();
void serviceNfcPersistence();

NfcTuiState getNfcTuiState();
