#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <LittleFS.h>
#include <esp_wifi.h>
#if __has_include(<esp_netif_sta_list.h>)
#include <esp_netif_sta_list.h>
#define SM_HAS_NETIF_STA_LIST 1
#else
// ESP-IDF 5.5 folded the AP station/IP pairing API out of the public Arduino
// headers. The suffix is only a cosmetic Field Notes label, so retain the
// lookup where the legacy API exists and otherwise fall back to ARP below.
#define SM_HAS_NETIF_STA_LIST 0
#endif
#include <lwip/etharp.h>
#include <cstring>
#include "badge_wifi.h"
#include "badge_button.h"
#include "badge_settings.h"
#include "board.h"
#include "nfc.h"
#include "usb_hid.h"
#include "usb_badusb.h"
#include "usb_network.h"
#include "usb_drive.h"
#include "usb_dropbox.h"
#include "usb_console.h"
#include "usb_tui.h"
#include "nfc_log.h"

// Implemented in main.cpp. wifi.cpp only transports and parses LED requests;
// main.cpp remains responsible for LED state, validation, and animation logic.
String getLedStateJson();
String getLedPixelsJson();
void signalTagCue(bool accepted);
void setIdentifyFrame(uint8_t frame);
uint8_t currentIdentifyFrame();
void applyLedWebSettings(const String &pattern,
                         int red,
                         int green,
                         int blue,
                         int brightness,
                         int speed);

namespace {

constexpr char AP_SSID_PREFIX[] = "Santa Muerte";
// IEEE 802.11 SSIDs can contain up to 32 bytes. This buffer holds the
// 12-character prefix, a space, the four-digit MAC suffix, and '\0'.
char apSsid[33] = {};

const IPAddress AP_IP(10, 69, 4, 20);
const IPAddress AP_GATEWAY(10, 69, 4, 20);
const IPAddress AP_SUBNET(255, 255, 255, 0);
constexpr uint16_t HTTP_PORT = 80;
constexpr uint16_t DNS_PORT = 53;
constexpr char STATION_HOSTNAME[] = "SantaMuerte";
constexpr uint32_t STATION_RETRY_MS = 6000;
// Boot always gives saved Wi-Fi first chance, even if the previous
// session ended in AP mode. After this long with no connection it restores the
// badge AP, which keeps the portal reachable without a cable. Roughly six
// retry cycles is long enough to ride out a router restart.
constexpr uint32_t STATION_FALLBACK_MS = 30000;
// How long a network gets to prove the credentials it was given before they
// are thrown away. Shorter than the fallback window above, so the verdict is
// in -- and the credentials are either remembered or discarded -- before the
// badge gives up and restores its own access point.
constexpr uint32_t STATION_TRIAL_MS = 20000;

WebServer server(HTTP_PORT);
DNSServer dnsServer;
bool dnsServerRunning = false;
bool fileSystemReady = false;
bool accessPointActive = false;
bool accessPointRestartPending = false;
bool refreshWifiNfcAfterRestart = false;
uint32_t accessPointRestartAt = 0;
bool accessPointTogglePending = false;
bool requestedAccessPointEnabled = true;
uint32_t accessPointToggleAt = 0;
bool stationConnectionRequested = false;
// The AP/home toggle represents the *result* of a boot attempt, not its
// starting preference. This flag temporarily overrides that toggle while the
// badge gives saved Wi-Fi a chance to connect.
bool bootHomeConnectionPending = false;
// A join that never succeeds used to look identical to one still in progress:
// WiFi.status() was only ever compared against WL_CONNECTED, so the reason was
// discarded. Keep the last reason so USB, the TUI and the portal can say it.
uint32_t stationAttempts = 0;
String stationLastFailure;
// millis() when the station link was last seen down; 0 while it is up. Covers
// both a join that never lands and a connection that drops later.
uint32_t stationOfflineSince = 0;
bool mdnsRunning = false;
uint32_t stationConnectionStartedAt = 0;
uint8_t stationCandidate = 0;
uint8_t stationCandidates[MAX_SAVED_STATION_NETWORKS] = {};
uint8_t stationCandidateCount = 0;

// A network earns its place in the remembered list by being joined, not by
// being typed. New credentials live here, in RAM, until the badge actually
// associates with them; a typo, a wrong passphrase, or a network that is not
// on air is discarded instead of joining the list the badge retries on every
// boot. trialFailedSsid outlives the attempt only so the portal and the USB
// console can say which network was rejected and that it was not kept.
String trialSsid;
String trialPassword;
bool trialActive = false;
uint32_t trialStartedAt = 0;
String trialFailedSsid;

void clearStationTrial() {
  trialActive = false;
  trialSsid = String();
  trialPassword = String();
}

// Throws away credentials that never produced an association. Nothing was
// written, so there is nothing to undo: an SSID that is not on air, a wrong
// passphrase and a plain typo all end here, and none of them reach the list
// the badge retries on every boot.
void abandonStationTrial() {
  Serial.printf("[WIFI] %s never associated -- not remembering it\r\n",
                trialSsid.c_str());
  trialFailedSsid = trialSsid;
  clearStationTrial();
  stationConnectionRequested = false;
  stationAttempts = 0;
  stationLastFailure = String();
  stationCandidateCount = 0;
  usbTuiRefresh();
}

void refreshStationCandidates() {
  stationCandidateCount = 0;
  const uint8_t savedCount = getPersistentStationWifiCount();
  if (!savedCount) return;

  // A scan lets boot skip remembered networks that are not presently on air,
  // rather than spending the whole recovery window waiting on each one. Hidden
  // SSIDs are retained as fallbacks because a scan cannot reliably reveal them.
  const int networks = WiFi.scanNetworks(false, true);
  for (uint8_t saved = 0; saved < savedCount; ++saved) {
    String ssid, password;
    if (!getPersistentStationWifi(saved, ssid, password)) continue;
    bool visible = false;
    for (int found = 0; found < networks; ++found) {
      if (WiFi.SSID(found) == ssid) { visible = true; break; }
    }
    if (visible) stationCandidates[stationCandidateCount++] = saved;
  }
  WiFi.scanDelete();
  if (!stationCandidateCount) {
    for (uint8_t saved = 0; saved < savedCount; ++saved) {
      stationCandidates[stationCandidateCount++] = saved;
    }
    Serial.println("[WIFI] No remembered SSIDs found in scan; trying hidden/off-air entries");
  } else {
    Serial.printf("[WIFI] Scan found %u remembered network(s)\r\n", stationCandidateCount);
  }
  stationCandidate = 0;
}
char lastSuccessfulApSsid[33] = {};
char lastSuccessfulApPassword[64] = {};
bool lastSuccessfulApHidden = false;
bool hasLastSuccessfulApSettings = false;

void rememberSuccessfulAccessPoint() {
  strlcpy(lastSuccessfulApSsid, apSsid, sizeof(lastSuccessfulApSsid));
  strlcpy(lastSuccessfulApPassword, getPersistentWifiPassword(),
          sizeof(lastSuccessfulApPassword));
  lastSuccessfulApHidden = getPersistentWifiHidden();
  hasLastSuccessfulApSettings = true;
}

bool activeApMatchesExpectedSettings(const char *expectedSsid,
                                    const char *expectedPassword,
                                    bool expectedHidden) {
  wifi_config_t config = {};
  if (esp_wifi_get_config(WIFI_IF_AP, &config) != ESP_OK) {
    return false;
  }

  const size_t expectedSsidLength = strlen(expectedSsid);
  const size_t expectedPasswordLength = strlen(expectedPassword);

  const size_t activeSsidLength =
      config.ap.ssid_len != 0
          ? config.ap.ssid_len
          : strnlen(reinterpret_cast<const char *>(config.ap.ssid),
                    sizeof(config.ap.ssid));

  const bool ssidMatches =
      activeSsidLength == expectedSsidLength &&
      memcmp(config.ap.ssid, expectedSsid, expectedSsidLength) == 0;

  const bool passwordMatches =
      expectedPasswordLength < sizeof(config.ap.password) &&
      memcmp(config.ap.password,
             expectedPassword,
             expectedPasswordLength) == 0 &&
      config.ap.password[expectedPasswordLength] == '\0';

  const bool hiddenMatches =
      (config.ap.ssid_hidden != 0) == expectedHidden;

  return ssidMatches && passwordMatches && hiddenMatches;
}

bool configureVerifiedAccessPoint() {
  const char *password = getPersistentWifiPassword();
  const bool hidden = getPersistentWifiHidden();
  const size_t passwordLength = password ? strlen(password) : 0;
  if (passwordLength != 0 && (passwordLength < 8 || passwordLength > 63)) {
    Serial.println(
        "[WIFI] ERROR: No valid access-point password is available");
    return false;
  }

  // The hostname is configured while Wi-Fi is off, before the station DHCP
  // client starts. This makes it visible as SantaMuerte to routers that list
  // DHCP client names, in addition to the separate .local mDNS record.
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  if (!WiFi.setHostname(STATION_HOSTNAME)) {
    Serial.println("[WIFI] WARNING: Could not set DHCP hostname");
  }
  delay(75);

  for (uint8_t attempt = 1; attempt <= 2; ++attempt) {
    // AP+STA keeps the badge's walk-up network alive while it joins the
    // owner's LAN in the background.
    WiFi.mode(WIFI_AP_STA);
    delay(50);

    if (!WiFi.softAPConfig(AP_IP, AP_GATEWAY, AP_SUBNET)) {
      Serial.println("[WIFI] ERROR: Could not configure access point IP");
    } else if (!WiFi.softAP(apSsid, password, 1, hidden ? 1 : 0, 4)) {
      Serial.println("[WIFI] ERROR: Could not start access point");
    } else {
      delay(75);
      if (activeApMatchesExpectedSettings(apSsid, password, hidden)) {
        stationConnectionRequested = false;
        accessPointActive = true;
        rememberSuccessfulAccessPoint();
        Serial.println(
            "[WIFI] Active access-point credentials verified");
        return true;
      }

      Serial.println(
          "[WIFI] WARNING: Active AP credentials did not match stored "
          "settings; restarting Wi-Fi");
    }

    WiFi.softAPdisconnect(true);
    accessPointActive = false;
    WiFi.mode(WIFI_OFF);
    delay(100);
  }

  Serial.println(
      "[WIFI] ERROR: Access point credential verification failed");
  return false;
}

void refreshAccessPointSsid() {
  const char *customSsid = getPersistentAccessPointSsid();
  if (customSsid && customSsid[0] != '\0') {
    strlcpy(apSsid, customSsid, sizeof(apSsid));
    return;
  }

  // ESP.getEfuseMac() writes the factory base MAC into the low six bytes of
  // its return value, least significant byte first, so bytes four and five are
  // the last pair-group printed on the module. Sixteen bits is short enough to
  // read off a phone at a glance; two badges in the same room collide with
  // probability about one in 65,536 per pair.
  const uint64_t efuseMac = ESP.getEfuseMac();

  snprintf(apSsid,
           sizeof(apSsid),
           "%s %02X%02X",
           AP_SSID_PREFIX,
           static_cast<unsigned>((efuseMac >> 32) & 0xFF),
           static_cast<unsigned>((efuseMac >> 40) & 0xFF));
}

bool restoreLastSuccessfulAccessPoint() {
  if (!hasLastSuccessfulApSettings) return false;

  String error;
  if (!setPersistentWifiSettings(lastSuccessfulApPassword,
                                 lastSuccessfulApHidden, error) ||
      !setPersistentAccessPointSsid(lastSuccessfulApSsid, error)) {
    Serial.printf("[WIFI] Could not restore the last working AP settings: %s\r\n",
                  error.c_str());
    return false;
  }
  refreshAccessPointSsid();
  Serial.println("[WIFI] Restoring the last verified access-point settings");
  return configureVerifiedAccessPoint();
}

// Answering every name with the badge's own address is what turns a phone's
// connectivity probe into a request this server can redirect.
void startCaptivePortalDns() {
  if (dnsServerRunning) {
    dnsServer.stop();
    dnsServerRunning = false;
  }

  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  if (dnsServer.start(DNS_PORT, "*", AP_IP)) {
    dnsServerRunning = true;
    Serial.println("[WIFI] Captive portal DNS started");
  } else {
    Serial.println(
        "[WIFI] WARNING: Captive portal DNS could not start; visitors must "
        "browse to 10.69.4.20 by hand");
  }
}

void stopCaptivePortalDns() {
  if (!dnsServerRunning) return;
  dnsServer.stop();
  dnsServerRunning = false;
  Serial.println("[WIFI] Captive portal DNS stopped");
}

void addNoCacheHeaders() {
  server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "-1");
}

int boundedRequestArg(const String &name, int low, int high) {
  if (!server.hasArg(name)) return -1;
  return constrain(server.arg(name).toInt(), low, high);
}

void serveLittleFsFile(const char *path, const char *contentType) {
  addNoCacheHeaders();

  if (!fileSystemReady || !LittleFS.exists(path)) {
    String error = path;
    error += "is missing. Upload the LittleFS image and try again.";
    server.send(500, "text/plain; charset=utf-8", error);
    return;
  }

  File page = LittleFS.open(path, "r");
  if (!page) {
    String error = "Could not open";
    error += path;
    server.send(500, "text/plain; charset=utf-8", error);
    return;
  }

  server.streamFile(page, contentType);
  page.close();
}

String jsonEscape(const String &value) {
  String escaped;
  escaped.reserve(value.length() + 8);
  for (size_t i = 0; i < value.length(); ++i) {
    const char character = value[i];
    switch (character) {
      case '\\': escaped += F("\\\\"); break;
      case '"': escaped += F("\\\""); break;
      case '\n': escaped += F("\\n"); break;
      case '\r': escaped += F("\\r"); break;
      case '\t': escaped += F("\\t"); break;
      default: escaped += character; break;
    }
  }
  return escaped;
}

void stopMdns() {
  if (!mdnsRunning) return;
  MDNS.end();
  mdnsRunning = false;
  Serial.println("[WIFI] mDNS stopped");
}

const char *stationStatusName(int status) {
  switch (status) {
    case WL_NO_SSID_AVAIL: return "SSID not found on air";
    case WL_CONNECT_FAILED: return "association rejected (wrong password?)";
    case WL_CONNECTION_LOST: return "connection lost";
    case WL_DISCONNECTED: return "disconnected";
    case WL_IDLE_STATUS: return "idle";
    case WL_SCAN_COMPLETED: return "scan completed";
    case WL_CONNECTED: return "connected";
    default: return "unknown";
  }
}

void startStationConnection(bool forceBootAttempt = false) {
  // A normal operator-selected AP suppresses station reconnects. At boot,
  // though, saved Wi-Fi is deliberately tried once regardless of the
  // toggle left by the preceding session. A trial overrides both: it is a
  // direct instruction that arrived while somebody was watching it.
  if (!trialActive &&
      ((!forceBootAttempt && getPersistentAccessPointEnabled()) ||
       !hasPersistentStationWifiSettings())) {
    return;
  }

  stopMdns();
  // setHostname applies to the station interface; it must be set before
  // begin() for DHCP and the local network to see SantaMuerte consistently.
  String ssid, password;
  if (trialActive) {
    ssid = trialSsid;
    password = trialPassword;
  } else {
    if (!stationCandidateCount) refreshStationCandidates();
    if (!stationCandidateCount) return;
    stationCandidate %= stationCandidateCount;
    if (!getPersistentStationWifi(stationCandidates[stationCandidate], ssid, password)) return;
  }
  WiFi.mode(accessPointActive ? WIFI_AP_STA : WIFI_STA);
  WiFi.disconnect(false, false);
  // A remembered network may be open, in which case the stored password is
  // empty. Passing no passphrase at all states that outright: the core only
  // raises the auth-mode threshold to WPA2 when a passphrase is present, so
  // leaving it out is what keeps an open AP eligible. The core happens to
  // treat an empty passphrase the same way, but the open case should not have
  // to rely on that.
  if (password.length() == 0) WiFi.begin(ssid.c_str());
  else WiFi.begin(ssid.c_str(), password.c_str());
  stationConnectionRequested = true;
  stationConnectionStartedAt = millis();
  Serial.printf(trialActive ? "[WIFI] Trying Wi-Fi: %s\r\n"
                            : "[WIFI] Joining saved Wi-Fi: %s\r\n",
                ssid.c_str());
}

// WiFi Tethering only means anything while the badge is on saved Wi-Fi: with
// no upstream there is nothing to share, and the profile is indistinguishable
// from one that is working -- no badge access point to join, and a host that
// sees an adapter carrying nothing. Leaving it returns the badge to the Field
// Notes Drive, which needs no network to be worth having plugged in.
void leaveTetheringWithoutUpstream(const char *why) {
  if (getPersistentUsbDeviceProfile() != UsbDeviceProfile::NETWORK) return;
  // The no-upstream check runs every loop, so a refused NVS write must not
  // become an NVS write attempted thousands of times a second.
  static bool attempted = false;
  if (attempted) return;
  attempted = true;
  String error;
  if (!setPersistentUsbDeviceProfile(UsbDeviceProfile::DRIVE, error)) {
    Serial.printf("[USB] Could not leave WiFi Tethering: %s\r\n",
                  error.c_str());
    return;
  }
  Serial.printf("[USB] Leaving WiFi Tethering (%s); starting Field Notes Drive\r\n",
                why);
  delay(80);
  ESP.restart();
}

void serviceStationConnection() {
  // NCM only advertises an active cable link while the badge's saved Wi-Fi is
  // associated. Serial and HID remain independently available.
  usbNetworkSetStationConnected(WiFi.status() == WL_CONNECTED);
  // Nothing remembered to join, so nothing will ever arrive to share.
  if (!hasPersistentStationWifiSettings() && !trialActive) {
    leaveTetheringWithoutUpstream("no saved Wi-Fi");
  }
  if (getPersistentAccessPointEnabled() && !bootHomeConnectionPending) {
    stationConnectionRequested = false;
    // Whether this was the operator's choice or the fallback below, the next
    // switch to saved Wi-Fi starts its own clock rather than inheriting one.
    stationOfflineSince = 0;
    stopMdns();
    return;
  }
  if (!hasPersistentStationWifiSettings() && !trialActive) return;

  if (WiFi.status() == WL_CONNECTED) {
    // The association is the proof -- but only an association with the network
    // actually under test. A badge that is already on a saved network still
    // reports WL_CONNECTED the instant a trial starts, and that connection
    // says nothing about the credentials just typed.
    if (trialActive && WiFi.SSID() == trialSsid) {
      String error;
      if (setPersistentStationWifiSettings(trialSsid, trialPassword, error)) {
        Serial.printf("[WIFI] Joined %s -- remembering it\r\n",
                      trialSsid.c_str());
        stationCandidateCount = 0;  // rebuild so the new network is a candidate
      } else {
        Serial.printf("[WIFI] WARNING: Joined %s but could not remember it: %s\r\n",
                      trialSsid.c_str(), error.c_str());
      }
      clearStationTrial();
      trialFailedSsid = String();
      usbTuiRefresh();
    } else if (trialActive && millis() - trialStartedAt >= STATION_TRIAL_MS) {
      // Still holding the old association when the window closed: the trial
      // never got its chance, and unproven credentials are not kept anyway.
      abandonStationTrial();
    }
    if (bootHomeConnectionPending) {
      bootHomeConnectionPending = false;
      requestedAccessPointEnabled = false;
      String error;
      if (!setPersistentAccessPointEnabled(false, error)) {
        Serial.printf("[WIFI] WARNING: Saved Wi-Fi connected, but the mode "
                      "could not be saved: %s\r\n", error.c_str());
      } else {
        Serial.println("[WIFI] Boot home-Wi-Fi attempt succeeded");
      }
    }
    stationOfflineSince = 0;
    if (!mdnsRunning) {
      if (MDNS.begin(STATION_HOSTNAME)) {
        MDNS.addService("http", "tcp", HTTP_PORT);
        mdnsRunning = true;
        stationAttempts = 0;
        stationLastFailure = String();
        Serial.printf("[WIFI] Saved Wi-Fi connected: %s // http://%s.local/\r\n",
                      WiFi.localIP().toString().c_str(), STATION_HOSTNAME);
      } else {
        Serial.println("[WIFI] WARNING: Could not start mDNS");
      }
    }
    return;
  }

  stopMdns();

  const uint32_t now = millis();
  if (trialActive && now - trialStartedAt >= STATION_TRIAL_MS) {
    abandonStationTrial();
    if (hasPersistentStationWifiSettings()) {
      // Networks that did prove themselves get their own full window from
      // here rather than inheriting what the trial already spent.
      stationOfflineSince = 0;
      startStationConnection();
      return;
    }
    // Nothing is left to try, so there is no point waiting out the fallback
    // window with no candidates: bring the portal back now.
    String error;
    requestedAccessPointEnabled = true;
    if (!setPersistentAccessPointEnabled(true, error)) {
      Serial.printf("[WIFI] Could not save AP fallback mode: %s\r\n",
                    error.c_str());
    }
    stationOfflineSince = 0;
    accessPointTogglePending = true;
    accessPointToggleAt = now + 50;
    return;
  }
  if (stationOfflineSince == 0) stationOfflineSince = now;
  if (now - stationOfflineSince >= STATION_FALLBACK_MS) {
    Serial.printf("[WIFI] No saved Wi-Fi for %lus -- bringing the badge access "
                  "point back so the portal stays reachable.\r\n",
                  static_cast<unsigned long>((now - stationOfflineSince) / 1000));
    String error;
    // AP fallback is unconditional once this boot attempt has failed. Persist
    // the result for status screens and later runtime reconnects, but still
    // bring the AP up if NVS happens to reject the write.
    bootHomeConnectionPending = false;
    requestedAccessPointEnabled = true;
    if (!setPersistentAccessPointEnabled(true, error)) {
      Serial.printf("[WIFI] Could not save AP fallback mode: %s\r\n",
                    error.c_str());
    }
    stationConnectionRequested = false;
    stationOfflineSince = 0;
    stationAttempts = 0;
    stationLastFailure = String();
    accessPointTogglePending = true;
    accessPointToggleAt = now + 50;
    // The access-point choice above is already saved, so the badge comes back
    // on its own AP either way; this only decides which USB device it is.
    leaveTetheringWithoutUpstream("saved Wi-Fi did not come up");
    return;
  }

  if (!stationConnectionRequested ||
      now - stationConnectionStartedAt >= STATION_RETRY_MS) {
    if (stationConnectionRequested) {
      const int status = WiFi.status();
      ++stationAttempts;
      stationLastFailure = stationStatusName(status);
      Serial.printf("[WIFI] Saved Wi-Fi attempt %lu failed: %s (status %d)\r\n",
                    static_cast<unsigned long>(stationAttempts),
                    stationStatusName(status), status);
      if (status == WL_NO_SSID_AVAIL) {
        // The single most common cause, and invisible without saying it: this
        // radio is 2.4 GHz only, so a 5 GHz SSID simply is not there.
        Serial.printf("[WIFI]   '%s' is not visible. This radio is 2.4 GHz "
                      "only -- a 5 GHz SSID cannot be joined. Check the name "
                      "and the band.\r\n",
                      trialActive ? trialSsid.c_str()
                                  : getPersistentStationWifiSsid());
      }
      // A trial has one network to try; only the saved list is a rotation.
      if (!trialActive && stationCandidateCount > 1) {
        stationCandidate = (stationCandidate + 1) % stationCandidateCount;
      }
    }
    startStationConnection();
  }
}

String stationWifiStatusText() {
  // The trial verdict outranks everything else: it is the answer to what the
  // operator just did, and it says whether the network was kept.
  if (trialActive) return F("Testing the network… it is only saved if it connects.");
  if (trialFailedSsid.length() > 0 && WiFi.status() != WL_CONNECTED) {
    return F("Could not connect. That network was not saved.");
  }
  if (!hasPersistentStationWifiSettings()) return F("You have not saved a network yet.");
  if (WiFi.status() == WL_CONNECTED) return F("Connected to saved Wi-Fi.");
  if (stationConnectionRequested && stationLastFailure.length() > 0) {
    return String(F("Connecting to saved Wi-Fi…")) + stationLastFailure;
  }
  if (stationConnectionRequested) return F("Connecting to saved Wi-Fi…");
  return F("Saved Wi-Fi is not connected.");
}

String stationWifiJson(bool ok, const String &message) {
  const bool connected = WiFi.status() == WL_CONNECTED;
  String json;
  json.reserve(320);
  json += F("{\"ok\":");
  json += ok ? F("true") : F("false");
  json += F(",\"configured\":");
  json += hasPersistentStationWifiSettings() ? F("true") : F("false");
  json += F(",\"ssid\":\"");
  json += jsonEscape(getPersistentStationWifiSsid());
  json += F("\",\"saved\":[");
  for (uint8_t i = 0; i < getPersistentStationWifiCount(); ++i) {
    String savedSsid, ignoredPassword;
    if (!getPersistentStationWifi(i, savedSsid, ignoredPassword)) continue;
    if (i) json += ',';
    json += '\"'; json += jsonEscape(savedSsid); json += '\"';
  }
  json += F("]");
  json += F(",\"trial\":");
  json += trialActive ? F("true") : F("false");
  json += F(",\"trialSsid\":\"");
  json += jsonEscape(trialActive ? trialSsid.c_str() : trialFailedSsid.c_str());
  json += F("\",\"connected\":");
  json += connected ? F("true") : F("false");
  json += F(",\"ip\":\"");
  json += connected ? WiFi.localIP().toString() : String();
  json += F("\",\"hostname\":\"");
  json += STATION_HOSTNAME;
  json += F(".local\",\"dhcpHostname\":\"");
  json += jsonEscape(WiFi.getHostname() ? WiFi.getHostname() : "");
  json += F("\",\"status\":\"");
  json += jsonEscape(stationWifiStatusText());
  json += F("\",\"message\":\"");
  json += jsonEscape(message);
  json += F("\"}");
  return json;
}

String wifiSettingsJson(bool ok, const String &message) {
  String json;
  json.reserve(220);
  json += F("{\"ok\":");
  json += ok ? F("true") : F("false");
  json += F(",\"ssid\":\"");
  json += jsonEscape(apSsid);
  json += F("\",\"password\":\"");
  json += jsonEscape(getPersistentWifiPassword());
  json += F("\",\"hidden\":");
  json += getPersistentWifiHidden() ? F("true") : F("false");
  json += F(",\"apEnabled\":");
  json += getPersistentAccessPointEnabled() ? F("true") : F("false");
  json += F(",\"apActive\":");
  json += accessPointActive ? F("true") : F("false");
  json += F(",\"ip\":\"");
  json += accessPointActive ? WiFi.softAPIP().toString()
                           : (WiFi.status() == WL_CONNECTED
                                  ? WiFi.localIP().toString()
                                  : AP_IP.toString());
  json += F("\"");
  json += F(",\"apTogglePending\":");
  json += accessPointTogglePending ? F("true") : F("false");
  json += F(",\"restartPending\":");
  json += accessPointRestartPending ? F("true") : F("false");
  json += F(",\"message\":\"");
  json += jsonEscape(message);
  json += F("\"}");
  return json;
}

String languageJson(bool ok, const String &message) {
  String json;
  json.reserve(100);
  json += F("{\"ok\":");
  json += ok ? F("true") : F("false");
  json += F(",\"locale\":\"");
  json += getPersistentEnglishLanguage() ? F("en-US") : F("es-MX");
  json += F("\",\"message\":\"");
  json += jsonEscape(message);
  json += F("\"}");
  return json;
}

void handleLanguageGet() {
  addNoCacheHeaders();
  server.send(200, "application/json", languageJson(true, String()));
}

void handleLanguageSet() {
  addNoCacheHeaders();
  if (!server.hasArg("locale")) {
    server.send(400, "application/json", languageJson(false, "The language is missing."));
    return;
  }
  const bool english = server.arg("locale") == "en-US";
  String error;
  if (!setPersistentEnglishLanguage(english, error)) {
    server.send(500, "application/json", languageJson(false, error));
    return;
  }
  server.send(200, "application/json", languageJson(true, String()));
}

void handleWifiSettingsGet() {
  addNoCacheHeaders();
  server.send(200, "application/json", wifiSettingsJson(true, String()));
}

void handleWifiSettingsSet() {
  addNoCacheHeaders();

  if (!server.hasArg("ssid") || !server.hasArg("password") || !server.hasArg("hidden")) {
    server.send(
        400,
        "application/json",
        wifiSettingsJson(false, "The network name, the password or the hidden-network setting is missing."));
    return;
  }

  const String ssid = server.arg("ssid");
  const String password = server.arg("password");
  const String hiddenValue = server.arg("hidden");
  const bool hidden =
      hiddenValue == "1" || hiddenValue == "true" || hiddenValue == "on";

  // Saving identical settings used to restart the access point anyway, which
  // drops every client. Landing on this page and pressing save then looked
  // like a loop: reconnect, portal reopens, same form.
  const bool unchanged = ssid == apSsid &&
                         password == getPersistentWifiPassword() &&
                         hidden == getPersistentWifiHidden();

  String error;
  if (!setPersistentWifiSettings(password, hidden, error)) {
    server.send(400, "application/json", wifiSettingsJson(false, error));
    return;
  }
  if (!setPersistentAccessPointSsid(ssid, error)) {
    server.send(400, "application/json", wifiSettingsJson(false, error));
    return;
  }
  refreshAccessPointSsid();

  if (unchanged) {
    server.send(200, "application/json",
                wifiSettingsJson(true, "No changes. The badge is unchanged."));
    return;
  }

  if (!getPersistentAccessPointEnabled()) {
    server.send(200, "application/json",
                wifiSettingsJson(
                    true,
                    "Saved. The access point stays off until you turn it on."));
    return;
  }

  // Preserve the current NFC mode. Only an active Wi-Fi onboarding record is
  // rebuilt after the AP restart; manual Text/URL emulation is untouched.
  refreshWifiNfcAfterRestart = isNfcWifiOnboardingActive();
  accessPointRestartPending = true;
  accessPointRestartAt = millis() + 700;

  server.send(
      202,
      "application/json",
      wifiSettingsJson(
          true,
          hidden
              ? "Saved. Join again by typing the hidden network name and the new password by hand."
              : "Saved. Join the badge again with the new network name and password."));
}

void handleAccessPointSet() {
  addNoCacheHeaders();
  if (!server.hasArg("enabled")) {
    server.send(400, "application/json",
                wifiSettingsJson(false, "The access-point setting is missing."));
    return;
  }

  const String value = server.arg("enabled");
  const bool enabled = value == "1" || value == "true" || value == "on";
  String error;
  if (!setPersistentAccessPointEnabled(enabled, error)) {
    server.send(400, "application/json", wifiSettingsJson(false, error));
    return;
  }

  requestedAccessPointEnabled = enabled;
  accessPointTogglePending = true;
  // Send the response before a client on the badge AP is intentionally
  // disconnected. A home-LAN client stays connected through the STA side.
  accessPointToggleAt = millis() + 700;
  server.send(202, "application/json",
              wifiSettingsJson(
                  true,
                  enabled
                      ? "Saved. Turning on the badge access point."
                      : "Saved. The access point will turn off; use saved Wi-Fi or USB to turn it back on."));
}

void handleStationWifiGet() {
  addNoCacheHeaders();
  server.send(200, "application/json", stationWifiJson(true, String()));
}

// On-demand scan for the "pick a network" helper on the settings page. The
// scan is synchronous, so this request takes a second or two and, because the
// single radio hops channels to sweep, a browser on the badge's own AP sees a
// brief stall while it runs -- which is why the page only scans on a tap and
// never on a timer. Results are de-duplicated by SSID (keeping the strongest
// sighting), hidden/blank SSIDs dropped, sorted strongest first.
void handleWifiScan() {
  addNoCacheHeaders();

  const int found = WiFi.scanNetworks(false, false);
  if (found <= 0) {
    WiFi.scanDelete();
    server.send(200, "application/json", F("{\"networks\":[]}"));
    return;
  }

  const int count = found > 64 ? 64 : found;
  bool used[64];
  for (int i = 0; i < count; ++i) used[i] = false;

  String seen[24];
  int seenCount = 0;
  String body = F("{\"networks\":[");
  int emitted = 0;

  // Selection sort by RSSI (strongest first) over the small scan list, so the
  // first sighting of an SSID is its best one and later duplicates are dropped.
  for (int n = 0; n < count && emitted < 24; ++n) {
    int best = -1;
    for (int i = 0; i < count; ++i) {
      if (used[i]) continue;
      if (best < 0 || WiFi.RSSI(i) > WiFi.RSSI(best)) best = i;
    }
    if (best < 0) break;
    used[best] = true;

    const String ssid = WiFi.SSID(best);
    if (ssid.length() == 0) continue;  // hidden / blank
    bool duplicate = false;
    for (int j = 0; j < seenCount; ++j) {
      if (seen[j] == ssid) { duplicate = true; break; }
    }
    if (duplicate) continue;
    seen[seenCount++] = ssid;

    if (emitted > 0) body += ',';
    body += F("{\"ssid\":\"");
    body += jsonEscape(ssid);
    body += F("\",\"rssi\":");
    body += WiFi.RSSI(best);
    body += F(",\"secure\":");
    body += (WiFi.encryptionType(best) == WIFI_AUTH_OPEN) ? F("false") : F("true");
    body += '}';
    ++emitted;
  }
  body += F("]}");

  WiFi.scanDelete();
  server.send(200, "application/json", body);
}

void handleStationWifiSet() {
  addNoCacheHeaders();
  if (!server.hasArg("ssid")) {
    server.send(400, "application/json",
                stationWifiJson(false,
                                "The saved Wi-Fi name is required."));
    return;
  }

  String error;
  // The portal and USB serial share one path, so a network is remembered on
  // the same terms either way. Trying it deliberately selects the alternate
  // mode; 700 ms is long enough for this reply to reach a browser that is
  // still on the badge's own access point before that point goes away.
  if (!setBadgeHomeWifiSettings(server.arg("ssid"),
                                server.hasArg("password")
                                    ? server.arg("password")
                                    : String(),
                                error, 700)) {
    server.send(400, "application/json", stationWifiJson(false, error));
    return;
  }

  // Nothing has been stored at this point, and saying otherwise would be a
  // lie the operator only discovers when a reboot drops the network.
  server.send(202, "application/json",
              stationWifiJson(true,
                              "Testing the network… it is only saved if it connects."));
}

void servicePendingAccessPointRestart() {
  if (!accessPointRestartPending ||
      static_cast<int32_t>(millis() - accessPointRestartAt) < 0) {
    return;
  }

  accessPointRestartPending = false;
  Serial.println("[WIFI] Applying dashboard Wi-Fi settings");

  if (!configureVerifiedAccessPoint()) {
    Serial.println(
        "[WIFI] ERROR: Dashboard settings were stored, but the access point "
        "could not restart");
    if (restoreLastSuccessfulAccessPoint()) startCaptivePortalDns();
    refreshWifiNfcAfterRestart = false;
    return;
  }

  Serial.printf("[WIFI] SSID visibility: %s\r\n",
                getPersistentWifiHidden() ? "hidden" : "visible");
  Serial.printf("[WIFI] Security: %s\r\n",
                getPersistentWifiPassword()[0] ? "WPA2" : "open");

  startCaptivePortalDns();

  startStationConnection();

  if (refreshWifiNfcAfterRestart) {
    uint8_t accessPointMac[6] = {0};
    const bool haveAccessPointMac = getBadgeWifiApMac(accessPointMac);
    if (startNfcWifiOnboarding(
            getBadgeWifiSsid(),
            getBadgeWifiPassword(),
            haveAccessPointMac ? accessPointMac : nullptr)) {
      Serial.println(
          "[NFC][WIFI] Onboarding records refreshed with dashboard settings");
    } else {
      Serial.println(
          "[NFC][WIFI] WARNING: Could not refresh onboarding records");
    }
  }

  refreshWifiNfcAfterRestart = false;
}

void servicePendingAccessPointToggle() {
  if (!accessPointTogglePending ||
      static_cast<int32_t>(millis() - accessPointToggleAt) < 0) {
    return;
  }

  accessPointTogglePending = false;
  if (requestedAccessPointEnabled) {
    Serial.println("[WIFI] Turning badge access point on");
    stationConnectionRequested = false;
    stopMdns();
    if (!configureVerifiedAccessPoint()) {
      Serial.println("[WIFI] ERROR: Could not turn badge access point on");
      if (restoreLastSuccessfulAccessPoint()) startCaptivePortalDns();
      return;
    }
    startCaptivePortalDns();
    return;
  }

  Serial.println("[WIFI] Turning badge access point off");
  stopCaptivePortalDns();
  WiFi.softAPdisconnect(true);
  accessPointActive = false;
  startStationConnection();
}

// -----------------------------------------------------------------------------
// Dashboard, LED webpage, and LED API
// -----------------------------------------------------------------------------
void handleDashboardPage() {
  serveLittleFsFile("/index.html", "text/html; charset=utf-8");
}

void handleThemeStylesheet() {
  serveLittleFsFile("/theme.css", "text/css; charset=utf-8");
}

void handleLocaleScript() {
  serveLittleFsFile("/locale.js", "text/javascript; charset=utf-8");
}

void handleTapeScript() {
  serveLittleFsFile("/tape.js", "text/javascript; charset=utf-8");
}

void handleLogoAsset() {
  serveLittleFsFile("/assets/logo-candle.png", "image/png");
}

void handleBadgeFigureAsset() {
  serveLittleFsFile("/assets/badge-figure.png", "image/png");
}

void handleLedPage() {
  serveLittleFsFile("/led.html", "text/html; charset=utf-8");
}

void handleLedState() {
  addNoCacheHeaders();
  server.send(200, "application/json", getLedStateJson());
}

// Polled by the LED page while it is on screen. getPixelColor() reads back the
// strip's own buffer, so what the page draws is what the badge is showing,
// brightness scaling included.
void handleLedPixels() {
  addNoCacheHeaders();
  server.send(200, "application/json", getLedPixelsJson());
}

// Identify frames, for mapping strand index to physical pixel from a photo.
// Same frames the serial console drives; this is the phone-friendly door.
void handleLedIdentify() {
  addNoCacheHeaders();

  const int frame = server.hasArg("frame") ? server.arg("frame").toInt() : 0;
  setIdentifyFrame(static_cast<uint8_t>(constrain(frame, 0, 3)));

  String json;
  json.reserve(48);
  json += F("{\"frame\":");
  json += currentIdentifyFrame();
  json += '}';
  server.send(200, "application/json", json);
}

void handleLedSet() {
  const String pattern = server.hasArg("pattern") ? server.arg("pattern") : String();

  applyLedWebSettings(pattern,
                      boundedRequestArg("r", 0, 255),
                      boundedRequestArg("g", 0, 255),
                      boundedRequestArg("b", 0, 255),
                      boundedRequestArg("brightness", 0, 255),
                      boundedRequestArg("speed", 1, 100));

  handleLedState();
}

// -----------------------------------------------------------------------------
// NFC webpage and API
// -----------------------------------------------------------------------------
void handleNfcPage() {
  serveLittleFsFile("/nfc.html", "text/html; charset=utf-8");
}

void handleNfcState() {
  addNoCacheHeaders();
  server.send(200, "application/json", getNfcStateJson());
}

void sendNfcQueueResponse(bool accepted) {
  addNoCacheHeaders();
  server.send(accepted ? 202 : 409, "application/json", getNfcStateJson());
}

void handleNfcRead() {
  sendNfcQueueResponse(queueNfcRead());
}

void handleNfcWrite() {
  const String recordType = server.hasArg("recordType") ? server.arg("recordType") : String();
  const String payload = server.hasArg("payload") ? server.arg("payload") : String();
  sendNfcQueueResponse(queueNfcWrite(recordType, payload));
}


void handleNfcTagEmulationStart() {
  const String recordType =
      server.hasArg("recordType") ? server.arg("recordType") : String();
  const String payload =
      server.hasArg("payload") ? server.arg("payload") : String();
  sendNfcQueueResponse(startNfcTagEmulation(recordType, payload));
}

// Restores the Wi-Fi credential record. Without this, stopping tag emulation
// would leave the badge with no way back to sharing its own network short of
// erasing NVS.
void handleNfcWifiOnboardingStart() {
  uint8_t accessPointMac[6] = {0};
  const bool haveAccessPointMac = getBadgeWifiApMac(accessPointMac);

  sendNfcQueueResponse(
      startNfcWifiOnboarding(getBadgeWifiSsid(),
                             getBadgeWifiPassword(),
                             haveAccessPointMac ? accessPointMac : nullptr));
}

void handleNfcTagEmulationStop() {
  sendNfcQueueResponse(stopNfcTagEmulation());
}

// -----------------------------------------------------------------------------
// Message board webpage and API
// -----------------------------------------------------------------------------
void handleBoardPage() {
  serveLittleFsFile("/board.html", "text/html; charset=utf-8");
}

String boardStateJson(bool ok, const String &message) {
  String json;
  json.reserve(200);
  json += F("{\"ok\":");
  json += ok ? F("true") : F("false");
  json += F(",\"ready\":");
  json += isBoardReady() ? F("true") : F("false");
  json += F(",\"stored\":");
  json += boardStoredCount();
  json += F(",\"newestId\":");
  json += boardNewestId();
  json += F(",\"message\":\"");
  json += jsonEscape(message);
  json += F("\"}");
  return json;
}

// One buffer serves both directions. The web server runs entirely from
// loop(), so an upload being decoded and an image being served can never
// overlap.
uint8_t boardImageBuffer[BOARD_MAX_IMAGE_BYTES];

// A drawing arrives as the raw POST body and is streamed straight into the
// buffer above, 1436 bytes at a time, without a single allocation.
//
// It used to travel as a base64 field inside a urlencoded form. The web server
// cannot stream one of those: it materialises the whole body as a malloc'd
// buffer, then a copy in a String, then another copy per parsed argument --
// about 50 KB of *contiguous* heap for a 12 KB drawing, before our handler is
// even reached. The badge runs on ~51 KB free with a ~30 KB largest block, so
// big notes died inside the parser, which then drops the connection
// without a reply. That is the "Failed to fetch" the browser reported.
size_t boardUploadLength = 0;
bool boardUploadTooBig = false;

void handleBoardUpload() {
  HTTPRaw &upload = server.raw();

  if (upload.status == RAW_START) {
    boardUploadLength = 0;
    boardUploadTooBig = false;
    return;
  }

  if (upload.status == RAW_WRITE) {
    // Once past the ceiling the remaining chunks are still accepted and
    // discarded rather than refused: the server reads the whole body off the
    // socket either way, and leaving part of it unread would desynchronise
    // the connection.
    if (boardUploadTooBig ||
        upload.currentSize > sizeof(boardImageBuffer) - boardUploadLength) {
      boardUploadTooBig = true;
      return;
    }

    memcpy(boardImageBuffer + boardUploadLength, upload.buf, upload.currentSize);
    boardUploadLength += upload.currentSize;
    return;
  }

  if (upload.status == RAW_ABORTED) {
    boardUploadLength = 0;
    boardUploadTooBig = false;
  }
}

int hexDigitValue(char character) {
  if (character >= '0' && character <= '9') return character - '0';
  if (character >= 'a' && character <= 'f') return character - 'a' + 10;
  if (character >= 'A' && character <= 'F') return character - 'A' + 10;
  return -1;
}

// The body now belongs to the image alone, so a note's text rides in a
// header instead -- percent-encoded by the browser, because a header value
// cannot carry a newline or a raw UTF-8 byte.
String percentDecode(const String &value) {
  String out;
  out.reserve(value.length());

  for (unsigned int i = 0; i < value.length(); ++i) {
    const char character = value[i];
    if (character == '%' && i + 2 < value.length()) {
      const int high = hexDigitValue(value[i + 1]);
      const int low = hexDigitValue(value[i + 2]);
      if (high >= 0 && low >= 0) {
        out += static_cast<char>(high * 16 + low);
        i += 2;
        continue;
      }
    }
    out += character;
  }

  return out;
}

void handleBoardImage() {
  const uint32_t postId =
      server.hasArg("id") ? strtoul(server.arg("id").c_str(), nullptr, 10) : 0;

  const size_t length =
      readBoardImage(postId, boardImageBuffer, sizeof(boardImageBuffer));
  if (length == 0) {
    server.send(404, "text/plain; charset=utf-8", "That note has no drawing.");
    return;
  }

  // The client adds a random v token for each rendered image. Post numbers
  // can restart after a filesystem flash, so the numeric id alone is not a
  // permanent cache identity.
  server.sendHeader("Cache-Control", "public, max-age=31536000, immutable");
  server.send_P(200, "image/jpeg",
                reinterpret_cast<const char *>(boardImageBuffer), length);
}

void handleBoardState() {
  addNoCacheHeaders();
  server.send(200, "application/json", boardStateJson(true, String()));
}

// Posts are streamed one at a time. Building the whole page of JSON in a
// String first would put several kilobytes on a heap that is already carrying
// Wi-Fi, the web server and the PN532 worker.
// A browser will not tell us a username or a hostname -- neither will the DHCP
// server, which only reports a client IP. The User-Agent is the one piece of
// identity a browser volunteers, and it names a device class at best
// ("Pixel 6a", "iPhone"). That is remembered here in RAM only: the board record
// is packed to a fixed 304 bytes with no room for a name, and widening it would
// change the on-disk layout and wipe every stored note. So a label lives
// only until the badge reboots, after which posts show their pseudonym again.
// The label is keyed to the author, not the post, so it applies to everything
// that pseudonym has ever written -- including retroactively. Normally one
// browser means one device and that reads correctly, but two devices that
// happen to draw the same 1000-9999 pseudonym would share a label.
constexpr uint8_t AUTHOR_NAME_SLOTS = 16;
constexpr uint8_t AUTHOR_NAME_LENGTH = 24;
struct AuthorName {
  uint16_t id;
  char name[AUTHOR_NAME_LENGTH];  // device type, e.g. "Pixel 6a"
  char mac[5];                    // last two octets of the MAC, e.g. "4B5C"
};
AuthorName authorNames[AUTHOR_NAME_SLOTS] = {};
uint8_t authorNameNext = 0;

String deviceLabelFromUserAgent(const String &agent) {
  if (agent.length() == 0) return String();
  if (agent.indexOf("Android") >= 0) {
    // Android carries the model last inside the platform parens, as in
    // "(Linux; Android 14; Pixel 6a)". A WebView adds "; wv" and some builds
    // append " Build/...", neither of which is a device name.
    const int open = agent.indexOf('(');
    const int close = agent.indexOf(')', open + 1);
    if (open >= 0 && close > open) {
      String inside = agent.substring(open + 1, close);
      int cut = inside.lastIndexOf(';');
      String model = cut >= 0 ? inside.substring(cut + 1) : String();
      model.trim();
      if (model == "wv" && cut >= 0) {
        inside = inside.substring(0, cut);
        cut = inside.lastIndexOf(';');
        model = cut >= 0 ? inside.substring(cut + 1) : String();
        model.trim();
      }
      const int build = model.indexOf(" Build/");
      if (build >= 0) model = model.substring(0, build);
      model.trim();
      // Two non-models can end up here. Stripping "wv" can leave the version
      // segment ("Android 13"), and Chrome's reduced user agent substitutes a
      // single placeholder letter ("Android 10; K"). Neither names a device.
      if (model.startsWith("Android")) model = String();
      if (model.length() < 2) model = String();
      if (model.length() > 0) return model;
    }
    return String(F("Android"));
  }
  if (agent.indexOf("iPhone") >= 0) return String(F("iPhone"));
  if (agent.indexOf("iPad") >= 0) return String(F("iPad"));
  if (agent.indexOf("CrOS") >= 0) return String(F("Chromebook"));
  if (agent.indexOf("Macintosh") >= 0) return String(F("Mac"));
  if (agent.indexOf("Windows") >= 0) return String(F("Windows"));
  if (agent.indexOf("Linux") >= 0) return String(F("Linux"));
  return String();
}

// The last octet of the client's MAC, as two uppercase hex digits. Two routes,
// because the badge is either the access point or just another station:
//   - as the AP, esp_netif_get_sta_list pairs every associated MAC with its
//     leased IP directly;
//   - on saved Wi-Fi it has no station list, but any peer it has just exchanged
//     packets with is in the ARP cache, which etharp_get_entry walks.
// Empty when the address cannot be resolved, which is normal for a client
// reached through a router rather than sitting on the same link.
String clientMacSuffix(const IPAddress &address) {
  const uint32_t wanted = static_cast<uint32_t>(address);
  // Two octets, not one: a single octet is 256 values, and at a busy con two
  // devices colliding is likelier than not once a couple of dozen have posted.
  char out[5] = {};

  if (isBadgeAccessPointActive() && SM_HAS_NETIF_STA_LIST) {
#if SM_HAS_NETIF_STA_LIST
    wifi_sta_list_t stations = {};
    esp_netif_sta_list_t leases = {};
    if (esp_wifi_ap_get_sta_list(&stations) == ESP_OK &&
        esp_netif_get_sta_list(&stations, &leases) == ESP_OK) {
      for (int i = 0; i < leases.num; ++i) {
        if (leases.sta[i].ip.addr == wanted) {
          snprintf(out, sizeof(out), "%02X%02X", leases.sta[i].mac[4],
                   leases.sta[i].mac[5]);
          return String(out);
        }
      }
    }
#endif
  }

  ip4_addr_t *entryIp = nullptr;
  struct netif *entryNetif = nullptr;
  struct eth_addr *entryMac = nullptr;
  for (size_t i = 0; i < ARP_TABLE_SIZE; ++i) {
    if (etharp_get_entry(i, &entryIp, &entryNetif, &entryMac) && entryIp &&
        entryMac && entryIp->addr == wanted) {
      snprintf(out, sizeof(out), "%02X%02X", entryMac->addr[4],
               entryMac->addr[5]);
      return String(out);
    }
  }
  return String();
}

void rememberAuthorName(uint16_t id, const String &raw, const String &mac) {
  if (id == 0 || (raw.length() == 0 && mac.length() == 0)) return;
  String name;
  name.reserve(AUTHOR_NAME_LENGTH);
  for (size_t i = 0; i < raw.length() && name.length() < AUTHOR_NAME_LENGTH - 1; ++i) {
    const char character = raw[i];
    // Only visible ASCII: this is echoed into JSON and then into the page.
    if (character >= 0x20 && character != 0x7F && character != '"' &&
        character != '\\') {
      name += character;
    }
  }
  name.trim();

  for (AuthorName &slot : authorNames) {
    if (slot.id == id) {
      if (name.length()) strlcpy(slot.name, name.c_str(), sizeof(slot.name));
      if (mac.length()) strlcpy(slot.mac, mac.c_str(), sizeof(slot.mac));
      return;
    }
  }
  AuthorName &slot = authorNames[authorNameNext];
  authorNameNext = (authorNameNext + 1) % AUTHOR_NAME_SLOTS;
  slot = AuthorName{};
  slot.id = id;
  strlcpy(slot.name, name.c_str(), sizeof(slot.name));
  strlcpy(slot.mac, mac.c_str(), sizeof(slot.mac));
}

const AuthorName *lookupAuthor(uint16_t id) {
  if (id == 0) return nullptr;
  for (const AuthorName &slot : authorNames) {
    if (slot.id == id && (slot.name[0] || slot.mac[0])) return &slot;
  }
  return nullptr;
}

void handleBoardPosts() {
  const uint32_t requestedBefore =
      server.hasArg("before")
          ? strtoul(server.arg("before").c_str(), nullptr, 10)
          : 0;
  const uint16_t limit =
      constrain(server.hasArg("limit") ? server.arg("limit").toInt() : 20,
                1, 40);

  addNoCacheHeaders();
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json", "");
  server.sendContent(F("{\"posts\":["));

  uint32_t cursor = requestedBefore;
  uint16_t sent = 0;
  BoardPost post;

  while (sent < limit && readNextBoardPost(cursor, post)) {
    String chunk;
    chunk.reserve(post.text.length() + 96);
    if (sent > 0) chunk += ',';
    chunk += F("{\"id\":");
    chunk += post.id;
    chunk += F(",\"createdAt\":");
    chunk += post.createdAt;
    chunk += F(",\"authorId\":");
    chunk += post.authorId;
    const AuthorName *author = lookupAuthor(post.authorId);
    if (author && author->name[0]) {
      chunk += F(",\"authorName\":\"");
      chunk += jsonEscape(String(author->name));
      chunk += '"';
    }
    if (author && author->mac[0]) {
      chunk += F(",\"authorMac\":\"");
      chunk += jsonEscape(String(author->mac));
      chunk += '"';
    }
    chunk += F(",\"textInImage\":");
    chunk += post.textInImage ? F("true") : F("false");
    chunk += F(",\"text\":\"");
    chunk += jsonEscape(post.text);
    chunk += F("\",\"hasImage\":");
    chunk += post.hasImage ? F("true") : F("false");
    chunk += '}';
    server.sendContent(chunk);
    ++sent;
  }

  bool more = false;
  if (sent == limit) {
    uint32_t probeCursor = cursor;
    BoardPost probe;
    more = readNextBoardPost(probeCursor, probe);
  }

  String tail;
  tail.reserve(120);
  tail += F("],\"nextBefore\":");
  tail += sent > 0 ? cursor : 0;
  tail += F(",\"more\":");
  tail += more ? F("true") : F("false");
  tail += F(",\"stored\":");
  tail += boardStoredCount();
  tail += '}';
  server.sendContent(tail);
  server.sendContent(F(""));
}

void handleBoardCreate() {
  addNoCacheHeaders();

  if (boardUploadTooBig) {
    server.send(400, "application/json",
                boardStateJson(false,
                               "The drawing is bigger than the badge can hold."));
    return;
  }

  const String text = percentDecode(server.header("X-Note-Text"));
  // The badge has no real-time clock, so the posting time can only ever be
  // what the browser claimed it was.
  const uint32_t createdAt =
      strtoul(server.header("X-Note-Ts").c_str(), nullptr, 10);

  const size_t imageLength = boardUploadLength;

  String error;
  const long claimedAuthor = server.header("X-Note-Author").toInt();
  // Deliberately the browser range only, not isStorableAuthorId(): a web
  // client must not be able to claim it is the NFC reader or the USB console.
  const uint16_t authorId = claimedAuthor >= BOARD_FIRST_BROWSER_AUTHOR_ID &&
                                    claimedAuthor <= BOARD_LAST_BROWSER_AUTHOR_ID
                                ? static_cast<uint16_t>(claimedAuthor)
                                : 0;
  rememberAuthorName(authorId, deviceLabelFromUserAgent(server.header("User-Agent")),
                     clientMacSuffix(server.client().remoteIP()));
  if (!addBoardPost(text, createdAt,
                    imageLength > 0 ? boardImageBuffer : nullptr, imageLength,
                    error, authorId,
                    server.header("X-Note-Text-In-Image") == "1")) {
    server.send(400, "application/json", boardStateJson(false, error));
    return;
  }

  server.send(201, "application/json", boardStateJson(true, "Note posted."));
}

void handleNfcCaptureSet() {
  addNoCacheHeaders();

  if (!server.hasArg("enabled")) {
    server.send(400, "application/json",
                F("{\"ok\":false,\"message\":\"The auto-scan setting is missing.\"}"));
    return;
  }

  const String value = server.arg("enabled");
  const bool enabled = value == "1" || value == "true";

  if (!setNfcCaptureEnabled(enabled)) {
    server.send(409, "application/json", getNfcStateJson());
    return;
  }

  server.send(200, "application/json", getNfcStateJson());
}

void handleBoardDelete() {
  addNoCacheHeaders();
  const uint32_t id = server.hasArg("id")
                          ? strtoul(server.arg("id").c_str(), nullptr, 10)
                          : 0;
  if (!deleteBoardPost(id)) {
    server.send(404, "application/json",
                boardStateJson(false, "That note is not on the board."));
    return;
  }
  server.send(200, "application/json", boardStateJson(true, "Note deleted."));
}

void handleBoardClear() {
  addNoCacheHeaders();
  if (!clearBoard()) {
    server.send(500, "application/json",
                boardStateJson(false, "Could not clear the board."));
    return;
  }
  server.send(200, "application/json",
              boardStateJson(true, "Every note was deleted."));
}

void handleNfcLogPage() {
  serveLittleFsFile("/nfclog.html", "text/html; charset=utf-8");
}

// Streams the unified NFC log newest-first, the same chunked way the Field
// Notes board does, so a long list never has to fit in one String.
void handleNfcBoard() {
  const uint32_t requestedBefore =
      server.hasArg("before")
          ? strtoul(server.arg("before").c_str(), nullptr, 10)
          : 0;
  const uint16_t limit =
      constrain(server.hasArg("limit") ? server.arg("limit").toInt() : 30, 1, 60);

  addNoCacheHeaders();
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json", "");
  server.sendContent(F("{\"tags\":["));

  uint32_t cursor = requestedBefore;
  uint16_t sent = 0;
  NfcLogEntry entry;
  while (sent < limit && nfcLogReadNext(cursor, entry)) {
    String chunk;
    chunk.reserve(entry.content.length() + entry.uid.length() + 96);
    if (sent > 0) chunk += ',';
    chunk += F("{\"id\":");
    chunk += entry.lastSeenId;
    chunk += F(",\"uid\":\"");
    chunk += jsonEscape(entry.uid);
    chunk += F("\",\"type\":\"");
    chunk += jsonEscape(entry.tagType);
    chunk += F("\",\"content\":\"");
    chunk += jsonEscape(entry.content);
    chunk += F("\",\"hits\":");
    chunk += entry.hitCount;
    chunk += '}';
    server.sendContent(chunk);
    ++sent;
  }

  String tail = F("],\"stored\":");
  tail += nfcLogStoredCount();
  tail += '}';
  server.sendContent(tail);
  server.sendContent(F(""));
}

void handleNfcBoardDelete() {
  addNoCacheHeaders();
  const uint32_t id = server.hasArg("id")
                          ? strtoul(server.arg("id").c_str(), nullptr, 10)
                          : 0;
  if (!deleteNfcLogEntry(id)) {
    server.send(404, "application/json",
                F("{\"ok\":false,\"message\":\"That tag is not in the log.\"}"));
    return;
  }
  server.send(200, "application/json",
              F("{\"ok\":true,\"message\":\"Tag deleted.\"}"));
}

void handleNfcBoardClear() {
  addNoCacheHeaders();
  if (!clearNfcLog()) {
    server.send(500, "application/json",
                F("{\"ok\":false,\"message\":\"Could not clear the NFC log.\"}"));
    return;
  }
  server.send(200, "application/json",
              F("{\"ok\":true,\"message\":\"NFC log cleared.\"}"));
}

// -----------------------------------------------------------------------------
// Captive portal
// -----------------------------------------------------------------------------
// Phones decide a network is "signed in" by fetching a known URL and checking
// for an exact response. Answering any of them with a redirect is what makes
// the sign-in notification appear, and what lands a walk-up visitor on the
// board without them typing an address.
void redirectToPortal() {
  String location = F("http://");
  location += AP_IP.toString();
  location += F("/notes");
  server.sendHeader("Location", location, true);
  addNoCacheHeaders();
  server.send(302, "text/plain; charset=utf-8", "");
}

// Keep old badge QR codes and bookmarks working, but expose the renamed page
// at its canonical address rather than leaving the browser on /board.
void redirectLegacyBoardPage() {
  server.sendHeader("Location", "/notes", true);
  addNoCacheHeaders();
  server.send(302, "text/plain; charset=utf-8", "");
}

bool requestIsForAnotherHost() {
  const String host = server.hostHeader();
  return host.length() > 0 && host != AP_IP.toString();
}

void handleNotFound() {
  if (server.uri().startsWith("/api/")) {
    server.send(404, "application/json", "{\"ok\":false,\"error\":\"No encontrado\"}");
    return;
  }

  if (requestIsForAnotherHost()) {
    redirectToPortal();
    return;
  }

  server.send(404, "text/plain; charset=utf-8",
              "Page not found. Open http://10.69.4.20/notes for the notes,"
              "http://10.69.4.20/led for the lights,"
              "http://10.69.4.20/nfc for NFC, or"
              "http://10.69.4.20/network for the network settings.");
}

// ---- USB HID payloads. The portal can author, store, load and delete payloads,
// and via /api/payloads/run it fires the current script straight at the connected
// computer. That has real-world effect and the captive portal is anonymous, so
// anyone on the badge's AP can inject keystrokes -- treat the AP as trusted.
// Shared status fields: whether a payload is running, the one-line status, the
// host keyboard's lock LEDs, and whether a USB host has been seen at all.
void appendPayloadStatus(String &json) {
  json += "\"busy\":";
  json += usbHidBusy() ? "true" : "false";
  json += ",\"status\":\"";
  json += jsonEscape(usbHidStatusLine());
  json += "\",\"locks\":";
  json += String(usbHidHostLeds());
  json += ",\"seen\":";
  json += usbHidHostSeen() ? "true" : "false";
  // Payload counts ride along on every status poll so a page can notice a list
  // change it did not make -- a DROP BOX import, the TUI, another browser -- and
  // refresh itself within one poll, without refetching the whole list each time.
  json += ",\"hidCount\":";
  json += String(usbHidPayloadCount());
  json += ",\"badusbCount\":";
  json += String(usbBadUSBPayloadCount());
}

void appendUsbNetworkStatus(String &json) {
  const UsbNetworkState network = getUsbNetworkState();
  json += F("\"network\":{\"available\":");
  json += network.available ? F("true") : F("false");
  json += F(",\"enabled\":");
  json += network.enabled ? F("true") : F("false");
  json += F(",\"stationConnected\":");
  json += network.stationConnected ? F("true") : F("false");
  json += F(",\"linkUp\":");
  json += network.linkUp ? F("true") : F("false");
  json += F(",\"sentFrames\":");
  json += String(network.sentFrames);
  json += F(",\"receivedFrames\":");
  json += String(network.receivedFrames);
  json += F(",\"droppedFrames\":");
  json += String(network.droppedFrames);
  json += F("}");
}

void appendUsbProfileStatus(String &json) {
  const UsbDriveState drive = getUsbDriveState();
  json += F("\"usbProfile\":\"");
  json += getPersistentUsbDeviceProfile() == UsbDeviceProfile::DRIVE ? F("drive") : F("network");
  json += F("\",\"drive\":{\"available\":");
  json += drive.available ? F("true") : F("false");
  json += F(",\"media\":");
  json += drive.mediaPresent ? F("true") : F("false");
  json += F(",\"notes\":");
  json += String(drive.noteCount);
  json += F(",\"scripts\":");
  json += String(drive.scriptCount);
  json += F(",\"badusbScripts\":");
  json += String(drive.badusbScriptCount);
  json += F(",\"tags\":");
  json += String(drive.tagCount);
  json += F("}");
}

// Lightweight status only -- safe to poll, reads no files.
String payloadsStatusJson() {
  String json;
  json.reserve(96);
  json += "{\"ok\":true,";
  appendPayloadStatus(json);
  json += "}";
  return json;
}

// USB Serial is intentionally not a profile choice. The badge remains a CDC
// console in every current and planned USB profile, while this endpoint exposes
// the HID controls and the physical BOOT-button bindings alongside it.
String usbControlsJson(bool ok, const String &error) {
  const UsbButtonState button = getUsbButtonState();
  String json;
  json.reserve(400);
  json += F("{\"ok\":");
  json += ok ? F("true") : F("false");
  json += F(",\"error\":\"");
  json += jsonEscape(error);
  json += F("\",\"serial\":true,\"profile\":\"serial-hid\",\"button\":{\"short\":\"");
  json += usbControlActionKey(button.shortPress);
  json += F("\",\"long\":\"");
  json += usbControlActionKey(button.longPress);
  json += F("\"},");
  appendPayloadStatus(json);
  json += ',';
  appendUsbNetworkStatus(json);
  json += ',';
  appendUsbProfileStatus(json);
  json += F("}");
  return json;
}

// Full listing: status plus every stored payload with its body, so the feed can
// show each script and "Load" needs no extra request. Called on demand, not
// polled -- payloads are few and small.
String payloadsListJson(bool ok, const String &error) {
  String json;
  json.reserve(512);
  json += "{\"ok\":";
  json += ok ? "true" : "false";
  json += ",\"error\":\"";
  json += jsonEscape(error);
  json += "\",";
  appendPayloadStatus(json);
  json += ",\"items\":[";
  const uint16_t count = usbHidPayloadCount();
  for (uint16_t i = 0; i < count; ++i) {
    if (i) json += ',';
    const String name = usbHidPayloadNameAt(i);
    String script;
    usbHidReadPayload(name, script);
    json += "{\"name\":\"";
    json += jsonEscape(name);
    json += "\",\"script\":\"";
    json += jsonEscape(script);
    json += "\"}";
  }
  json += "]}";
  return json;
}

void handlePayloadsPage() {
  serveLittleFsFile("/usb.html", "text/html; charset=utf-8");
}

void handleDuckyscriptPage() {
  serveLittleFsFile("/ducky.html", "text/html; charset=utf-8");
}

void handleBadUSBPage() {
  serveLittleFsFile("/badusb.html", "text/html; charset=utf-8");
}


void handlePayloadsList() {
  addNoCacheHeaders();
  server.send(200, "application/json", payloadsListJson(true, String()));
}

void handlePayloadStatus() {
  addNoCacheHeaders();
  server.send(200, "application/json", payloadsStatusJson());
}

void handlePayloadGet() {
  addNoCacheHeaders();
  const String name = server.hasArg("name") ? server.arg("name") : String();
  String script;
  if (!usbHidReadPayload(name, script)) {
    server.send(404, "application/json", "{\"ok\":false,\"error\":\"No existe.\"}");
    return;
  }
  String json;
  json.reserve(script.length() + 64);
  json += "{\"ok\":true,\"name\":\"";
  json += jsonEscape(name);
  json += "\",\"script\":\"";
  json += jsonEscape(script);
  json += "\"}";
  server.send(200, "application/json", json);
}

void handlePayloadSave() {
  addNoCacheHeaders();
  const String name = server.hasArg("name") ? server.arg("name") : String();
  const String script = server.hasArg("script") ? server.arg("script") : String();
  String error;
  if (!usbHidSavePayload(name, script, error)) {
    server.send(400, "application/json", payloadsListJson(false, error));
    return;
  }
  server.send(201, "application/json", payloadsListJson(true, String()));
}

void handlePayloadDelete() {
  addNoCacheHeaders();
  const String name = server.hasArg("name") ? server.arg("name") : String();
  String error;
  // A normal Ofrenda move already removed its source. Clean up any legacy or
  // failed source copy when the volume is not owned by the host.
  if (!usbDropboxDeletePayloadSource(false, name, error)) {
    server.send(409, "application/json", payloadsListJson(false, error));
    return;
  }
  if (!usbHidDeletePayload(name, error)) {
    server.send(400, "application/json", payloadsListJson(false, error));
    return;
  }
  server.send(200, "application/json", payloadsListJson(true, String()));
}

// Fires the given Ducky script live on the connected computer. Unlike the rest
// of this API this actually types on the host, so it is the one portal action
// with physical-world effect -- callable by anyone joined to the badge's AP.
void handlePayloadRun() {
  addNoCacheHeaders();
  const String script = server.hasArg("script") ? server.arg("script") : String();
  String error;
  if (!usbHidRunScript(script, error)) {
    server.send(409, "application/json", payloadsListJson(false, error));
    return;
  }
  server.send(202, "application/json", payloadsListJson(true, String()));
}

// ---- BadUSB Payloads ----

String badUSBListJson(bool ok, const String &error) {
  String json;
  json.reserve(512);
  json += "{\"ok\":";
  json += ok ? "true" : "false";
  json += ",\"error\":\"";
  json += jsonEscape(error);
  json += "\",\"items\":[";
  const uint16_t count = usbBadUSBPayloadCount();
  for (uint16_t i = 0; i < count; ++i) {
    if (i) json += ',';
    const String name = usbBadUSBPayloadNameAt(i);
    String script;
    usbBadUSBReadPayload(name, script);
    json += "{\"name\":\"";
    json += jsonEscape(name);
    json += "\",\"script\":\"";
    json += jsonEscape(script);
    json += "\"}";
  }
  json += "]}";
  return json;
}

void handleBadUSBList() {
  addNoCacheHeaders();
  server.send(200, "application/json", badUSBListJson(true, String()));
}

void handleBadUSBGet() {
  addNoCacheHeaders();
  const String name = server.hasArg("name") ? server.arg("name") : String();
  String script;
  if (!usbBadUSBReadPayload(name, script)) {
    server.send(404, "application/json", "{\"ok\":false,\"error\":\"No existe.\"}");
    return;
  }
  String json;
  json.reserve(script.length() + 64);
  json += "{\"ok\":true,\"name\":\"";
  json += jsonEscape(name);
  json += "\",\"script\":\"";
  json += jsonEscape(script);
  json += "\"}";
  server.send(200, "application/json", json);
}

void handleBadUSBSave() {
  addNoCacheHeaders();
  const String name = server.hasArg("name") ? server.arg("name") : String();
  const String script = server.hasArg("script") ? server.arg("script") : String();
  String error;
  if (!usbBadUSBSavePayload(name, script, error)) {
    server.send(400, "application/json", badUSBListJson(false, error));
    return;
  }
  server.send(201, "application/json", badUSBListJson(true, String()));
}

void handleBadUSBDelete() {
  addNoCacheHeaders();
  const String name = server.hasArg("name") ? server.arg("name") : String();
  String error;
  if (!usbDropboxDeletePayloadSource(true, name, error)) {
    server.send(409, "application/json", badUSBListJson(false, error));
    return;
  }
  if (!usbBadUSBDeletePayload(name, error)) {
    server.send(400, "application/json", badUSBListJson(false, error));
    return;
  }
  server.send(200, "application/json", badUSBListJson(true, String()));
}

void handleBadUSBRun() {
  addNoCacheHeaders();
  const String script = server.hasArg("script") ? server.arg("script") : String();
  String error;
  if (!usbBadUSBRunScript(script, error)) {
    server.send(409, "application/json", badUSBListJson(false, error));
    return;
  }
  server.send(202, "application/json", badUSBListJson(true, String()));
}

void handleUsbControlsGet() {
  addNoCacheHeaders();
  server.send(200, "application/json", usbControlsJson(true, String()));
}

void handleUsbControlRun() {
  addNoCacheHeaders();
  UsbControlAction action = UsbControlAction::NONE;
  if (!server.hasArg("action") ||
      !usbControlActionFromKey(server.arg("action"), action) ||
      action == UsbControlAction::NONE ||
      action == UsbControlAction::LED_CONTROLS) {
    server.send(400, "application/json",
                usbControlsJson(false, "Invalid USB action."));
    return;
  }
  if (action == UsbControlAction::SYSTEM_POWER_OFF &&
      (!server.hasArg("confirm") || server.arg("confirm") != "power-off")) {
    server.send(400, "application/json",
                usbControlsJson(false,
                                "Confirm powering off the computer before sending it."));
    return;
  }

  String error;
  if (!usbHidRunControl(action, error)) {
    server.send(409, "application/json", usbControlsJson(false, error));
    return;
  }
  server.send(202, "application/json", usbControlsJson(true, String()));
}

void handleUsbButtonSet() {
  addNoCacheHeaders();
  UsbControlAction shortPress = UsbControlAction::NONE;
  UsbControlAction longPress = UsbControlAction::NONE;
  if (!server.hasArg("short") || !server.hasArg("long") ||
      !usbControlActionFromKey(server.arg("short"), shortPress) ||
      !usbControlActionFromKey(server.arg("long"), longPress)) {
    server.send(400, "application/json",
                usbControlsJson(false, "Invalid button action."));
    return;
  }
  if (shortPress == UsbControlAction::SYSTEM_POWER_OFF ||
      longPress == UsbControlAction::SYSTEM_POWER_OFF) {
    server.send(400, "application/json",
                usbControlsJson(false,
                                "Powering off the computer cannot be assigned to the button."));
    return;
  }

  String error;
  if (!setUsbButtonState(shortPress, longPress, error)) {
    server.send(500, "application/json", usbControlsJson(false, error));
    return;
  }
  server.send(200, "application/json", usbControlsJson(true, String()));
}

void handleUsbProfileSet() {
  const String requested = server.hasArg("profile") ? server.arg("profile") : String();
  const UsbDeviceProfile profile = requested == "drive" ? UsbDeviceProfile::DRIVE
                                  : requested == "network" ? UsbDeviceProfile::NETWORK
                                                           : static_cast<UsbDeviceProfile>(255);
  String error;
  if (!setPersistentUsbDeviceProfile(profile, error)) {
    server.send(400, "application/json", usbControlsJson(false, error));
    return;
  }
  server.send(200, "application/json", usbControlsJson(true, String()));
  delay(150);
  ESP.restart();
}

// VID/PID are written and read as bare hex (an optional leading "0x" is
// tolerated), matching how every USB database and lsusb itself prints them.
uint16_t parseHexUint16(const String &value) {
  String trimmed = value;
  trimmed.trim();
  if (trimmed.startsWith("0x") || trimmed.startsWith("0X")) trimmed.remove(0, 2);
  return static_cast<uint16_t>(strtoul(trimmed.c_str(), nullptr, 16));
}

String hexUint16(uint16_t value) {
  char buffer[5];
  snprintf(buffer, sizeof(buffer), "%04x", value);
  return String(buffer);
}

String usbIdentityJson(bool ok, const String &error) {
  StoredUsbIdentitySettings identity = {};
  loadUsbIdentitySettings(identity);
  String json;
  json.reserve(320);
  json += "{\"ok\":";
  json += ok ? "true" : "false";
  json += ",\"error\":\"";
  json += jsonEscape(error);
  json += "\",\"enabled\":";
  json += identity.enabled ? "true" : "false";
  json += ",\"vid\":\"";
  json += hexUint16(identity.vid);
  json += "\",\"pid\":\"";
  json += hexUint16(identity.pid);
  json += "\",\"manufacturer\":\"";
  json += jsonEscape(identity.manufacturer);
  json += "\",\"product\":\"";
  json += jsonEscape(identity.product);
  json += "\",\"serial\":\"";
  json += jsonEscape(identity.serial);
  json += "\",\"hidReportSet\":\"";
  json += identity.hidReportSet == static_cast<uint8_t>(UsbHidReportSet::KEYBOARD_ONLY)
              ? "keyboard"
              : "full";
  json += "\",\"defaultManufacturer\":\"";
  json += jsonEscape(F(USB_MANUFACTURER));
  json += "\",\"defaultProduct\":\"";
  json += jsonEscape(F(USB_PRODUCT));
  json += "\"}";
  return json;
}

void handleUsbIdentityGet() {
  addNoCacheHeaders();
  server.send(200, "application/json", usbIdentityJson(true, String()));
}

// Saving here always reboots on success: a new identity only takes effect at
// the next USB.begin(), the same as switching USB profile already requires.
void handleUsbIdentitySet() {
  addNoCacheHeaders();
  StoredUsbIdentitySettings identity = {};
  identity.enabled = server.hasArg("enabled") && server.arg("enabled") == "true";
  identity.vid = parseHexUint16(server.hasArg("vid") ? server.arg("vid") : String());
  identity.pid = parseHexUint16(server.hasArg("pid") ? server.arg("pid") : String());
  identity.hidReportSet =
      server.hasArg("hidReportSet") && server.arg("hidReportSet") == "keyboard"
          ? static_cast<uint8_t>(UsbHidReportSet::KEYBOARD_ONLY)
          : static_cast<uint8_t>(UsbHidReportSet::FULL);
  const String manufacturer = server.hasArg("manufacturer") ? server.arg("manufacturer") : String();
  const String product = server.hasArg("product") ? server.arg("product") : String();
  const String serial = server.hasArg("serial") ? server.arg("serial") : String();
  strncpy(identity.manufacturer, manufacturer.c_str(), sizeof(identity.manufacturer) - 1);
  strncpy(identity.product, product.c_str(), sizeof(identity.product) - 1);
  strncpy(identity.serial, serial.c_str(), sizeof(identity.serial) - 1);

  String error;
  if (!saveUsbIdentitySettings(identity, error)) {
    server.send(400, "application/json", usbIdentityJson(false, error));
    return;
  }
  server.send(200, "application/json", usbIdentityJson(true, String()));
  delay(150);
  ESP.restart();
}

// The Terminal page mirrors the USB console byte for byte, so this transport
// only ever carries bytes: what the console has printed since the page last
// asked, and whatever has been typed into the page. Base64 is what makes that
// safe through JSON -- the stream is full of ANSI escapes, and the mirror ring
// can hand back a UTF-8 character cut in half at its boundary. The page
// reassembles both with a streaming decoder.
String base64Encode(const String &value) {
  static const char alphabet[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  String encoded;
  const size_t length = value.length();
  encoded.reserve(((length + 2) / 3) * 4 + 1);
  for (size_t i = 0; i < length; i += 3) {
    const size_t remaining = length - i;
    const uint32_t block =
        (static_cast<uint32_t>(static_cast<uint8_t>(value[i])) << 16) |
        (remaining > 1 ? static_cast<uint32_t>(static_cast<uint8_t>(value[i + 1])) << 8 : 0) |
        (remaining > 2 ? static_cast<uint32_t>(static_cast<uint8_t>(value[i + 2])) : 0);
    encoded += alphabet[(block >> 18) & 0x3F];
    encoded += alphabet[(block >> 12) & 0x3F];
    encoded += remaining > 1 ? alphabet[(block >> 6) & 0x3F] : '=';
    encoded += remaining > 2 ? alphabet[block & 0x3F] : '=';
  }
  return encoded;
}

void handleTerminalPage() {
  serveLittleFsFile("/terminal.html", "text/html; charset=utf-8");
}

void handleTerminalStream() {
  addNoCacheHeaders();
  // Polling is what keeps capture alive, and it is also what asks for the
  // first frame, so this comes before the read rather than after it.
  usbTuiMirrorOpen();
  const uint32_t since =
      server.hasArg("since")
          ? static_cast<uint32_t>(strtoul(server.arg("since").c_str(), nullptr, 10))
          : 0;
  uint32_t sequence = 0;
  bool resynchronised = false;
  const String pending = usbTuiMirrorRead(since, sequence, resynchronised);
  String body = "{\"sequence\":";
  body += sequence;
  body += ",\"reset\":";
  body += resynchronised ? "true" : "false";
  body += ",\"data\":\"";
  body += base64Encode(pending);
  body += "\"}";
  server.send(200, "application/json", body);
}

void handleTerminalKey() {
  addNoCacheHeaders();
  if (!server.hasArg("keys")) {
    server.send(400, "application/json", "{\"ok\":false}");
    return;
  }
  usbTuiInjectKeys(server.arg("keys"));
  server.send(200, "application/json", "{\"ok\":true}");
}

void setupWebServer() {
  // The portal opens on the board. "/" used to be a dashboard whose first
  // element was a row of links onward, so landing there was navigable; it is
  // now the Wi-Fi form alone, which reads exactly like a captive-portal gate
  // demanding setup before you may continue. Network configuration is at
  // /network; the former settings paths stay available for old bookmarks.
  server.on("/", HTTP_GET, handleBoardPage);
  server.on("/network", HTTP_GET, handleDashboardPage);
  server.on("/network.html", HTTP_GET, handleDashboardPage);
  server.on("/settings", HTTP_GET, handleDashboardPage);
  server.on("/settings.html", HTTP_GET, handleDashboardPage);
  server.on("/index.html", HTTP_GET, handleDashboardPage);
  server.on("/theme.css", HTTP_GET, handleThemeStylesheet);
  server.on("/locale.js", HTTP_GET, handleLocaleScript);
  server.on("/tape.js", HTTP_GET, handleTapeScript);
  server.on("/assets/logo-candle.png", HTTP_GET, handleLogoAsset);
  server.on("/assets/badge-figure.png", HTTP_GET, handleBadgeFigureAsset);
  server.on("/led", HTTP_GET, handleLedPage);
  server.on("/led.html", HTTP_GET, handleLedPage);
  server.on("/terminal", HTTP_GET, handleTerminalPage);
  server.on("/terminal.html", HTTP_GET, handleTerminalPage);
  server.on("/api/terminal/stream", HTTP_GET, handleTerminalStream);
  server.on("/api/terminal/key", HTTP_POST, handleTerminalKey);
  server.on("/nfc", HTTP_GET, handleNfcPage);
  server.on("/nfc.html", HTTP_GET, handleNfcPage);
  // /nfc-log is the page's address. The two /nfc-board spellings stay
  // answerable so a bookmark or a cached menu from the old name still lands on
  // it rather than on the captive-portal catch-all.
  server.on("/nfc-log", HTTP_GET, handleNfcLogPage);
  server.on("/nfclog.html", HTTP_GET, handleNfcLogPage);
  server.on("/nfc-board", HTTP_GET, handleNfcLogPage);
  server.on("/nfcboard.html", HTTP_GET, handleNfcLogPage);
  server.on("/notes", HTTP_GET, handleBoardPage);
  server.on("/board", HTTP_GET, redirectLegacyBoardPage);
  server.on("/board.html", HTTP_GET, redirectLegacyBoardPage);

  server.on("/api/state", HTTP_GET, handleLedState);
  server.on("/api/set", HTTP_GET, handleLedSet);
  server.on("/api/led/pixels", HTTP_GET, handleLedPixels);
  server.on("/api/led/identify", HTTP_GET, handleLedIdentify);

  server.on("/api/wifi/settings", HTTP_GET, handleWifiSettingsGet);
  server.on("/api/wifi/settings", HTTP_POST, handleWifiSettingsSet);
  server.on("/api/wifi/ap", HTTP_POST, handleAccessPointSet);
  server.on("/api/wifi/client", HTTP_GET, handleStationWifiGet);
  server.on("/api/wifi/scan", HTTP_GET, handleWifiScan);
  server.on("/api/wifi/client", HTTP_POST, handleStationWifiSet);
  server.on("/api/ui/language", HTTP_GET, handleLanguageGet);
  server.on("/api/ui/language", HTTP_POST, handleLanguageSet);

  server.on("/api/board/state", HTTP_GET, handleBoardState);
  server.on("/api/board/posts", HTTP_GET, handleBoardPosts);
  server.on("/api/board/image", HTTP_GET, handleBoardImage);
  // The fourth argument is the body receiver. Registering one is what makes the
  // server stream this route's body instead of parsing it into Strings.
  server.on("/api/board/post", HTTP_POST, handleBoardCreate, handleBoardUpload);
  server.on("/api/board/delete", HTTP_POST, handleBoardDelete);
  server.on("/api/board/clear", HTTP_POST, handleBoardClear);
  server.on("/api/nfc/board", HTTP_GET, handleNfcBoard);
  server.on("/api/nfc/board/delete", HTTP_POST, handleNfcBoardDelete);
  server.on("/api/nfc/board/clear", HTTP_POST, handleNfcBoardClear);

  server.on("/usb", HTTP_GET, handlePayloadsPage);
  server.on("/usb.html", HTTP_GET, handlePayloadsPage);
  server.on("/ducky", HTTP_GET, handleDuckyscriptPage);
  server.on("/ducky.html", HTTP_GET, handleDuckyscriptPage);
  server.on("/badusb", HTTP_GET, handleBadUSBPage);
  server.on("/badusb.html", HTTP_GET, handleBadUSBPage);
  server.on("/api/payloads/list", HTTP_GET, handlePayloadsList);
  server.on("/api/payloads/status", HTTP_GET, handlePayloadStatus);
  server.on("/api/payloads/get", HTTP_GET, handlePayloadGet);
  server.on("/api/payloads/save", HTTP_POST, handlePayloadSave);
  server.on("/api/payloads/delete", HTTP_POST, handlePayloadDelete);
  server.on("/api/payloads/run", HTTP_POST, handlePayloadRun);
  server.on("/api/payloads/badusb/list", HTTP_GET, handleBadUSBList);
  server.on("/api/payloads/badusb/get", HTTP_GET, handleBadUSBGet);
  server.on("/api/payloads/badusb/save", HTTP_POST, handleBadUSBSave);
  server.on("/api/payloads/badusb/delete", HTTP_POST, handleBadUSBDelete);
  server.on("/api/payloads/badusb/run", HTTP_POST, handleBadUSBRun);
  server.on("/api/usb/controls", HTTP_GET, handleUsbControlsGet);
  server.on("/api/usb/control", HTTP_POST, handleUsbControlRun);
  server.on("/api/usb/button", HTTP_POST, handleUsbButtonSet);
  server.on("/api/usb/profile", HTTP_POST, handleUsbProfileSet);
  server.on("/api/usb/identity", HTTP_GET, handleUsbIdentityGet);
  server.on("/api/usb/identity", HTTP_POST, handleUsbIdentitySet);

  server.on("/api/nfc/state", HTTP_GET, handleNfcState);
  server.on("/api/nfc/read", HTTP_POST, handleNfcRead);
  server.on("/api/nfc/write", HTTP_POST, handleNfcWrite);
  server.on("/api/nfc/emulation/start", HTTP_POST, handleNfcTagEmulationStart);
  server.on("/api/nfc/emulation/wifi", HTTP_POST, handleNfcWifiOnboardingStart);
  server.on("/api/nfc/emulation/stop", HTTP_POST, handleNfcTagEmulationStop);
  server.on("/api/nfc/capture", HTTP_POST, handleNfcCaptureSet);

  server.on("/favicon.ico", HTTP_GET, []() { server.send(204, "text/plain", ""); });

  // Connectivity probes, per platform: Android, then iOS and macOS, then
  // Windows, then Firefox.
  const char *const portalProbes[] = {
      "/generate_204", "/gen_204",
      "/hotspot-detect.html", "/library/test/success.html",
      "/ncsi.txt", "/connecttest.txt", "/redirect", "/fwlink",
      "/canonical.html", "/success.txt"};
  for (const char *probe : portalProbes) {
    server.on(probe, HTTP_GET, redirectToPortal);
  }

  server.onNotFound(handleNotFound);

  // WebServer discards every header it was not told to keep. User-Agent is the
  // only identity a browser offers for naming a note; the X-Note-*
  // ones carry the metadata that used to share the body with the drawing.
  static const char *collected[] = {"User-Agent", "X-Note-Text",
                                    "X-Note-Ts", "X-Note-Author",
                                    "X-Note-Text-In-Image"};
  server.collectHeaders(collected, sizeof(collected) / sizeof(collected[0]));

  server.begin();
  Serial.println("[WEB] HTTP server started");
}

}  // namespace

// Drains what the NFC reader captured while capture mode was on. This runs on
// the Arduino loop task, the only task that writes the board: the reader task
// stages payloads in a queue instead of touching LittleFS from a second core.
static String uidToStringForLog(const uint8_t *uid, uint8_t length) {
  String out;
  char b[4];
  for (uint8_t i = 0; i < length; ++i) {
    snprintf(b, sizeof(b), "%02X", uid[i]);
    if (i) out += ':';
    out += b;
  }
  return out;
}

void serviceNfcCapture() {
  NfcCapturedTag tag;
  // One per pass so a burst of tags cannot stall the web server.
  if (!takeNfcCapture(tag)) return;

  if (!nfcLogRecord(tag.uid, tag.uidLength, String(tag.tagType),
                    String(tag.content))) {
    Serial.println("[NFCLOG] Capture rejected by storage");
    // Whoever tapped is most likely not on the access point, so the failure has
    // to be visible on the badge or it is invisible entirely.
    signalTagCue(false);
    return;
  }

  noteNfcCapturePosted();
  signalTagCue(true);
  Serial.printf("[NFCLOG] Recorded %s on the NFC log\r\n",
                uidToStringForLog(tag.uid, tag.uidLength).c_str());
}

const char *getBadgeWifiSsid() {
  return apSsid;
}

const char *getBadgeWifiPassword() {
  return getPersistentWifiPassword();
}

bool getBadgeWifiApMac(uint8_t outMac[6]) {
  if (!outMac || apSsid[0] == '\0') return false;
  return WiFi.softAPmacAddress(outMac) != nullptr;
}

bool isBadgeAccessPointActive() {
  return accessPointActive;
}

bool setBadgeAccessPointEnabled(bool enabled, String &error) {
  if (!setPersistentAccessPointEnabled(enabled, error)) return false;
  requestedAccessPointEnabled = enabled;
  accessPointTogglePending = true;
  accessPointToggleAt = millis() + 50;
  return true;
}

WifiTuiState getWifiTuiState() {
  WifiTuiState state;
  state.accessPointActive = accessPointActive;
  state.accessPointSelected = getPersistentAccessPointEnabled();
  state.homeConfigured = hasPersistentStationWifiSettings();
  state.homeConnected = WiFi.status() == WL_CONNECTED;
  state.trialActive = trialActive;
  state.trialFailed = !trialActive && trialFailedSsid.length() > 0 &&
                      WiFi.status() != WL_CONNECTED;
  state.trialSsid = trialActive ? trialSsid : trialFailedSsid;
  state.hidden = getPersistentWifiHidden();
  state.accessPointSsid = apSsid;
  state.homeSsid = getPersistentStationWifiSsid();
  state.localIp = state.homeConnected ? WiFi.localIP().toString() : String();
  state.hostname = WiFi.getHostname() ? WiFi.getHostname() : STATION_HOSTNAME;
  return state;
}

bool setBadgeAccessPointSettings(const String &ssid, const String &password,
                                 bool hidden, String &error) {
  if (!setPersistentWifiSettings(password, hidden, error)) return false;
  if (!setPersistentAccessPointSsid(ssid, error)) return false;
  refreshAccessPointSsid();
  if (!getPersistentAccessPointEnabled()) return true;
  refreshWifiNfcAfterRestart = isNfcWifiOnboardingActive();
  accessPointRestartPending = true;
  accessPointRestartAt = millis() + 50;
  return true;
}

bool setBadgeHomeWifiSettings(const String &ssid, const String &password,
                              String &error, uint32_t apHandoverDelayMs) {
  error = getStationWifiCredentialError(ssid, password);
  if (error.length() > 0) return false;
  // Nothing reaches NVS here. These credentials are a trial: they are written
  // only once the badge has associated with the network, so a network it
  // cannot actually join never enters the list it retries on every boot.
  trialSsid = ssid;
  trialPassword = password;
  trialActive = true;
  trialStartedAt = millis();
  trialFailedSsid = String();
  // The trial is judged on its own clock, not on however long the badge
  // happened to be offline before it was asked to try.
  stationOfflineSince = 0;
  stationAttempts = 0;
  stationLastFailure = String();
  if (!setPersistentAccessPointEnabled(false, error)) {
    clearStationTrial();
    return false;
  }
  requestedAccessPointEnabled = false;
  accessPointTogglePending = accessPointActive;
  accessPointToggleAt = millis() + apHandoverDelayMs;
  if (!accessPointActive) startStationConnection();
  return true;
}

void setupWiFiAccessPoint() {
  Serial.println("[WIFI] Mounting LittleFS");
  fileSystemReady = LittleFS.begin(true);
  if (!fileSystemReady) {
    Serial.println("[WIFI] ERROR: LittleFS mount failed");
  } else {
    Serial.printf("[WIFI] /index.html: %s\r\n",
                  LittleFS.exists("/index.html") ? "ready" : "missing");
    Serial.printf("[WIFI] /led.html: %s\r\n",
                  LittleFS.exists("/led.html") ? "ready" : "missing");
    Serial.printf("[WIFI] /nfc.html: %s\r\n",
                  LittleFS.exists("/nfc.html") ? "ready" : "missing");
    Serial.printf("[WIFI] /board.html: %s\r\n",
                  LittleFS.exists("/board.html") ? "ready" : "missing");
    Serial.printf("[WIFI] /terminal.html: %s\r\n",
                  LittleFS.exists("/terminal.html") ? "ready" : "missing");
    Serial.printf("[WIFI] /nfclog.html: %s\r\n",
                  LittleFS.exists("/nfclog.html") ? "ready" : "missing");
  }

  // Resolve and verify the persistent password before starting Wi-Fi.
  if (!initializeBadgeSettings()) {
    Serial.println(
        "[WIFI] ERROR: Persistent credentials unavailable; access point "
        "was not started");
    return;
  }
  refreshAccessPointSsid();

  // Boot policy is intentionally independent of the AP/home setting that was
  // left by the last session: configured saved Wi-Fi always gets the first
  // connection attempt. The AP starts only if there is no saved Wi-Fi
  // or that attempt later times out in serviceStationConnection().
  bootHomeConnectionPending = hasPersistentStationWifiSettings();
  requestedAccessPointEnabled = false;
  accessPointActive = false;
  if (bootHomeConnectionPending) {
    Serial.printf("[WIFI] Boot: trying saved Wi-Fi first: %s\r\n",
                  getPersistentStationWifiSsid());
    WiFi.mode(WIFI_OFF);
    if (!WiFi.setHostname(STATION_HOSTNAME)) {
      Serial.println("[WIFI] WARNING: Could not set DHCP hostname");
    }
    WiFi.mode(WIFI_STA);
    refreshStationCandidates();
  } else {
    Serial.println("[WIFI] Boot: no saved Wi-Fi; starting access point");
    String error;
    if (!setPersistentAccessPointEnabled(true, error)) {
      Serial.printf("[WIFI] WARNING: Could not save AP boot mode: %s\r\n",
                    error.c_str());
    }
    requestedAccessPointEnabled = true;
    if (!configureVerifiedAccessPoint()) return;
  }

  setupWebServer();
  if (accessPointActive) startCaptivePortalDns();
  if (bootHomeConnectionPending) startStationConnection(true);

  Serial.printf("[WIFI] Badge AP: %s\r\n", accessPointActive ? "on" : "off");
  if (accessPointActive) {
    Serial.printf("[WIFI] SSID: %s\r\n", apSsid);
    Serial.printf("[WIFI] SSID visibility: %s\r\n",
                  getPersistentWifiHidden() ? "hidden" : "visible");
    Serial.printf("[WIFI] Security: %s\r\n",
                  getPersistentWifiPassword()[0] ? "WPA2" : "open");
    Serial.print("[WIFI] Dashboard: http://");
    Serial.println(WiFi.softAPIP());
  }
  Serial.print("[WIFI] Message board: http://");
  Serial.print(WiFi.softAPIP());
  Serial.println("/notes");
  Serial.print("[WIFI] LED controller: http://");
  Serial.print(WiFi.softAPIP());
  Serial.println("/led");
  Serial.print("[WIFI] NFC tools: http://");
  Serial.print(WiFi.softAPIP());
  Serial.println("/nfc");
}

void updateWebServer() {
  if (dnsServerRunning) dnsServer.processNextRequest();
  server.handleClient();
  servicePendingAccessPointRestart();
  servicePendingAccessPointToggle();
  serviceStationConnection();
}
