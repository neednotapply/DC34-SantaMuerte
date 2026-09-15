#pragma once

#include <Arduino.h>
#include <stddef.h>

// -----------------------------------------------------------------------------
// Whole-frame WS2812 output for the badge's strand
//
// Adafruit_NeoPixel's ESP-IDF 5 backend clocks a frame through a single 48
// symbol RMT window -- two pixels' worth -- that an interrupt refills every 24
// symbols. That is about 30us of slack per refill, and the driver's interrupt
// is not IRAM-resident, so it cannot run at all while the flash cache is down.
// Eleven pixels need ten of those refills per frame. Miss one and the strand
// latches whatever the line settled to from that point on: the pixels already
// clocked out are right, every pixel after the stall is wrong. The chain runs
// left to right across the badge, which is why the damage always appeared on
// the right-hand side of the halo rather than anywhere else.
//
// This path encodes the whole frame into SRAM up front and lets the RMT's DMA
// engine clock it out, so no interrupt has to arrive on time for the frame to
// come out correct.
// -----------------------------------------------------------------------------

// Claims an RMT channel with DMA. Safe to call more than once.
bool ledOutputBegin(uint8_t pin, uint16_t pixelCount);

// False means the channel could not be claimed and the caller should keep
// using Adafruit_NeoPixel::show(), which still works but stays exposed to the
// stall described above.
bool ledOutputReady();

// Clocks one frame out and returns once it has been sent. `colours` is the
// NeoPixel library's own buffer: already in strand order and already scaled by
// the current brightness.
void ledOutputShow(const uint8_t *colours, size_t byteCount);
