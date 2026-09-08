#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <LittleFS.h>
#include <esp_wifi.h>
#include <cstring>
#include "badge_wifi.h"
#include "badge_settings.h"
#include "board.h"
#include "nfc.h"

// Implemented in main.cpp. wifi.cpp only transports and parses LED requests;
// main.cpp remains responsible for LED state, validation, and animation logic.
String getLedStateJson();
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

const IPAddress AP_IP(10, 10, 10, 100);
const IPAddress AP_GATEWAY(10, 10, 10, 100);
const IPAddress AP_SUBNET(255, 255, 255, 0);
constexpr uint16_t HTTP_PORT = 80;
constexpr uint16_t DNS_PORT = 53;

WebServer server(HTTP_PORT);
DNSServer dnsServer;
bool dnsServerRunning = false;
bool fileSystemReady = false;
bool accessPointRestartPending = false;
bool refreshWifiNfcAfterRestart = false;
uint32_t accessPointRestartAt = 0;

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

  // WiFi.mode(WIFI_AP) may restore the Wi-Fi driver's previous AP settings.
  // Force a clean stop before applying the NVS credential resolved above.
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(75);

  for (uint8_t attempt = 1; attempt <= 2; ++attempt) {
    WiFi.mode(WIFI_AP);
    delay(50);

    if (!WiFi.softAPConfig(AP_IP, AP_GATEWAY, AP_SUBNET)) {
      Serial.println("[WIFI] ERROR: Could not configure access point IP");
    } else if (!WiFi.softAP(apSsid, password, 1, hidden ? 1 : 0, 4)) {
      Serial.println("[WIFI] ERROR: Could not start access point");
    } else {
      delay(75);
      if (activeApMatchesExpectedSettings(apSsid, password, hidden)) {
        Serial.println(
            "[WIFI] Active access-point credentials verified");
        return true;
      }

      Serial.println(
          "[WIFI] WARNING: Active AP credentials did not match stored "
          "settings; restarting Wi-Fi");
    }

    WiFi.softAPdisconnect(true);
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
        "browse to 10.10.10.100 by hand");
  }
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
    error += " is missing. Upload the LittleFS data image and try again.";
    server.send(500, "text/plain; charset=utf-8", error);
    return;
  }

  File page = LittleFS.open(path, "r");
  if (!page) {
    String error = "Could not open ";
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
  json += F(",\"restartPending\":");
  json += accessPointRestartPending ? F("true") : F("false");
  json += F(",\"message\":\"");
  json += jsonEscape(message);
  json += F("\"}");
  return json;
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
        wifiSettingsJson(false, "Password and hidden fields are required."));
    return;
  }

  const String password = server.arg("password");
  const String hiddenValue = server.arg("hidden");
  const bool hidden =
      hiddenValue == "1" || hiddenValue == "true" || hiddenValue == "on";

  String error;
  if (!setPersistentWifiSettings(password, hidden, error)) {
    server.send(400, "application/json", wifiSettingsJson(false, error));
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
              ? "Saved. Reconnect by manually entering the hidden SSID and new password."
              : "Saved. Reconnect to the badge using the updated password."));
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

  Serial.printf("[WIFI] SSID visibility: %s\n",
                getPersistentWifiHidden() ? "hidden" : "visible");
  Serial.printf("[WIFI] Password: %s\n", getPersistentWifiPassword());

  startCaptivePortalDns();

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

// -----------------------------------------------------------------------------
// Dashboard, LED webpage, and LED API
// -----------------------------------------------------------------------------
void handleDashboardPage() {
  serveLittleFsFile("/index.html", "text/html; charset=utf-8");
}

void handleLedPage() {
  serveLittleFsFile("/led.html", "text/html; charset=utf-8");
}

void handleLedState() {
  addNoCacheHeaders();
  server.send(200, "application/json", getLedStateJson());
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
    server.send(404, "text/plain; charset=utf-8", "No picture for that post.");
    return;
  }

  // A post id is never reused, so its picture never changes. Letting a phone
  // cache it is the difference between scrolling the board once and
  // re-fetching every thumbnail on every scroll.
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
void handleBoardPosts() {
  const uint32_t requestedBefore =
      server.hasArg("before")
          ? strtoul(server.arg("before").c_str(), nullptr, 10)
          : 0;
  const uint16_t limit =
      constrain(server.hasArg("limit") ? server.arg("limit").toInt() : 20,
                1, 40);
  String tag = server.hasArg("tag") ? server.arg("tag") : String();
  tag.trim();
  tag.toLowerCase();

  addNoCacheHeaders();
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json", "");
  server.sendContent(F("{\"posts\":["));

  uint32_t cursor = requestedBefore;
  uint16_t sent = 0;
  BoardPost post;

  while (sent < limit && readNextBoardPost(cursor, tag, post)) {
    String chunk;
    chunk.reserve(post.text.length() + post.link.length() +
                  post.tags.length() + 96);
    if (sent > 0) chunk += ',';
    chunk += F("{\"id\":");
    chunk += post.id;
    chunk += F(",\"createdAt\":");
    chunk += post.createdAt;
    chunk += F(",\"text\":\"");
    chunk += jsonEscape(post.text);
    chunk += F("\",\"link\":\"");
    chunk += jsonEscape(post.link);
    chunk += F("\",\"tags\":\"");
    chunk += jsonEscape(post.tags);
    chunk += F("\",\"hasImage\":");
    chunk += post.hasImage ? F("true") : F("false");
    chunk += '}';
    server.sendContent(chunk);
    ++sent;
  }

  String tail;
  tail.reserve(120);
  tail += F("],\"nextBefore\":");
  tail += sent > 0 ? cursor : 0;
  tail += F(",\"more\":");
  tail += sent == limit ? F("true") : F("false");
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
  const String link = server.hasArg("link") ? server.arg("link") : String();
  const String tags = server.hasArg("tags") ? server.arg("tags") : String();
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
                                 "That picture could not be decoded, or is "
                                 "larger than the badge accepts."));
      return;
    }
  }

  String error;
  if (!addBoardPost(text, link, tags, createdAt,
                    imageLength > 0 ? boardImageBuffer : nullptr, imageLength,
                    error)) {
    server.send(400, "application/json", boardStateJson(false, error));
    return;
  }

  server.send(201, "application/json", boardStateJson(true, "Posted."));
}

void handleBoardClear() {
  addNoCacheHeaders();
  if (!clearBoard()) {
    server.send(500, "application/json",
                boardStateJson(false, "The board could not be cleared."));
    return;
  }
  server.send(200, "application/json",
              boardStateJson(true, "Every post was cleared."));
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
    server.send(404, "application/json", "{\"ok\":false,\"error\":\"Not found\"}");
    return;
  }

  if (requestIsForAnotherHost()) {
    redirectToPortal();
    return;
  }

  server.send(404, "text/plain; charset=utf-8",
              "Page not found. Open http://10.10.10.100/ for the dashboard, "
              "http://10.10.10.100/board for the message board, "
              "http://10.10.10.100/led for LEDs, or "
              "http://10.10.10.100/nfc for NFC tools.");
}

void setupWebServer() {
  server.on("/", HTTP_GET, handleDashboardPage);
  server.on("/index.html", HTTP_GET, handleDashboardPage);
  server.on("/led", HTTP_GET, handleLedPage);
  server.on("/led.html", HTTP_GET, handleLedPage);
  server.on("/nfc", HTTP_GET, handleNfcPage);
  server.on("/nfc.html", HTTP_GET, handleNfcPage);
  server.on("/board", HTTP_GET, handleBoardPage);
  server.on("/board.html", HTTP_GET, handleBoardPage);

  server.on("/api/state", HTTP_GET, handleLedState);
  server.on("/api/set", HTTP_GET, handleLedSet);

  server.on("/api/wifi/settings", HTTP_GET, handleWifiSettingsGet);
  server.on("/api/wifi/settings", HTTP_POST, handleWifiSettingsSet);

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

  server.begin();
  Serial.println("[WEB] HTTP server started");
}

}  // namespace

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

void setupWiFiAccessPoint() {
  Serial.println("[WIFI] Mounting LittleFS");
  fileSystemReady = LittleFS.begin(true);
  if (!fileSystemReady) {
    Serial.println("[WIFI] ERROR: LittleFS mount failed");
  } else {
    Serial.printf("[WIFI] /index.html: %s\n",
                  LittleFS.exists("/index.html") ? "ready" : "missing");
    Serial.printf("[WIFI] /led.html: %s\n",
                  LittleFS.exists("/led.html") ? "ready" : "missing");
    Serial.printf("[WIFI] /nfc.html: %s\n",
                  LittleFS.exists("/nfc.html") ? "ready" : "missing");
    Serial.printf("[WIFI] /board.html: %s\n",
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

  Serial.println("[WIFI] Starting access point");
  if (!configureVerifiedAccessPoint()) {
    return;
  }

  setupWebServer();
  startCaptivePortalDns();

  Serial.printf("[WIFI] SSID: %s\n", apSsid);
  Serial.printf("[WIFI] SSID visibility: %s\n",
                getPersistentWifiHidden() ? "hidden" : "visible");
  Serial.printf("[WIFI] Password: %s\n", getPersistentWifiPassword());
  Serial.print("[WIFI] Dashboard: http://");
  Serial.println(WiFi.softAPIP());
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
}
