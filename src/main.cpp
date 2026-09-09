#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <LittleFS.h>
#include <math.h>
#include "badge_wifi.h"
#include "badge_led.h"
#include "badge_settings.h"
#include "board.h"
#include "nfc.h"
#include "usb_tui.h"

// -----------------------------------------------------------------------------
// PCB hardware
// -----------------------------------------------------------------------------
#define LED_PIN 17
#define LED_COUNT 11

Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

// -----------------------------------------------------------------------------
// LED state
// -----------------------------------------------------------------------------
// The stored settings record holds the pattern as this enum's index, so entries
// may only ever be appended. Reordering silently changes what a saved badge
// comes back as; inserting in the middle does the same.
enum LedPattern : uint8_t {
  PATTERN_SOLID,
  PATTERN_RAINBOW,
  PATTERN_CHASE,
  PATTERN_PULSE,
  PATTERN_TWINKLE,
  PATTERN_THEATER,
  PATTERN_AURORA,
  PATTERN_OFF,
  // Appended once the strand order was verified on hardware, which is what
  // makes an animation able to tell the ring from the hands.
  PATTERN_OFRENDA,
  PATTERN_CORONA,
  PATTERN_AUREOLA,
  PATTERN_ENCUENTRO,
  PATTERN_MANOS,
  PATTERN_ESCANER,
  PATTERN_PLASMA,
  PATTERN_DERIVA,
  PATTERN_LIMIT
};

constexpr uint8_t LED_PATTERN_COUNT = PATTERN_LIMIT;

// -----------------------------------------------------------------------------
// Physical layout, verified on hardware (see docs/led-map.md)
//
// Eight pixels form the halo, a ring read clockwise from the figure's left
// shoulder. Three sit over the praying hands as a triangle whose apex points up
// at that ring, which is the relationship most of the patterns below trade on.
// -----------------------------------------------------------------------------
constexpr uint8_t RING_COUNT = 8;
constexpr uint8_t RING[RING_COUNT] = {0, 1, 2, 3, 4, 5, 6, 7};

constexpr uint8_t HAND_TOP = 8;          // apex, just under the chin
constexpr uint8_t HAND_LOWER_RIGHT = 9;  // viewer's right
constexpr uint8_t HAND_LOWER_LEFT = 10;  // viewer's left
constexpr uint8_t HAND_COUNT = 3;
constexpr uint8_t HANDS[HAND_COUNT] = {HAND_LOWER_LEFT, HAND_LOWER_RIGHT, HAND_TOP};

// How far round the ring each pixel sits from the hands: the two shoulders are
// nearest, the crown pair furthest. Drives anything that opens or closes.
constexpr uint8_t RING_RANK[RING_COUNT] = {0, 1, 2, 3, 3, 2, 1, 0};
constexpr uint8_t RING_RANK_MAX = 3;

// Ring pixels ordered left to right across the badge, for a sweep that reads as
// horizontal rather than as travel around the circle.
constexpr uint8_t RING_BY_X[RING_COUNT] = {1, 0, 2, 3, 4, 5, 7, 6};

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
    case PATTERN_OFRENDA: return "ofrenda";
    case PATTERN_CORONA: return "corona";
    case PATTERN_AUREOLA: return "aureola";
    case PATTERN_ENCUENTRO: return "encuentro";
    case PATTERN_MANOS: return "manos";
    case PATTERN_ESCANER: return "escaner";
    case PATTERN_PLASMA: return "plasma";
    case PATTERN_DERIVA: return "deriva";
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
  if (name == "ofrenda") return PATTERN_OFRENDA;
  if (name == "corona") return PATTERN_CORONA;
  if (name == "aureola") return PATTERN_AUREOLA;
  if (name == "encuentro") return PATTERN_ENCUENTRO;
  if (name == "manos") return PATTERN_MANOS;
  if (name == "escaner") return PATTERN_ESCANER;
  if (name == "plasma") return PATTERN_PLASMA;
  if (name == "deriva") return PATTERN_DERIVA;
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

// -----------------------------------------------------------------------------
// Layout-aware patterns
//
// These are the ones worth having on this badge specifically: a strip of eleven
// in a line would render them meaningless. Each trades on the ring, the hand
// triangle, or the relationship between the two.
// -----------------------------------------------------------------------------

// Light gathers in the hands, climbs the triangle to its apex, then floods the
// halo outward from the crown to both shoulders and fades. The badge's own
// gesture: an offering raised, received, and let go.
void renderOfrenda(uint32_t now) {
  fillPixels(0);
  const uint32_t period = animationInterval(9000, 1600);
  const float t = static_cast<float>(now % period) / period;  // 0..1

  if (t < 0.28f) {
    // Gathering: the two lower hands come up together.
    const float level = t / 0.28f;
    strip.setPixelColor(HAND_LOWER_LEFT, selectedColor(level));
    strip.setPixelColor(HAND_LOWER_RIGHT, selectedColor(level));
    return;
  }

  if (t < 0.42f) {
    // Rising: the apex takes over as the pair dims.
    const float k = (t - 0.28f) / 0.14f;
    strip.setPixelColor(HAND_LOWER_LEFT, selectedColor(1.0f - k * 0.65f));
    strip.setPixelColor(HAND_LOWER_RIGHT, selectedColor(1.0f - k * 0.65f));
    strip.setPixelColor(HAND_TOP, selectedColor(k));
    return;
  }

  // Received: the halo opens from the crown down to the shoulders, then the
  // whole thing releases.
  const float k = (t - 0.42f) / 0.58f;
  const float reach = k * 2.6f;          // in RING_RANK steps, crown outward
  const float release = k > 0.62f ? (1.0f - (k - 0.62f) / 0.38f) : 1.0f;

  for (uint8_t i = 0; i < HAND_COUNT; ++i) {
    strip.setPixelColor(HANDS[i], selectedColor(0.35f * release));
  }
  for (uint8_t i = 0; i < RING_COUNT; ++i) {
    const float distance = RING_RANK_MAX - RING_RANK[i];   // 0 at the crown
    const float level = constrain(reach - distance, 0.0f, 1.0f) * release;
    if (level > 0.0f) strip.setPixelColor(RING[i], selectedColor(level));
  }
}

// A head travelling the halo with a fading tail, the hands holding an ember so
// the figure never goes fully dark.
void renderCorona(uint32_t now) {
  fillPixels(0);
  const uint16_t head = (now / animationInterval(230, 34)) % RING_COUNT;
  const float tail[] = {1.0f, 0.5f, 0.24f, 0.10f};
  for (uint8_t t = 0; t < 4; ++t) {
    const uint8_t i = (head - t + RING_COUNT) % RING_COUNT;
    strip.setPixelColor(RING[i], selectedColor(tail[t]));
  }
  for (uint8_t i = 0; i < HAND_COUNT; ++i) {
    strip.setPixelColor(HANDS[i], selectedColor(0.14f));
  }
}

// Opens from the hands out to the crown and closes again — the aperture the
// two groups make when you treat the hands as the centre.
void renderAureola(uint32_t now) {
  fillPixels(0);
  const uint32_t period = animationInterval(6000, 900);
  const float phase = static_cast<float>(now % period) / period * TWO_PI;
  const float open = (sinf(phase - HALF_PI) + 1.0f) * 0.5f;   // 0..1..0
  const float reach = open * (RING_RANK_MAX + 1.4f);

  const float handLevel = constrain(reach * 1.6f, 0.0f, 1.0f);
  for (uint8_t i = 0; i < HAND_COUNT; ++i) {
    strip.setPixelColor(HANDS[i], selectedColor(handLevel));
  }
  for (uint8_t i = 0; i < RING_COUNT; ++i) {
    const float level = constrain(reach - RING_RANK[i], 0.0f, 1.0f);
    if (level > 0.0f) strip.setPixelColor(RING[i], selectedColor(level));
  }
}

// Two heads leave the shoulders, climb opposite sides of the halo and meet at
// the crown, which flares. Then the hands answer and it starts again.
void renderEncuentro(uint32_t now) {
  fillPixels(0);
  const uint32_t period = animationInterval(4200, 700);
  const float t = static_cast<float>(now % period) / period;

  if (t < 0.72f) {
    const float travel = (t / 0.72f) * 3.0f;   // 0..3 rank steps
    for (uint8_t i = 0; i < RING_COUNT; ++i) {
      const float level = 1.0f - fabsf(RING_RANK[i] - travel);
      if (level > 0.0f) strip.setPixelColor(RING[i], selectedColor(level));
    }
    return;
  }

  // Meeting: the crown pair flares and the hands take the echo.
  const float k = (t - 0.72f) / 0.28f;
  const float flare = 1.0f - k;
  strip.setPixelColor(RING[3], selectedColor(flare));
  strip.setPixelColor(RING[4], selectedColor(flare));
  for (uint8_t i = 0; i < HAND_COUNT; ++i) {
    strip.setPixelColor(HANDS[i], selectedColor(flare * 0.8f));
  }
}

// The hand triangle pulses corner to corner while the halo holds a low wash —
// the three-pixel group given something to do on its own.
void renderManos(uint32_t now) {
  const uint32_t step = animationInterval(700, 110);
  const float t = static_cast<float>(now % (step * HAND_COUNT)) / step;

  for (uint8_t i = 0; i < RING_COUNT; ++i) {
    strip.setPixelColor(RING[i], selectedColor(0.10f));
  }
  for (uint8_t i = 0; i < HAND_COUNT; ++i) {
    float distance = fabsf(t - i);
    if (distance > HAND_COUNT / 2.0f) distance = HAND_COUNT - distance;
    const float level = constrain(1.0f - distance, 0.0f, 1.0f);
    strip.setPixelColor(HANDS[i], selectedColor(0.12f + 0.88f * level * level));
  }
}

// A beam sweeping left to right and back. Ring pixels are taken in screen order
// rather than chain order, so it reads as a horizontal scan of the halo.
void renderEscaner(uint32_t now) {
  fillPixels(0);
  const uint32_t period = animationInterval(2600, 420);
  const float phase = static_cast<float>(now % period) / period * TWO_PI;
  const float head = (sinf(phase) + 1.0f) * 0.5f * (RING_COUNT - 1);

  for (uint8_t i = 0; i < RING_COUNT; ++i) {
    const float level = constrain(1.0f - fabsf(i - head) * 0.75f, 0.0f, 1.0f);
    if (level > 0.0f) strip.setPixelColor(RING_BY_X[i], selectedColor(level));
  }
  // The hands answer as the beam crosses the middle of the badge.
  const float centre = constrain(1.0f - fabsf(head - (RING_COUNT - 1) / 2.0f), 0.0f, 1.0f);
  for (uint8_t i = 0; i < HAND_COUNT; ++i) {
    strip.setPixelColor(HANDS[i], selectedColor(centre * 0.7f));
  }
}

// Multicolour field. Hue runs round the ring and across the hands from two
// slow sines, so neighbours drift together instead of stepping.
void renderPlasma(uint32_t now) {
  const float t = now * (0.00035f + animationSpeed * 0.00002f);
  for (uint8_t i = 0; i < RING_COUNT; ++i) {
    const float a = static_cast<float>(i) / RING_COUNT * TWO_PI;
    const float v = sinf(a + t * 2.1f) + sinf(a * 2.0f - t * 1.3f);
    const uint16_t hue = static_cast<uint16_t>((v + 2.0f) / 4.0f * 65535.0f);
    strip.setPixelColor(RING[i], strip.gamma32(strip.ColorHSV(hue, 235, 255)));
  }
  for (uint8_t i = 0; i < HAND_COUNT; ++i) {
    const float v = sinf(t * 1.7f + i * 1.1f);
    const uint16_t hue = static_cast<uint16_t>((v + 1.0f) / 2.0f * 65535.0f);
    strip.setPixelColor(HANDS[i], strip.gamma32(strip.ColorHSV(hue, 235, 210)));
  }
}

// The two groups drift in opposite directions: the halo turns one way, the
// hands the other, so the badge never quite repeats.
void renderDeriva(uint32_t now) {
  const uint16_t ringHue = static_cast<uint16_t>(now * (1UL + animationSpeed / 3UL));
  for (uint8_t i = 0; i < RING_COUNT; ++i) {
    const uint16_t hue = ringHue + static_cast<uint32_t>(i) * 65536UL / RING_COUNT;
    strip.setPixelColor(RING[i], strip.gamma32(strip.ColorHSV(hue, 250, 255)));
  }
  for (uint8_t i = 0; i < HAND_COUNT; ++i) {
    const uint16_t hue = 32768 - ringHue + static_cast<uint32_t>(i) * 65536UL / HAND_COUNT;
    strip.setPixelColor(HANDS[i], strip.gamma32(strip.ColorHSV(hue, 250, 220)));
  }
}

// -----------------------------------------------------------------------------
// Tag acknowledgement
//
// Offering mode exists to be used by someone who is not looking at the portal
// -- often not even joined to the access point -- so the badge itself has to be
// the receipt. Whatever the LEDs were doing, a read interrupts them for under a
// second and then hands them straight back.
// -----------------------------------------------------------------------------
enum class TagCue : uint8_t { NONE, ACCEPTED, REJECTED };

constexpr uint32_t CUE_ACCEPTED_MS = 900;
constexpr uint32_t CUE_REJECTED_MS = 700;
// Floor the brightness while a cue runs: the badge may be dimmed right down or
// on the Off pattern, and those are exactly the times the cue is the only
// signal there is.
constexpr uint8_t CUE_MIN_BRIGHTNESS = 140;

TagCue activeCue = TagCue::NONE;
uint32_t cueStartedAt = 0;

uint32_t cueDurationMs() {
  return activeCue == TagCue::REJECTED ? CUE_REJECTED_MS : CUE_ACCEPTED_MS;
}

// The offering gesture, quickly: the hands take it, the halo receives it, then
// everything lets go.
uint32_t cueWhite(float scale) {
  scale = constrain(scale, 0.0f, 1.0f);
  const uint8_t v = static_cast<uint8_t>(roundf(255.0f * scale));
  return strip.gamma32(strip.Color(v, v, v));
}

void renderCueAccepted(float t) {
  strip.clear();
  const uint32_t white = cueWhite(1.0f);

  if (t < 0.15f) {
    for (uint8_t i = 0; i < HAND_COUNT; ++i) strip.setPixelColor(HANDS[i], white);
    return;
  }

  const float fade = t > 0.6f ? 1.0f - (t - 0.6f) / 0.4f : 1.0f;
  const float reach = (t - 0.15f) / 0.45f * (RING_RANK_MAX + 1.0f);

  for (uint8_t i = 0; i < HAND_COUNT; ++i) {
    strip.setPixelColor(HANDS[i], cueWhite(fade));
  }
  for (uint8_t i = 0; i < RING_COUNT; ++i) {
    const float level = constrain(reach - RING_RANK[i], 0.0f, 1.0f) * fade;
    if (level > 0.0f) strip.setPixelColor(RING[i], cueWhite(level));
  }
}

// Two hard red blinks: unmistakably not the accept.
void renderCueRejected(float t) {
  strip.clear();
  const bool on = (t < 0.22f) || (t > 0.42f && t < 0.64f);
  if (!on) return;
  for (uint16_t i = 0; i < LED_COUNT; ++i) {
    strip.setPixelColor(i, strip.Color(255, 0, 0));
  }
}

bool renderTagCue(uint32_t now) {
  if (activeCue == TagCue::NONE) return false;

  const uint32_t elapsed = now - cueStartedAt;
  const uint32_t duration = cueDurationMs();
  if (elapsed >= duration) {
    activeCue = TagCue::NONE;
    return false;
  }

  const float t = static_cast<float>(elapsed) / duration;
  if (activeCue == TagCue::REJECTED) renderCueRejected(t);
  else renderCueAccepted(t);
  return true;
}

// -----------------------------------------------------------------------------
// Identify mode: light known pixels in known colours so a photograph of the
// badge can be read back into a strand-index -> physical-position map.
//
// Three frames of four unmistakable colours beat eleven hues in one frame: a
// lit WS2812 blooms in a camera and neighbouring hues stop being tellable
// apart, while red / green / blue / white survive any exposure.
// -----------------------------------------------------------------------------
constexpr uint8_t IDENTIFY_FRAME_COUNT = 3;
constexpr uint8_t IDENTIFY_BRIGHTNESS = 40;  // Low, so the colours do not blow out.
uint8_t identifyFrame = 0;                   // 0 = off, 1..3 = a frame.

struct IdentifyColour { uint8_t r, g, b; const char *name; };
constexpr IdentifyColour IDENTIFY_COLOURS[4] = {
    {255, 0, 0, "RED"},
    {0, 255, 0, "GREEN"},
    {0, 0, 255, "BLUE"},
    {255, 255, 255, "WHITE"},
};

void renderIdentify() {
  strip.clear();
  const uint8_t first = (identifyFrame - 1) * 4;
  for (uint8_t slot = 0; slot < 4; ++slot) {
    const uint8_t index = first + slot;
    if (index >= LED_COUNT) break;
    const IdentifyColour &c = IDENTIFY_COLOURS[slot];
    strip.setPixelColor(index, strip.Color(c.r, c.g, c.b));
  }
}

void printIdentifyFrame() {
  if (identifyFrame == 0) {
    Serial.println("[IDENTIFY] Off; the badge is back to its own pattern.");
    return;
  }

  Serial.println();
  Serial.printf("===== IDENTIFY FRAME %u of %u =====\n", identifyFrame,
                IDENTIFY_FRAME_COUNT);
  const uint8_t first = (identifyFrame - 1) * 4;
  for (uint8_t slot = 0; slot < 4; ++slot) {
    const uint8_t index = first + slot;
    if (index >= LED_COUNT) break;
    Serial.printf("  strand index %2u -> %s\n", index,
                  IDENTIFY_COLOURS[slot].name);
  }
  Serial.println("  Every other pixel is off. Photograph the badge front-on,");
  Serial.println("  then press the next number.");
  Serial.println("==================================");
  Serial.println();
}

void setIdentifyFrame(uint8_t frame) {
  identifyFrame = frame > IDENTIFY_FRAME_COUNT ? 0 : frame;
  printIdentifyFrame();
}

uint8_t currentIdentifyFrame() { return identifyFrame; }

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

  uint8_t wantBrightness = identifyFrame ? IDENTIFY_BRIGHTNESS : ledBrightness;
  if (activeCue != TagCue::NONE && wantBrightness < CUE_MIN_BRIGHTNESS) {
    wantBrightness = CUE_MIN_BRIGHTNESS;
  }
  if (appliedBrightness != wantBrightness) {
    strip.setBrightness(wantBrightness);
    appliedBrightness = wantBrightness;
  }

  // Identify holds the strip until it is switched off, so a photograph is
  // never taken mid-animation.
  if (identifyFrame) {
    renderIdentify();
    strip.show();
    return;
  }

  // A tag was just read. This outranks the running pattern for under a second.
  if (renderTagCue(now)) {
    strip.show();
    return;
  }

  switch (currentPattern) {
    case PATTERN_SOLID: renderSolid(); break;
    case PATTERN_RAINBOW: renderRainbow(now); break;
    case PATTERN_CHASE: renderChase(now); break;
    case PATTERN_PULSE: renderPulse(now); break;
    case PATTERN_TWINKLE: renderTwinkle(frameScale); break;
    case PATTERN_THEATER: renderTheater(now); break;
    case PATTERN_AURORA: renderAurora(now, frameScale); break;
    case PATTERN_OFRENDA: renderOfrenda(now); break;
    case PATTERN_CORONA: renderCorona(now); break;
    case PATTERN_AUREOLA: renderAureola(now); break;
    case PATTERN_ENCUENTRO: renderEncuentro(now); break;
    case PATTERN_MANOS: renderManos(now); break;
    case PATTERN_ESCANER: renderEscaner(now); break;
    case PATTERN_PLASMA: renderPlasma(now); break;
    case PATTERN_DERIVA: renderDeriva(now); break;
    case PATTERN_OFF: strip.clear(); break;
    case PATTERN_LIMIT: break;
  }
  strip.show();
}

// -----------------------------------------------------------------------------
// LED state interface used by wifi.cpp
// All LED state validation and mutation stays in this file.
// -----------------------------------------------------------------------------
// The eleven pixels as they are lit right now, most-significant nibble first,
// so the LED page can show the running pattern on a picture of the badge
// instead of re-implementing eight animations in JavaScript and drifting.
// Called by the capture drain on the loop task, which is the same task the LED
// engine runs on, so a plain assignment is all the synchronisation needed.
void signalTagCue(bool accepted) {
  activeCue = accepted ? TagCue::ACCEPTED : TagCue::REJECTED;
  cueStartedAt = millis();
}

String getLedPixelsJson() {
  String hex;
  hex.reserve(LED_COUNT * 6 + 16);
  for (uint16_t i = 0; i < LED_COUNT; i++) {
    const uint32_t colour = strip.getPixelColor(i);
    char chunk[7];
    snprintf(chunk, sizeof(chunk), "%02X%02X%02X",
             static_cast<unsigned>((colour >> 16) & 0xFF),
             static_cast<unsigned>((colour >> 8) & 0xFF),
             static_cast<unsigned>(colour & 0xFF));
    hex += chunk;
  }

  String json;
  json.reserve(hex.length() + 40);
  json += F("{\"count\":");
  json += LED_COUNT;
  json += F(",\"pixels\":\"");
  json += hex;
  json += F("\"}");
  return json;
}

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

LedTuiState getLedTuiState() {
  return {patternToString(currentPattern), selectedR, selectedG, selectedB,
          ledBrightness, animationSpeed, currentIdentifyFrame()};
}

bool setLedTuiState(const String &pattern, int red, int green, int blue,
                    int brightness, int speed) {
  if (pattern.length() > 0 && stringToPattern(pattern) == PATTERN_AURORA &&
      pattern != "aurora") {
    return false;
  }
  applyLedWebSettings(pattern, red, green, blue, brightness, speed);
  return true;
}

void setLedTuiIdentifyFrame(uint8_t frame) { setIdentifyFrame(frame); }

// -----------------------------------------------------------------------------
// Arduino entry points
// -----------------------------------------------------------------------------
// The badge no longer forces its Wi-Fi record back onto NFC at every boot, so
// this is the guaranteed way to read the credentials: plug in USB, open a
// serial monitor at 115200, press Enter. Printed at boot as well, so anyone
// watching the log already has it.
void printBadgeCredentials() {
  const char *ssid = getBadgeWifiSsid();
  const char *password = getBadgeWifiPassword();

  Serial.println();
  Serial.println("==================================================");
  Serial.println("  SANTA MUERTE // WI-FI");
  Serial.printf("  SSID:     %s\n", ssid && ssid[0] ? ssid : "(not ready)");
  Serial.printf("  PASSWORD: %s\n",
                password && password[0] ? password : "(not ready)");
  Serial.printf("  BADGE AP: %s\n", isBadgeAccessPointActive() ? "ON" : "OFF");
  if (isBadgeAccessPointActive()) {
    Serial.println("  PORTAL:   http://10.69.4.20/");
    Serial.println("  Hidden SSID: type it by hand if your phone cannot see it.");
  } else {
    Serial.println("  PORTAL:   AP is off; use home Wi-Fi or press a to restore it.");
  }
  if (hasPersistentStationWifiSettings()) {
    Serial.printf("  HOME:     %s // http://SantaMuerte.local/\n",
                  getPersistentStationWifiSsid());
  }
  Serial.println("--------------------------------------------------");
  Serial.println("  Press Enter to show this again.");
  if (isBadgeAccessPointActive()) {
    Serial.println("  Press a to switch from Santa Muerte AP to home Wi-Fi.");
  } else {
    Serial.println("  Press a to switch from home Wi-Fi to Santa Muerte AP.");
  }
  Serial.println("  Press 1, 2 or 3 to run an LED identify frame; 0 to stop.");
  Serial.println("  Press h for free heap, c/x to preview the tag cue.");
  Serial.println("==================================================");
  Serial.println();
}

// Free heap, and the largest block still contiguous within it. The second
// number is the one that matters: a board write needs one large contiguous
// allocation for a base64 drawing, so a heap that is roomy but shredded will
// fail that write while still reporting plenty free.
void printHeap(const char *when) {
  Serial.printf("[HEAP] %s free=%u largest=%u min-ever=%u\n", when,
                static_cast<unsigned>(ESP.getFreeHeap()),
                static_cast<unsigned>(ESP.getMaxAllocHeap()),
                static_cast<unsigned>(ESP.getMinFreeHeap()));
}

// Enter reprints the Wi-Fi block; 1-3 hold an identify frame and 0 releases it.
// Asking an end user to remember a command is one step too many.
void serviceSerialConsole() {
  if (!Serial.available()) return;

  bool sawLineEnd = false;
  bool sawHeap = false;
  bool sawCue = false;
  bool sawCueFail = false;
  bool sawAccessPointToggle = false;
  int frameKey = -1;
  while (Serial.available()) {
    const int character = Serial.read();
    if (character == '\n' || character == '\r') sawLineEnd = true;
    else if (character == 'h' || character == 'H') sawHeap = true;
    else if (character == 'c' || character == 'C') sawCue = true;
    else if (character == 'x' || character == 'X') sawCueFail = true;
    else if (character == 'a' || character == 'A') sawAccessPointToggle = true;
    else if (character >= '0' && character <= '9') frameKey = character - '0';
  }

  if (sawHeap) {
    printHeap("on request");
    return;
  }

  // Preview the tag acknowledgement without needing a tag to hand.
  if (sawCue || sawCueFail) {
    signalTagCue(sawCue);
    Serial.printf("[CUE] %s preview\n", sawCue ? "accepted" : "rejected");
    return;
  }

  if (sawAccessPointToggle) {
    String error;
    const bool next = !isBadgeAccessPointActive();
    if (setBadgeAccessPointEnabled(next, error)) {
      Serial.printf("[WIFI] Badge AP turning %s\n", next ? "on" : "off");
    } else {
      Serial.printf("[WIFI] Badge AP change failed: %s\n", error.c_str());
    }
    return;
  }

  if (frameKey >= 0 && frameKey <= IDENTIFY_FRAME_COUNT) {
    setIdentifyFrame(static_cast<uint8_t>(frameKey));
    return;
  }

  if (sawLineEnd) printBadgeCredentials();
}

// Brings the NFC radio back to its stored mode, or to Wi-Fi sharing when
// nothing has been stored yet.
void restoreNfcSettings() {
  uint8_t accessPointMac[6] = {0};
  const bool haveAccessPointMac = getBadgeWifiApMac(accessPointMac);

  StoredNfcSettings stored = {};
  const bool restored = loadNfcSettings(stored);
  if (!restored) {
    stored.mode = NFC_MODE_WIFI;
    stored.offeringEnabled = false;
    Serial.println("[MAIN] No stored NFC state; sharing Wi-Fi over NFC");
  }

  // Offering wins the radio at boot too. A record written before the two were
  // made mutually exclusive can still hold both; starting emulation on top of
  // it would leave the toggle reading as on while the reader was busy
  // elsewhere, which is the one state the page cannot explain.
  if (stored.offeringEnabled) {
    if (setNfcCaptureEnabled(true)) {
      Serial.println("[MAIN] NFC Offering restored; reader scanning");
    }
    return;
  }

  switch (stored.mode) {
    case NFC_MODE_STOPPED:
      Serial.println("[MAIN] NFC emulation left stopped, as last set");
      return;

    case NFC_MODE_TEXT:
    case NFC_MODE_URL: {
      const char *type = stored.mode == NFC_MODE_URL ? "url" : "text";
      if (startNfcTagEmulation(type, String(stored.payload))) {
        Serial.printf("[MAIN] NFC %s emulation restored\n", type);
        return;
      }
      Serial.printf(
          "[MAIN] WARNING: stored NFC %s record could not be restored; "
          "falling back to Wi-Fi sharing\n",
          type);
      break;
    }

    default:
      break;
  }

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

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("===== START =====");
  usbTuiBegin();

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

  // A freshly flashed badge has no stored NFC state, and comes up sharing its
  // Wi-Fi so the first phone to touch it can get on. After that the radio is
  // treated like the LEDs: whatever it was left doing is what it comes back
  // doing. The Wi-Fi details stay reachable over USB either way, which is what
  // makes it safe not to force the record back up on every boot.
  if (!initializeBadgeSettings()) {
    Serial.println(
        "[MAIN] Persistent settings unavailable; NFC was not restored");
    return;
  }

  restoreNfcSettings();
  armNfcPersistence();

}

void loop() {
  updateWebServer();
  serviceNfcCapture();
  serviceNfcPersistence();
  usbTuiService();
  updateLEDs();
  serviceLedSettingsPersistence();
  delay(1);
}
