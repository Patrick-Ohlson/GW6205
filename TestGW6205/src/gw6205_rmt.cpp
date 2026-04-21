#include "gw6205_rmt.h"
#include <Arduino.h>
#include "driver/rmt.h"
#include <string.h>

// ── RMT Timing ──────────────────────────────────────────────────────────
// Clock: 80 MHz / 2 = 40 MHz → 25 ns per tick
//
// GW6205 NRZ bit encoding (800 kHz, 1.25 us per bit):
//   Logic 1: 750 ns high (30 ticks) + 500 ns low  (20 ticks)
//   Logic 0: 250 ns high (10 ticks) + 1000 ns low (40 ticks)
//
// Reset: line held low for >80 us (conservative; WS2812 uses >50 us)

static constexpr uint8_t  RMT_CLK_DIV    = 2;      // 40 MHz
static constexpr uint16_t T1H_TICKS      = 30;     // 750 ns
static constexpr uint16_t T1L_TICKS      = 20;     // 500 ns
static constexpr uint16_t T0H_TICKS      = 10;     // 250 ns
static constexpr uint16_t T0L_TICKS      = 40;     // 1000 ns
static constexpr uint16_t RESET_US       = 80;

// Pre-built RMT symbols for logic 0 and 1
static rmt_item32_t _bit0;
static rmt_item32_t _bit1;

static void init_rmt_items() {
    _bit1.duration0 = T1H_TICKS;
    _bit1.level0    = 1;
    _bit1.duration1 = T1L_TICKS;
    _bit1.level1    = 0;

    _bit0.duration0 = T0H_TICKS;
    _bit0.level0    = 1;
    _bit0.duration1 = T0L_TICKS;
    _bit0.level1    = 0;
}

// ── GW6205Strip implementation ──────────────────────────────────────────

GW6205Strip::GW6205Strip(uint8_t pin, uint16_t numLeds, uint8_t channel,
                         GW6205ColorOrder order)
    : _pin(pin), _channel(channel), _numLeds(numLeds), _order(order),
      _pixels(nullptr), _rmtItems(nullptr), _initialized(false)
{
}

GW6205Strip::~GW6205Strip() {
    if (_pixels)   { free(_pixels);   _pixels = nullptr; }
    if (_rmtItems) { free(_rmtItems); _rmtItems = nullptr; }
    if (_initialized) {
        rmt_driver_uninstall((rmt_channel_t)_channel);
    }
}

bool GW6205Strip::begin() {
    init_rmt_items();

    _pixels = (GW6205Color *)calloc(_numLeds, sizeof(GW6205Color));

    // Pre-allocate full symbol buffer: 36 RMT items per pixel.
    size_t numItems = (size_t)_numLeds * 36;
    _rmtItems = malloc(numItems * sizeof(rmt_item32_t));

    if (!_pixels || !_rmtItems) {
        Serial.println("GW6205: failed to allocate buffers");
        return false;
    }

    rmt_config_t config = RMT_DEFAULT_CONFIG_TX((gpio_num_t)_pin, (rmt_channel_t)_channel);
    config.clk_div = RMT_CLK_DIV;
    // 2 memory blocks gives the driver headroom to keep the ping-pong
    // refill ahead of transmission. Consumes the next channel's memory too.
    config.mem_block_num = 2;

    esp_err_t err = rmt_config(&config);
    if (err != ESP_OK) {
        Serial.printf("GW6205: rmt_config failed: %s\n", esp_err_to_name(err));
        return false;
    }

    err = rmt_driver_install(config.channel, 0, 0);
    if (err != ESP_OK) {
        Serial.printf("GW6205: rmt_driver_install failed: %s\n", esp_err_to_name(err));
        return false;
    }

    _initialized = true;
    Serial.printf("GW6205: initialized %d LEDs on GPIO %d (RMT ch%d, %u items, %u bytes)\n",
                  _numLeds, _pin, _channel,
                  (unsigned)numItems, (unsigned)(numItems * sizeof(rmt_item32_t)));
    return true;
}

void GW6205Strip::setPixel(uint16_t index, uint16_t r, uint16_t g, uint16_t b) {
    if (index >= _numLeds) return;
    _pixels[index].r = r & 0x0FFF;
    _pixels[index].g = g & 0x0FFF;
    _pixels[index].b = b & 0x0FFF;
}

void GW6205Strip::fill(uint16_t r, uint16_t g, uint16_t b) {
    for (uint16_t i = 0; i < _numLeds; i++)
        setPixel(i, r, g, b);
}

void GW6205Strip::clear() {
    memset(_pixels, 0, _numLeds * sizeof(GW6205Color));
}

bool GW6205Strip::show() {
    if (!_initialized) return false;

    rmt_item32_t *items = (rmt_item32_t *)_rmtItems;
    size_t idx = 0;

    // Build the full symbol buffer for the frame.
    // 12 bits per channel, 3 channels per pixel, MSB first.
    for (uint16_t i = 0; i < _numLeds; i++) {
        uint16_t c0, c1, c2;
        switch (_order) {
            case GW6205_RGB:
                c0 = _pixels[i].r; c1 = _pixels[i].g; c2 = _pixels[i].b; break;
            case GW6205_GRB:
                c0 = _pixels[i].g; c1 = _pixels[i].r; c2 = _pixels[i].b; break;
            case GW6205_BRG:
                c0 = _pixels[i].b; c1 = _pixels[i].r; c2 = _pixels[i].g; break;
            default:
                c0 = _pixels[i].r; c1 = _pixels[i].g; c2 = _pixels[i].b; break;
        }

        for (int bit = 11; bit >= 0; bit--)
            items[idx++] = (c0 >> bit) & 1 ? _bit1 : _bit0;
        for (int bit = 11; bit >= 0; bit--)
            items[idx++] = (c1 >> bit) & 1 ? _bit1 : _bit0;
        for (int bit = 11; bit >= 0; bit--)
            items[idx++] = (c2 >> bit) & 1 ? _bit1 : _bit0;
    }

    // rmt_write_items is interrupt-driven: the driver copies from our buffer
    // into the RMT hardware memory on each ISR until idx items are sent.
    // No translator callback → no unfilled-slot garbage.
    esp_err_t err = rmt_write_items(
        (rmt_channel_t)_channel,
        items,
        idx,
        true  // wait for tx complete
    );

    // Hold line low for reset period to latch the chain.
    delayMicroseconds(RESET_US);

    return err == ESP_OK;
}
