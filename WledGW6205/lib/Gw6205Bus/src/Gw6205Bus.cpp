// Gw6205Bus — implementation.
// See include/Gw6205Bus.h for API contract and design notes.

#include "Gw6205Bus.h"

#if defined(ARDUINO_ARCH_ESP32)

#include <Arduino.h>
#include "driver/rmt.h"
#include <string.h>
#include <stdlib.h>

// ── RMT Timing (ported verbatim from TestGW6205) ─────────────────────
// Clock: 80 MHz / 2 = 40 MHz → 25 ns per tick
//
// GW6205 NRZ bit encoding (800 kHz, 1.25 us per bit):
//   Logic 1: 750 ns high (30 ticks) + 500 ns low  (20 ticks)
//   Logic 0: 250 ns high (10 ticks) + 1000 ns low (40 ticks)
//
// Reset: line held low for >80 us (conservative; WS2812 uses >50 us)

static constexpr uint8_t  RMT_CLK_DIV = 2;    // 40 MHz
static constexpr uint16_t T1H_TICKS   = 30;   // 750 ns
static constexpr uint16_t T1L_TICKS   = 20;   // 500 ns
static constexpr uint16_t T0H_TICKS   = 10;   // 250 ns
static constexpr uint16_t T0L_TICKS   = 40;   // 1000 ns
static constexpr uint16_t RESET_US    = 80;

// Pre-built symbols for logic 0 and 1. Initialized once on first Begin().
static rmt_item32_t s_bit0;
static rmt_item32_t s_bit1;
static bool         s_bitsReady = false;

static void initRmtBitItems() {
    if (s_bitsReady) return;
    s_bit1.duration0 = T1H_TICKS; s_bit1.level0 = 1;
    s_bit1.duration1 = T1L_TICKS; s_bit1.level1 = 0;
    s_bit0.duration0 = T0H_TICKS; s_bit0.level0 = 1;
    s_bit0.duration1 = T0L_TICKS; s_bit0.level1 = 0;
    s_bitsReady = true;
}

// ─────────────────────────────────────────────────────────────────────

Gw6205Bus::Gw6205Bus(uint16_t numLeds, uint8_t pin, uint8_t rmtChannel,
                     Gw6205ColorOrder order)
    : _numLeds(numLeds),
      _pin(pin),
      _channel(rmtChannel),
      _order(order),
      _brightness(255),
      _initialized(false),
      _pixels(nullptr),
      _rmtItems(nullptr)
{
}

Gw6205Bus::~Gw6205Bus() {
    if (_pixels)   { free(_pixels);   _pixels   = nullptr; }
    if (_rmtItems) { free(_rmtItems); _rmtItems = nullptr; }
    if (_initialized) {
        rmt_driver_uninstall((rmt_channel_t)_channel);
        _initialized = false;
    }
}

bool Gw6205Bus::Begin() {
    if (_initialized) return true;
    initRmtBitItems();

    _pixels = (Gw6205Pixel *)calloc(_numLeds, sizeof(Gw6205Pixel));

    // 36 RMT items per pixel (12 bits × 3 channels)
    size_t numItems = (size_t)_numLeds * 36;
    _rmtItems = malloc(numItems * sizeof(rmt_item32_t));

    if (!_pixels || !_rmtItems) {
        Serial.println(F("Gw6205Bus: buffer alloc failed"));
        return false;
    }

    rmt_config_t config = RMT_DEFAULT_CONFIG_TX((gpio_num_t)_pin,
                                                (rmt_channel_t)_channel);
    config.clk_div = RMT_CLK_DIV;
    // 2 memory blocks → 128 symbols → 64-symbol ping-pong halves.
    // Headroom for the driver to keep refilling ahead of transmission.
    // Consumes the next channel's memory: if _channel is 0, channel 1
    // becomes unavailable.
    config.mem_block_num = 2;

    esp_err_t err = rmt_config(&config);
    if (err != ESP_OK) {
        Serial.printf("Gw6205Bus: rmt_config err %s\n", esp_err_to_name(err));
        return false;
    }

    err = rmt_driver_install(config.channel, 0, 0);
    if (err != ESP_OK) {
        Serial.printf("Gw6205Bus: rmt_driver_install err %s\n", esp_err_to_name(err));
        return false;
    }

    _initialized = true;
    Serial.printf("Gw6205Bus: %u LEDs on GPIO %u (RMT ch%u, %u bytes)\n",
                  (unsigned)_numLeds, (unsigned)_pin, (unsigned)_channel,
                  (unsigned)(numItems * sizeof(rmt_item32_t)));
    return true;
}

void Gw6205Bus::setPixelColor(uint16_t index, uint8_t r, uint8_t g, uint8_t b, uint8_t /*w*/) {
    if (index >= _numLeds || !_pixels) return;
    _pixels[index].r = expand8to12(r);
    _pixels[index].g = expand8to12(g);
    _pixels[index].b = expand8to12(b);
}

uint32_t Gw6205Bus::getPixelColor(uint16_t index) const {
    if (index >= _numLeds || !_pixels) return 0;
    // Downsample 12-bit → 8-bit by dropping the 4 LSBs
    uint8_t r8 = (uint8_t)(_pixels[index].r >> 4);
    uint8_t g8 = (uint8_t)(_pixels[index].g >> 4);
    uint8_t b8 = (uint8_t)(_pixels[index].b >> 4);
    // WLED packed color: 0x00RRGGBB (no white channel for GW6205)
    return ((uint32_t)r8 << 16) | ((uint32_t)g8 << 8) | (uint32_t)b8;
}

void Gw6205Bus::setBrightness(uint8_t brightness) {
    _brightness = brightness;
}

void Gw6205Bus::clear() {
    if (_pixels) memset(_pixels, 0, _numLeds * sizeof(Gw6205Pixel));
}

bool Gw6205Bus::Show() {
    if (!_initialized || !_pixels || !_rmtItems) return false;

    rmt_item32_t *items = (rmt_item32_t *)_rmtItems;
    size_t idx = 0;

    // Apply brightness in the 12-bit domain for finer control.
    // brightness 255 → multiplier = 1.0, 0 → 0.
    // (val * brightness + 128) >> 8 gives rounded scaling.
    const uint16_t bri = _brightness;

    for (uint16_t i = 0; i < _numLeds; i++) {
        uint16_t r12 = (uint16_t)(((uint32_t)_pixels[i].r * bri + 128) >> 8);
        uint16_t g12 = (uint16_t)(((uint32_t)_pixels[i].g * bri + 128) >> 8);
        uint16_t b12 = (uint16_t)(((uint32_t)_pixels[i].b * bri + 128) >> 8);
        if (r12 > 0xFFF) r12 = 0xFFF;
        if (g12 > 0xFFF) g12 = 0xFFF;
        if (b12 > 0xFFF) b12 = 0xFFF;

        uint16_t c0, c1, c2;
        switch (_order) {
            case GW6205_ORDER_GRB: c0 = g12; c1 = r12; c2 = b12; break;
            case GW6205_ORDER_BRG: c0 = b12; c1 = r12; c2 = g12; break;
            case GW6205_ORDER_RGB:
            default:               c0 = r12; c1 = g12; c2 = b12; break;
        }

        // 12 bits per channel, MSB first
        for (int bit = 11; bit >= 0; bit--)
            items[idx++] = (c0 >> bit) & 1 ? s_bit1 : s_bit0;
        for (int bit = 11; bit >= 0; bit--)
            items[idx++] = (c1 >> bit) & 1 ? s_bit1 : s_bit0;
        for (int bit = 11; bit >= 0; bit--)
            items[idx++] = (c2 >> bit) & 1 ? s_bit1 : s_bit0;
    }

    esp_err_t err = rmt_write_items(
        (rmt_channel_t)_channel,
        items,
        idx,
        true  // wait for TX complete
    );
    delayMicroseconds(RESET_US);
    return err == ESP_OK;
}

#endif // ARDUINO_ARCH_ESP32
