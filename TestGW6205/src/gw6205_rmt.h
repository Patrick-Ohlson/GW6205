#pragma once

#include <stdint.h>
#include <stddef.h>

// GW6205 12-bit color (0-4095 per channel)
struct GW6205Color {
    uint16_t r;
    uint16_t g;
    uint16_t b;
};

// Color order on the wire (default RGB, but may need GRB — verify with hardware)
enum GW6205ColorOrder {
    GW6205_RGB,
    GW6205_GRB,
    GW6205_BRG,
};

class GW6205Strip {
public:
    // pin: GPIO for data output
    // numLeds: number of LEDs in the chain
    // channel: RMT channel (0-7 on ESP32)
    // order: color channel order on the wire
    GW6205Strip(uint8_t pin, uint16_t numLeds, uint8_t channel = 0,
                GW6205ColorOrder order = GW6205_RGB);
    ~GW6205Strip();

    bool begin();

    // Set a single pixel (r/g/b are 12-bit: 0-4095)
    void setPixel(uint16_t index, uint16_t r, uint16_t g, uint16_t b);

    // Fill all pixels with one color
    void fill(uint16_t r, uint16_t g, uint16_t b);

    // All pixels off
    void clear();

    // Transmit pixel data via RMT (interrupt-driven, non-blocking after start)
    bool show();

    uint16_t numPixels() const { return _numLeds; }

    // Helper: convert 8-bit color to 12-bit (for compatibility)
    static uint16_t to12bit(uint8_t val8) { return (uint16_t)val8 << 4 | val8 >> 4; }

private:
    uint8_t  _pin;
    uint8_t  _channel;
    uint16_t _numLeds;
    GW6205ColorOrder _order;
    GW6205Color *_pixels;
    void *_rmtItems;        // Pre-allocated RMT symbol buffer (numLeds * 36 items)
    bool _initialized;
};
