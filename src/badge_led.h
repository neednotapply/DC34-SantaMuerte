#pragma once

#include <Arduino.h>

struct LedTuiState {
  const char *pattern;
  uint8_t red;
  uint8_t green;
  uint8_t blue;
  uint8_t brightness;
  uint8_t speed;
  uint8_t identifyFrame;
};

LedTuiState getLedTuiState();
bool setLedTuiState(const String &pattern, int red, int green, int blue,
                    int brightness, int speed);
void setLedTuiIdentifyFrame(uint8_t frame);
