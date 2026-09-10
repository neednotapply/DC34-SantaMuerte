#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <LittleFS.h>
#include <esp_wifi.h>
#include <esp_netif_sta_list.h>
#include <lwip/etharp.h>
#include <cstring>
#include "badge_wifi.h"
#include "badge_settings.h"
#include "board.h"
#include "nfc.h"

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
constexpr uint32_t STATION_RETRY_MS = 20000;
// Home mode is exclusive: joining the house network takes the badge's own
// access point down, so a join that never succeeds used to leave the badge
// reachable over USB and nothing else. After this long with no connection it
// gives up and brings the AP back, which is the only route left to a portal
// that can change the setting. Roughly six retry cycles -- long enough to ride
// out a router restart, short enough that nobody goes looking for a cable.
constexpr uint32_t STATION_FALLBACK_MS = 120000;

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
  if (passwordLength < 8 || passwordLength > 63) {
    Serial.println(
        "[WIFI] ERROR: No verified WPA2 Wi-Fi passphrase is available");
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

void createMacDerivedSsid() {
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
    error += " no existe. Carga la imagen LittleFS y prueba otra vez.";
    server.send(500, "text/plain; charset=utf-8", error);
    return;
  }

  File page = LittleFS.open(path, "r");
  if (!page) {
    String error = "No se pudo abrir ";
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

void startStationConnection() {
  // Home Wi-Fi is the alternate mode, never a second network alongside the
  // badge AP. Leaving AP mode selected also suppresses automatic reconnects.
  if (getPersistentAccessPointEnabled() ||
      !hasPersistentStationWifiSettings()) return;

  stopMdns();
  // setHostname applies to the station interface; it must be set before
  // begin() for DHCP and the local network to see SantaMuerte consistently.
  WiFi.mode(accessPointActive ? WIFI_AP_STA : WIFI_STA);
  WiFi.disconnect(false, false);
  WiFi.begin(getPersistentStationWifiSsid(),
             getPersistentStationWifiPassword());
  stationConnectionRequested = true;
  stationConnectionStartedAt = millis();
  Serial.printf("[WIFI] Joining home Wi-Fi: %s\r\n",
                getPersistentStationWifiSsid());
}

void serviceStationConnection() {
  if (getPersistentAccessPointEnabled()) {
    stationConnectionRequested = false;
    // Whether this was the operator's choice or the fallback below, the next
    // switch to home Wi-Fi starts its own clock rather than inheriting one.
    stationOfflineSince = 0;
    stopMdns();
    return;
  }
  if (!hasPersistentStationWifiSettings()) return;

  if (WiFi.status() == WL_CONNECTED) {
    stationOfflineSince = 0;
    if (!mdnsRunning) {
      if (MDNS.begin(STATION_HOSTNAME)) {
        MDNS.addService("http", "tcp", HTTP_PORT);
        mdnsRunning = true;
        stationAttempts = 0;
        stationLastFailure = String();
        Serial.printf("[WIFI] Home Wi-Fi connected: %s // http://%s.local/\r\n",
                      WiFi.localIP().toString().c_str(), STATION_HOSTNAME);
      } else {
        Serial.println("[WIFI] WARNING: Could not start mDNS");
      }
    }
    return;
  }

  stopMdns();

  const uint32_t now = millis();
  if (stationOfflineSince == 0) stationOfflineSince = now;
  if (now - stationOfflineSince >= STATION_FALLBACK_MS) {
    Serial.printf("[WIFI] No home Wi-Fi for %lus -- bringing the badge access "
                  "point back so the portal stays reachable.\r\n",
                  static_cast<unsigned long>((now - stationOfflineSince) / 1000));
    String error;
    if (setBadgeAccessPointEnabled(true, error)) {
      // Persisted deliberately: a reboot must not strand the badge again on a
      // network that is not there. Home Wi-Fi stays saved, so switching back is
      // one tap in the portal once the network is.
      stationConnectionRequested = false;
      stationOfflineSince = 0;
      stationAttempts = 0;
      stationLastFailure = String();
    } else {
      Serial.printf("[WIFI] Could not fall back to the access point: %s\r\n",
                    error.c_str());
      stationOfflineSince = now;   // try the fallback again next cycle
    }
    return;
  }

  if (!stationConnectionRequested ||
      now - stationConnectionStartedAt >= STATION_RETRY_MS) {
    if (stationConnectionRequested) {
      const int status = WiFi.status();
      ++stationAttempts;
      stationLastFailure = stationStatusName(status);
      Serial.printf("[WIFI] Home Wi-Fi attempt %lu failed: %s (status %d)\r\n",
                    static_cast<unsigned long>(stationAttempts),
                    stationStatusName(status), status);
      if (status == WL_NO_SSID_AVAIL) {
        // The single most common cause, and invisible without saying it: this
        // radio is 2.4 GHz only, so a 5 GHz SSID simply is not there.
        Serial.printf("[WIFI]   '%s' is not visible. This radio is 2.4 GHz "
                      "only -- a 5 GHz SSID cannot be joined. Check the name "
                      "and the band.\r\n",
                      getPersistentStationWifiSsid());
      }
    }
    startStationConnection();
  }
}

String stationWifiStatusText() {
  if (!hasPersistentStationWifiSettings()) return F("Red de casa sin configurar.");
  if (WiFi.status() == WL_CONNECTED) return F("Conectado a la red de casa.");
  if (stationConnectionRequested && stationLastFailure.length() > 0) {
    return String(F("Conectando a la red de casa… ")) + stationLastFailure;
  }
  if (stationConnectionRequested) return F("Conectando a la red de casa…");
  return F("La red de casa no está conectada.");
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
    server.send(400, "application/json", languageJson(false, "Falta el idioma."));
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

  if (!server.hasArg("password") || !server.hasArg("hidden")) {
    server.send(
        400,
        "application/json",
        wifiSettingsJson(false, "Faltan la contraseña y el ajuste de red oculta."));
    return;
  }

  const String password = server.arg("password");
  const String hiddenValue = server.arg("hidden");
  const bool hidden =
      hiddenValue == "1" || hiddenValue == "true" || hiddenValue == "on";

  // Saving identical settings used to restart the access point anyway, which
  // drops every client. Landing on this page and pressing save then looked
  // like a loop: reconnect, portal reopens, same form.
  const bool unchanged = password == getPersistentWifiPassword() &&
                         hidden == getPersistentWifiHidden();

  String error;
  if (!setPersistentWifiSettings(password, hidden, error)) {
    server.send(400, "application/json", wifiSettingsJson(false, error));
    return;
  }

  if (unchanged) {
    server.send(200, "application/json",
                wifiSettingsJson(true, "Sin cambios. El badge sigue igual."));
    return;
  }

  if (!getPersistentAccessPointEnabled()) {
    server.send(200, "application/json",
                wifiSettingsJson(
                    true,
                    "Guardado. El punto de acceso sigue apagado hasta que lo enciendas."));
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
              ? "Guardado. Vuelve a entrar escribiendo a mano el SSID oculto y la nueva contraseña."
              : "Guardado. Vuelve a entrar al badge con la nueva contraseña."));
}

void handleAccessPointSet() {
  addNoCacheHeaders();
  if (!server.hasArg("enabled")) {
    server.send(400, "application/json",
                wifiSettingsJson(false, "Falta el ajuste del punto de acceso."));
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
                      ? "Guardado. Encendiendo el punto de acceso del badge."
                      : "Guardado. El punto de acceso se apagará; usa la red de casa o USB para volver a encenderlo."));
}

void handleStationWifiGet() {
  addNoCacheHeaders();
  server.send(200, "application/json", stationWifiJson(true, String()));
}

void handleStationWifiSet() {
  addNoCacheHeaders();
  if (!server.hasArg("ssid") || !server.hasArg("password")) {
    server.send(400, "application/json",
                stationWifiJson(false,
                                "Faltan el nombre y la contraseña de la red de casa."));
    return;
  }

  String error;
  if (!setPersistentStationWifiSettings(server.arg("ssid"),
                                        server.arg("password"), error)) {
    server.send(400, "application/json", stationWifiJson(false, error));
    return;
  }

  // Saving a home network deliberately selects the alternate mode. Reply
  // before ending the AP connection that submitted the form.
  if (!setPersistentAccessPointEnabled(false, error)) {
    server.send(500, "application/json", stationWifiJson(false, error));
    return;
  }
  requestedAccessPointEnabled = false;
  accessPointTogglePending = accessPointActive;
  accessPointToggleAt = millis() + 700;
  if (!accessPointActive) startStationConnection();
  server.send(202, "application/json",
              stationWifiJson(true,
                              "Guardado. Cambiando a tu red de casa."));
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
    refreshWifiNfcAfterRestart = false;
    return;
  }

  Serial.printf("[WIFI] SSID visibility: %s\r\n",
                getPersistentWifiHidden() ? "hidden" : "visible");
  Serial.println("[WIFI] Password: masked (USB Altar Network > v reveals briefly)");

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
  json += F(",\"capacity\":");
  json += boardCapacity();
  json += F(",\"imageCapacity\":");
  json += boardImageCapacity();
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

int base64UrlValue(char character) {
  if (character >= 'A' && character <= 'Z') return character - 'A';
  if (character >= 'a' && character <= 'z') return character - 'a' + 26;
  if (character >= '0' && character <= '9') return character - '0' + 52;
  if (character == '-' || character == '+') return 62;
  if (character == '_' || character == '/') return 63;
  return -1;
}

// base64url, so the payload survives form encoding without the expansion that
// '+', '/' and '=' would cause. Returns 0 on any invalid or oversized input.
size_t decodeBase64Url(const String &encoded, uint8_t *out, size_t capacity) {
  uint32_t accumulator = 0;
  uint8_t bits = 0;
  size_t written = 0;

  for (size_t i = 0; i < encoded.length(); ++i) {
    const char character = encoded[i];
    if (character == '=') break;

    const int value = base64UrlValue(character);
    if (value < 0) return 0;

    accumulator = (accumulator << 6) | static_cast<uint32_t>(value);
    bits += 6;
    if (bits < 8) continue;

    bits -= 8;
    if (written >= capacity) return 0;
    out[written++] = static_cast<uint8_t>((accumulator >> bits) & 0xFF);
  }

  return written;
}

void handleBoardImage() {
  const uint32_t postId =
      server.hasArg("id") ? strtoul(server.arg("id").c_str(), nullptr, 10) : 0;

  const size_t length =
      readBoardImage(postId, boardImageBuffer, sizeof(boardImageBuffer));
  if (length == 0) {
    server.send(404, "text/plain; charset=utf-8", "No hay dibujo para esa ofrenda.");
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
// change the on-disk layout and wipe every stored offering. So a label lives
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
//   - on home Wi-Fi it has no station list, but any peer it has just exchanged
//     packets with is in the ARP cache, which etharp_get_entry walks.
// Empty when the address cannot be resolved, which is normal for a client
// reached through a router rather than sitting on the same link.
String clientMacSuffix(const IPAddress &address) {
  const uint32_t wanted = static_cast<uint32_t>(address);
  // Two octets, not one: a single octet is 256 values, and at a busy con two
  // devices colliding is likelier than not once a couple of dozen have posted.
  char out[5] = {};

  if (isBadgeAccessPointActive()) {
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
  tail += F(",\"capacity\":");
  tail += boardCapacity();
  tail += F(",\"imageCapacity\":");
  tail += boardImageCapacity();
  tail += '}';
  server.sendContent(tail);
  server.sendContent(F(""));
}

void handleBoardCreate() {
  addNoCacheHeaders();

  const String text = server.hasArg("text") ? server.arg("text") : String();
  // The badge has no real-time clock, so the posting time can only ever be
  // what the browser claimed it was.
  const uint32_t createdAt =
      server.hasArg("ts") ? strtoul(server.arg("ts").c_str(), nullptr, 10) : 0;

  size_t imageLength = 0;
  if (server.hasArg("image") && server.arg("image").length() > 0) {
    imageLength = decodeBase64Url(server.arg("image"), boardImageBuffer,
                                  sizeof(boardImageBuffer));
    if (imageLength == 0) {
      server.send(400, "application/json",
                  boardStateJson(false,
                                 "No se pudo leer el dibujo o pesa más de lo "
                                 "que aguanta el badge."));
      return;
    }
  }

  String error;
  const long claimedAuthor = server.hasArg("authorId") ? server.arg("authorId").toInt() : 0;
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
                    error, authorId, server.arg("textInImage") == "1")) {
    server.send(400, "application/json", boardStateJson(false, error));
    return;
  }

  server.send(201, "application/json", boardStateJson(true, "Ofrenda enviada."));
}

void handleNfcCaptureSet() {
  addNoCacheHeaders();

  if (!server.hasArg("enabled")) {
    server.send(400, "application/json",
                F("{\"ok\":false,\"message\":\"Falta el ajuste de Ofrenda NFC.\"}"));
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

void handleBoardClear() {
  addNoCacheHeaders();
  if (!clearBoard()) {
    server.send(500, "application/json",
                boardStateJson(false, "No se pudo limpiar el tablero."));
    return;
  }
  server.send(200, "application/json",
              boardStateJson(true, "Se borraron todas las ofrendas."));
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
  location += F("/board");
  server.sendHeader("Location", location, true);
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
              "No se encontró la página. Abre http://10.69.4.20/ para las ofrendas, "
              "http://10.69.4.20/led para luces, "
              "http://10.69.4.20/nfc para NFC o "
              "http://10.69.4.20/settings para ajustes.");
}

void setupWebServer() {
  // The portal opens on the board. "/" used to be a dashboard whose first
  // element was a row of links onward, so landing there was navigable; it is
  // now the Wi-Fi form alone, which reads exactly like a captive-portal gate
  // demanding setup before you may continue. Settings moved to /settings.
  server.on("/", HTTP_GET, handleBoardPage);
  server.on("/settings", HTTP_GET, handleDashboardPage);
  server.on("/settings.html", HTTP_GET, handleDashboardPage);
  server.on("/index.html", HTTP_GET, handleDashboardPage);
  server.on("/theme.css", HTTP_GET, handleThemeStylesheet);
  server.on("/locale.js", HTTP_GET, handleLocaleScript);
  server.on("/assets/logo-candle.png", HTTP_GET, handleLogoAsset);
  server.on("/assets/badge-figure.png", HTTP_GET, handleBadgeFigureAsset);
  server.on("/led", HTTP_GET, handleLedPage);
  server.on("/led.html", HTTP_GET, handleLedPage);
  server.on("/nfc", HTTP_GET, handleNfcPage);
  server.on("/nfc.html", HTTP_GET, handleNfcPage);
  server.on("/board", HTTP_GET, handleBoardPage);
  server.on("/board.html", HTTP_GET, handleBoardPage);

  server.on("/api/state", HTTP_GET, handleLedState);
  server.on("/api/set", HTTP_GET, handleLedSet);
  server.on("/api/led/pixels", HTTP_GET, handleLedPixels);
  server.on("/api/led/identify", HTTP_GET, handleLedIdentify);

  server.on("/api/wifi/settings", HTTP_GET, handleWifiSettingsGet);
  server.on("/api/wifi/settings", HTTP_POST, handleWifiSettingsSet);
  server.on("/api/wifi/ap", HTTP_POST, handleAccessPointSet);
  server.on("/api/wifi/client", HTTP_GET, handleStationWifiGet);
  server.on("/api/wifi/client", HTTP_POST, handleStationWifiSet);
  server.on("/api/ui/language", HTTP_GET, handleLanguageGet);
  server.on("/api/ui/language", HTTP_POST, handleLanguageSet);

  server.on("/api/board/state", HTTP_GET, handleBoardState);
  server.on("/api/board/posts", HTTP_GET, handleBoardPosts);
  server.on("/api/board/image", HTTP_GET, handleBoardImage);
  server.on("/api/board/post", HTTP_POST, handleBoardCreate);
  server.on("/api/board/clear", HTTP_POST, handleBoardClear);

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

  // WebServer discards every header it was not told to keep, and User-Agent is
  // the only identity a browser offers for naming an offering.
  static const char *collected[] = {"User-Agent"};
  server.collectHeaders(collected, 1);

  server.begin();
  Serial.println("[WEB] HTTP server started");
}

}  // namespace

// Drains what the NFC reader captured while capture mode was on. This runs on
// the Arduino loop task, the only task that writes the board: the reader task
// stages payloads in a queue instead of touching LittleFS from a second core.
void serviceNfcCapture() {
  String captured;
  // One per pass so a burst of tags cannot stall the web server.
  if (!takeNfcCapture(captured)) return;

  String error;
  if (!addBoardPost(captured, 0, nullptr, 0, error, NFC_CAPTURE_AUTHOR_ID)) {
    Serial.printf("[BOARD][NFC] Capture rejected: %s\r\n", error.c_str());
    // Whoever tapped is most likely not on the access point, so the failure has
    // to be visible on the badge or it is invisible entirely.
    signalTagCue(false);
    return;
  }

  noteNfcCapturePosted();
  signalTagCue(true);
  Serial.printf("[BOARD][NFC] Captured tag posted (%u bytes)\r\n",
                static_cast<unsigned>(captured.length()));
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
  state.hidden = getPersistentWifiHidden();
  state.accessPointSsid = apSsid;
  state.homeSsid = getPersistentStationWifiSsid();
  state.localIp = state.homeConnected ? WiFi.localIP().toString() : String();
  state.hostname = WiFi.getHostname() ? WiFi.getHostname() : STATION_HOSTNAME;
  return state;
}

bool setBadgeAccessPointSettings(const String &password, bool hidden,
                                 String &error) {
  if (!setPersistentWifiSettings(password, hidden, error)) return false;
  if (!getPersistentAccessPointEnabled()) return true;
  refreshWifiNfcAfterRestart = isNfcWifiOnboardingActive();
  accessPointRestartPending = true;
  accessPointRestartAt = millis() + 50;
  return true;
}

bool setBadgeHomeWifiSettings(const String &ssid, const String &password,
                              String &error) {
  if (!setPersistentStationWifiSettings(ssid, password, error)) return false;
  if (!setPersistentAccessPointEnabled(false, error)) return false;
  requestedAccessPointEnabled = false;
  accessPointTogglePending = accessPointActive;
  accessPointToggleAt = millis() + 50;
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
  }

  createMacDerivedSsid();

  // Resolve and verify the persistent password before starting Wi-Fi.
  if (!initializeBadgeSettings()) {
    Serial.println(
        "[WIFI] ERROR: Persistent credentials unavailable; access point "
        "was not started");
    return;
  }

  if (getPersistentAccessPointEnabled()) {
    Serial.println("[WIFI] Starting access point");
    if (!configureVerifiedAccessPoint()) return;
  } else {
    // The owner deliberately left the AP off. The web server still starts so
    // a saved home-network station can serve the exact same portal.
    WiFi.mode(WIFI_OFF);
    if (!WiFi.setHostname(STATION_HOSTNAME)) {
      Serial.println("[WIFI] WARNING: Could not set DHCP hostname");
    }
    WiFi.mode(WIFI_STA);
    accessPointActive = false;
    Serial.println("[WIFI] Access point is disabled by saved setting");
  }

  setupWebServer();
  if (accessPointActive) startCaptivePortalDns();
  startStationConnection();

  Serial.printf("[WIFI] Badge AP: %s\r\n", accessPointActive ? "on" : "off");
  if (accessPointActive) {
    Serial.printf("[WIFI] SSID: %s\r\n", apSsid);
    Serial.printf("[WIFI] SSID visibility: %s\r\n",
                  getPersistentWifiHidden() ? "hidden" : "visible");
    Serial.println("[WIFI] Password: masked (USB Altar Network > v reveals briefly)");
    Serial.print("[WIFI] Dashboard: http://");
    Serial.println(WiFi.softAPIP());
  }
  Serial.print("[WIFI] Message board: http://");
  Serial.print(WiFi.softAPIP());
  Serial.println("/board");
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
