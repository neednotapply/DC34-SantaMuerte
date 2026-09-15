#include "led_output.h"

#include <driver/rmt_encoder.h>
#include <driver/rmt_tx.h>

// Serial is a macro for the USB CDC console; without this the warnings below
// would go out UART0, where nothing is listening.
#include "usb_console.h"

namespace {

// 0.1us per tick. The bit shapes below are the ones the NeoPixel library's own
// ESP-IDF 5 path uses, and they are already proven on this strand -- only the
// way they reach the pin changes here.
constexpr uint32_t RMT_RESOLUTION_HZ = 10UL * 1000UL * 1000UL;
constexpr uint16_t WS2812_ZERO_HIGH_TICKS = 4;  // 0.4us high
constexpr uint16_t WS2812_ZERO_LOW_TICKS = 8;   // 0.8us low
constexpr uint16_t WS2812_ONE_HIGH_TICKS = 8;   // 0.8us high
constexpr uint16_t WS2812_ONE_LOW_TICKS = 4;    // 0.4us low

// The strand latches after the line has rested low for this long. Frames are
// 20ms apart so this only ever matters if two are pushed back to back.
constexpr uint32_t LATCH_US = 300;

// Bits per pixel: one WS2812 bit is one RMT symbol.
constexpr size_t SYMBOLS_PER_PIXEL = 24;

rmt_channel_handle_t channel = nullptr;
rmt_encoder_handle_t encoder = nullptr;
uint32_t frameEndedAt = 0;

}  // namespace

bool ledOutputBegin(uint8_t pin, uint16_t pixelCount) {
  if (channel && encoder) return true;
  if (pixelCount == 0) return false;

  // The driver halves its DMA buffer and raises an interrupt when the first
  // half drains. Asking for more than twice the frame keeps the entire frame
  // inside that first half: the encoder runs once, from this task, and the
  // transfer completes without an interrupt ever being on the critical path.
  const size_t frameSymbols = static_cast<size_t>(pixelCount) * SYMBOLS_PER_PIXEL;
  size_t memBlockSymbols = (frameSymbols + 64) * 2;
  if (memBlockSymbols & 1) ++memBlockSymbols;  // the driver requires an even size

  rmt_tx_channel_config_t channelConfig = {};
  channelConfig.gpio_num = static_cast<gpio_num_t>(pin);
  channelConfig.clk_src = RMT_CLK_SRC_DEFAULT;
  channelConfig.resolution_hz = RMT_RESOLUTION_HZ;
  channelConfig.mem_block_symbols = memBlockSymbols;
  channelConfig.trans_queue_depth = 2;
  channelConfig.flags.with_dma = 1;

  esp_err_t err = rmt_new_tx_channel(&channelConfig, &channel);
  if (err != ESP_OK) {
    channel = nullptr;
    Serial.printf("[LED] WARNING: no DMA-capable RMT channel (%s)\r\n",
                  esp_err_to_name(err));
    return false;
  }

  rmt_bytes_encoder_config_t encoderConfig = {};
  encoderConfig.bit0.level0 = 1;
  encoderConfig.bit0.duration0 = WS2812_ZERO_HIGH_TICKS;
  encoderConfig.bit0.level1 = 0;
  encoderConfig.bit0.duration1 = WS2812_ZERO_LOW_TICKS;
  encoderConfig.bit1.level0 = 1;
  encoderConfig.bit1.duration0 = WS2812_ONE_HIGH_TICKS;
  encoderConfig.bit1.level1 = 0;
  encoderConfig.bit1.duration1 = WS2812_ONE_LOW_TICKS;
  encoderConfig.flags.msb_first = 1;

  err = rmt_new_bytes_encoder(&encoderConfig, &encoder);
  if (err != ESP_OK) {
    encoder = nullptr;
    rmt_del_channel(channel);
    channel = nullptr;
    Serial.printf("[LED] WARNING: RMT encoder unavailable (%s)\r\n",
                  esp_err_to_name(err));
    return false;
  }

  err = rmt_enable(channel);
  if (err != ESP_OK) {
    rmt_del_encoder(encoder);
    encoder = nullptr;
    rmt_del_channel(channel);
    channel = nullptr;
    Serial.printf("[LED] WARNING: RMT channel would not start (%s)\r\n",
                  esp_err_to_name(err));
    return false;
  }

  Serial.printf(
      "[LED] RMT DMA output on GPIO%u: %u symbols per frame, %u buffered\r\n",
      static_cast<unsigned>(pin), static_cast<unsigned>(frameSymbols),
      static_cast<unsigned>(memBlockSymbols));
  return true;
}

bool ledOutputReady() { return channel != nullptr && encoder != nullptr; }

void ledOutputShow(const uint8_t *colours, size_t byteCount) {
  if (!ledOutputReady() || colours == nullptr || byteCount == 0) return;

  while (micros() - frameEndedAt < LATCH_US) {
  }

  rmt_transmit_config_t transmitConfig = {};
  transmitConfig.loop_count = 0;
  transmitConfig.flags.eot_level = 0;  // rest low afterwards so the strand latches

  // The payload has to stay put until the transfer finishes, which is why this
  // waits rather than queueing and returning: the buffer it points at is the
  // one the next frame is about to be rendered into.
  if (rmt_transmit(channel, encoder, colours, byteCount, &transmitConfig) ==
      ESP_OK) {
    rmt_tx_wait_all_done(channel, 100);
  }
  frameEndedAt = micros();
}
