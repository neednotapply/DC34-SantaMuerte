#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <LittleFS.h>
#include <math.h>
#include "badge_wifi.h"
#include "badge_settings.h"
#include "board.h"
#include "nfc.h"

// -----------------------------------------------------------------------------
// PCB hardware
// -----------------------------------------------------------------------------
#define LED_PIN 17
#define LED_COUNT 11

Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

// -----------------------------------------------------------------------------
// LED state
// -----------------------------------------------------------------------------
enum LedPattern : uint8_t {
  PATTERN_SOLID,
  PATTERN_RAINBOW,
  PATTERN_CHASE,
  PATTERN_PULSE,
  PATTERN_TWINKLE,
  PATTERN_THEATER,
  PATTERN_AURORA,
  PATTERN_OFF
};

constexpr uint8_t LED_PATTERN_COUNT = PATTERN_OFF + 1;

LedPattern currentPattern = PATTERN_AURORA;
uint8_t selectedR = 166;
uint8_t selectedG = 36;
uint8_t selectedB = 255;
uint8_t ledBrightness = 160;  // 0-255
uint8_t animationSpeed = 55;  // 1-100

// LED settings are saved a few seconds after the last change. The web page
// sends one request per slider movement, so writing on every request would
// put thousands of NVS writes behind a single drag.
constexpr uint32_t LED_SETTINGS_SAVE_DELAY_MS = 3000;
bool ledSettingsDirty = false;
uint32_t ledSettingsDirtyAt = 0;

uint16_t auroraHue[LED_COUNT];
float auroraVelocity[LED_COUNT];
uint8_t twinkleLevel[LED_COUNT] = {0};

const uint16_t PURPLE_MIN = 50000;
const uint16_t PURPLE_MAX = 56000;

// -----------------------------------------------------------------------------
// Utility functions
// -----------------------------------------------------------------------------
const char *patternToString(LedPattern pattern) {
  switch (pattern) {
    case PATTERN_SOLID: return "solid";
    case PATTERN_RAINBOW: return "rainbow";
    case PATTERN_CHASE: return "chase";
    case PATTERN_PULSE: return "pulse";
    case PATTERN_TWINKLE: return "twinkle";
    case PATTERN_THEATER: return "theater";
    case PATTERN_AURORA: return "aurora";
    case PATTERN_OFF: return "off";
    default: return "aurora";
  }
}

LedPattern stringToPattern(const String &name) {
  if (name == "solid") return PATTERN_SOLID;
  if (name == "rainbow") return PATTERN_RAINBOW;
  if (name == "chase") return PATTERN_CHASE;
  if (name == "pulse") return PATTERN_PULSE;
  if (name == "twinkle") return PATTERN_TWINKLE;
  if (name == "theater") return PATTERN_THEATER;
  if (name == "off") return PATTERN_OFF;
  return PATTERN_AURORA;
}

uint32_t selectedColor(float scale = 1.0f) {
  scale = constrain(scale, 0.0f, 1.0f);
  uint8_t r = (uint8_t)roundf(selectedR * scale);
  uint8_t g = (uint8_t)roundf(selectedG * scale);
  uint8_t b = (uint8_t)roundf(selectedB * scale);
  return strip.gamma32(strip.Color(r, g, b));
}

void fillPixels(uint32_t color) {
  for (uint16_t i = 0; i < LED_COUNT; i++) strip.setPixelColor(i, color);
}

uint32_t animationInterval(uint32_t slowMs, uint32_t fastMs) {
  return slowMs - ((slowMs - fastMs) * (animationSpeed - 1UL) / 99UL);
}

StoredLedSettings currentLedSettings() {
  StoredLedSettings settings;
  settings.pattern = static_cast<uint8_t>(currentPattern);
  settings.red = selectedR;
  settings.green = selectedG;
  settings.blue = selectedB;
  settings.brightness = ledBrightness;
  settings.speed = animationSpeed;
  return settings;
}

// Range checking stays here with the rest of the LED logic; badge_settings.cpp
// only vouches for the record's integrity, never for what the values mean.
void restoreLedSettings() {
  StoredLedSettings settings = {};
  if (!loadLedSettings(settings)) {
    Serial.println("[LED] No saved settings; using defaults");
    return;
  }

  if (settings.pattern >= LED_PATTERN_COUNT || settings.speed < 1 ||
      settings.speed > 100) {
    Serial.println(
        "[LED] WARNING: Saved settings are out of range; using defaults");
    return;
  }

  currentPattern = static_cast<LedPattern>(settings.pattern);
  selectedR = settings.red;
  selectedG = settings.green;
  selectedB = settings.blue;
  ledBrightness = settings.brightness;
  animationSpeed = settings.speed;

  Serial.printf(
      "[LED] Restored pattern=%s rgb=%u,%u,%u brightness=%u speed=%u\n",
      patternToString(currentPattern), selectedR, selectedG, selectedB,
      ledBrightness, animationSpeed);
}

void serviceLedSettingsPersistence() {
  if (!ledSettingsDirty ||
      static_cast<int32_t>(millis() - ledSettingsDirtyAt) <
          static_cast<int32_t>(LED_SETTINGS_SAVE_DELAY_MS)) {
    return;
  }

  ledSettingsDirty = false;
  if (saveLedSettings(currentLedSettings())) {
    Serial.println("[LED] Settings saved");
  }
}

// -----------------------------------------------------------------------------
// LED setup and animation engine
// -----------------------------------------------------------------------------
void setupLEDs() {
  Serial.println("[LED] begin");
  strip.begin();
  strip.setBrightness(ledBrightness);
  strip.clear();
  strip.show();

  randomSeed(esp_random());
  for (int i = 0; i < LED_COUNT; i++) {
    auroraHue[i] = random(PURPLE_MIN, PURPLE_MAX);
    auroraVelocity[i] = random(8, 18) / 100.0f;
  }
  Serial.println("[LED] ready");
}

void renderSolid() {
  fillPixels(selectedColor());
}

void renderRainbow(uint32_t now) {
  uint16_t firstHue = (uint16_t)(now * (2UL + animationSpeed / 2UL));
  for (uint16_t i = 0; i < LED_COUNT; i++) {
    uint16_t pixelHue = firstHue + (uint32_t)i * 65536UL / LED_COUNT;
    strip.setPixelColor(i, strip.gamma32(strip.ColorHSV(pixelHue, 255, 255)));
  }
}

void renderChase(uint32_t now) {
  fillPixels(0);
  uint16_t head = (now / animationInterval(240, 35)) % LED_COUNT;
  const float tail[] = {1.0f, 0.46f, 0.20f, 0.08f};
  for (uint8_t t = 0; t < 4; t++) {
    int index = (head - t + LED_COUNT) % LED_COUNT;
    strip.setPixelColor(index, selectedColor(tail[t]));
  }
}

void renderPulse(uint32_t now) {
  uint32_t period = animationInterval(5000, 650);
  float phase = (float)(now % period) / period * TWO_PI;
  float level = 0.10f + 0.90f * (sinf(phase - HALF_PI) + 1.0f) * 0.5f;
  fillPixels(selectedColor(level));
}

void renderTwinkle(float frameScale) {
  const float baseFade = 5.0f + animationSpeed / 12.0f;
  const uint8_t fadeAmount =
      static_cast<uint8_t>(constrain(roundf(baseFade * frameScale), 1.0f, 255.0f));

  for (uint16_t i = 0; i < LED_COUNT; i++) {
    twinkleLevel[i] =
        (twinkleLevel[i] > fadeAmount) ? twinkleLevel[i] - fadeAmount : 0;
  }

  // Convert the original per-frame sparkle chance to an elapsed-time chance.
  // At the normal 20 ms frame interval this is visually equivalent.
  const float baseChance = (3.0f + animationSpeed / 5.0f) / 100.0f;
  const float elapsedChance = 1.0f - powf(1.0f - baseChance, frameScale);
  if (random(10000) < static_cast<long>(elapsedChance * 10000.0f)) {
    twinkleLevel[random(LED_COUNT)] = random(175, 256);
  }

  for (uint16_t i = 0; i < LED_COUNT; i++) {
    strip.setPixelColor(i, selectedColor(twinkleLevel[i] / 255.0f));
  }
}

void renderTheater(uint32_t now) {
  uint8_t offset = (now / animationInterval(320, 45)) % 3;
  for (uint16_t i = 0; i < LED_COUNT; i++) {
    strip.setPixelColor(i, ((i + offset) % 3 == 0) ? selectedColor() : 0);
  }
}

void renderAurora(uint32_t now, float frameScale) {
  float speedMultiplier = 0.35f + animationSpeed / 45.0f;
  for (int i = 0; i < LED_COUNT; i++) {
    auroraHue[i] +=
        auroraVelocity[i] * 110.0f * speedMultiplier * frameScale;
    if (auroraHue[i] > PURPLE_MAX || auroraHue[i] < PURPLE_MIN) {
      auroraVelocity[i] = -auroraVelocity[i];
      auroraHue[i] +=
          auroraVelocity[i] * 110.0f * speedMultiplier * frameScale;
    }
    uint8_t value = 175 + 70 * sinf((now * (0.0007f + animationSpeed * 0.000018f)) + i * 0.8f);
    strip.setPixelColor(i, strip.gamma32(strip.ColorHSV(auroraHue[i], 255, value)));
  }
}

void updateLEDs() {
  static uint32_t lastFrame = 0;
  static uint8_t appliedBrightness = 255;
  const uint32_t now = millis();

  if (lastFrame == 0) lastFrame = now - 20;
  const uint32_t elapsedMs = now - lastFrame;
  if (elapsedMs < 20) return;  // approximately 50 frames per second
  lastFrame = now;

  // Frame-dependent effects keep their intended speed through occasional
  // scheduling delays. Clamp large gaps so an effect never jumps excessively.
  const float frameScale =
      constrain(static_cast<float>(elapsedMs) / 20.0f, 0.25f, 5.0f);

  if (appliedBrightness != ledBrightness) {
    strip.setBrightness(ledBrightness);
    appliedBrightness = ledBrightness;
  }

  switch (currentPattern) {
    case PATTERN_SOLID: renderSolid(); break;
    case PATTERN_RAINBOW: renderRainbow(now); break;
    case PATTERN_CHASE: renderChase(now); break;
    case PATTERN_PULSE: renderPulse(now); break;
    case PATTERN_TWINKLE: renderTwinkle(frameScale); break;
    case PATTERN_THEATER: renderTheater(now); break;
    case PATTERN_AURORA: renderAurora(now, frameScale); break;
    case PATTERN_OFF: strip.clear(); break;
  }
  strip.show();
}

// -----------------------------------------------------------------------------
// LED state interface used by wifi.cpp
// All LED state validation and mutation stays in this file.
// -----------------------------------------------------------------------------
String getLedStateJson() {
  char json[160];
  snprintf(json, sizeof(json),
           "{\"pattern\":\"%s\",\"r\":%u,\"g\":%u,\"b\":%u,\"brightness\":%u,\"speed\":%u}",
           patternToString(currentPattern), selectedR, selectedG, selectedB,
           ledBrightness, animationSpeed);
  return String(json);
}

void applyLedWebSettings(const String &pattern,
                         int red,
                         int green,
                         int blue,
                         int brightness,
                         int speed) {
  if (pattern.length() > 0) currentPattern = stringToPattern(pattern);
  if (red >= 0) selectedR = (uint8_t)constrain(red, 0, 255);
  if (green >= 0) selectedG = (uint8_t)constrain(green, 0, 255);
  if (blue >= 0) selectedB = (uint8_t)constrain(blue, 0, 255);
  if (brightness >= 0) ledBrightness = (uint8_t)constrain(brightness, 0, 255);
  if (speed >= 0) animationSpeed = (uint8_t)constrain(speed, 1, 100);

  Serial.printf("[WEB] pattern=%s rgb=%u,%u,%u brightness=%u speed=%u\n",
                patternToString(currentPattern), selectedR, selectedG, selectedB,
                ledBrightness, animationSpeed);

  ledSettingsDirty = true;
  ledSettingsDirtyAt = millis();
}

// -----------------------------------------------------------------------------
// Arduino entry points
// -----------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("===== START =====");

  // Restore before the strip starts so the first frame is already the state
  // the badge was left in, rather than a flash of the defaults.
  restoreLedSettings();

  setupLEDs();
  Serial.println("[MAIN] LEDs initialized");

  // Start Wi-Fi before NFC so the controller remains available even if the
  // PN532 is missing or fails its startup check.
  setupWiFiAccessPoint();
  Serial.println("[MAIN] Wi-Fi controller initialized");

  // After LittleFS is mounted by the Wi-Fi setup above. The board's two rings
  // claim most of the filesystem, so the space left over is only meaningful
  // once they have been allocated.
  if (setupBoard()) {
    Serial.printf("[MAIN] Message board ready: %u of %u posts stored\n",
                  boardStoredCount(), boardCapacity());
    Serial.printf("[MAIN] LittleFS: %u of %u bytes used, %u free\n",
                  static_cast<unsigned>(LittleFS.usedBytes()),
                  static_cast<unsigned>(LittleFS.totalBytes()),
                  static_cast<unsigned>(LittleFS.totalBytes() -
                                        LittleFS.usedBytes()));
  } else {
    Serial.println(
        "[MAIN] WARNING: Message board storage is unavailable; posting is "
        "disabled");
  }

  setupNFC();
  Serial.println("[MAIN] NFC initialized");

  // The Wi-Fi record goes back up on every boot, whatever was being emulated
  // when the badge was last powered down. A Text or URL record is a runtime
  // choice; letting one persist would mean a badge whose password nobody can
  // read is a badge nobody can reach.
  if (!initializeBadgeSettings()) {
    Serial.println(
        "[MAIN] Persistent settings unavailable; NFC Wi-Fi onboarding was not "
        "started");
    return;
  }

  uint8_t accessPointMac[6] = {0};
  const bool haveAccessPointMac = getBadgeWifiApMac(accessPointMac);
  if (!haveAccessPointMac) {
    Serial.println(
        "[MAIN] WARNING: SoftAP MAC unavailable; Wi-Fi NFC record will use zeros");
  }

  if (startNfcWifiOnboarding(getBadgeWifiSsid(),
                             getBadgeWifiPassword(),
                             haveAccessPointMac ? accessPointMac : nullptr)) {
    Serial.println("[MAIN] NFC Wi-Fi onboarding tag emulation started");
  } else {
    Serial.println(
        "[MAIN] WARNING: NFC Wi-Fi onboarding could not be started");
  }
}

void loop() {
  updateWebServer();
  updateLEDs();
  serviceLedSettingsPersistence();
  delay(1);
}
