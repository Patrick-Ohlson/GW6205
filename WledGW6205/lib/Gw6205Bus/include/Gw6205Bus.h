// Gw6205Bus — ESP32 RMT driver for GW6205 12-bit addressable LED chipsets.
//
// Designed to be driven from WLED's PolyBus dispatcher (wled00/bus_wrapper.h)
// for TYPE_GW6205, but has no hard dependency on WLED and can be used
// standalone.
//
// Accepts 8-bit-per-channel input from WLED's color pipeline and upscales
// to 12-bit on the wire via bit replication (val8 << 4 | val8 >> 4), so
// 0x00 -> 0x000 and 0xFF -> 0xFFF with evenly distributed intermediate
// values.
//
// Ported from TestGW6205/src/gw6205_rmt.{h,cpp} (validated on ESP32
// hardware with 5-LED test strip at 800 kHz).

#pragma once

#include <stdint.h>
#include <stddef.h>

#if defined(ARDUINO_ARCH_ESP32)

// GW6205 color channel order on the wire. RGB is the default validated
// against TestGW6205. Some batches of strips may require GRB or BRG.
enum Gw6205ColorOrder : uint8_t {
    GW6205_ORDER_RGB = 0,
    GW6205_ORDER_GRB = 1,
    GW6205_ORDER_BRG = 2,
};

class Gw6205Bus {
public:
    // Construct but do not initialize hardware. Call Begin() once the
    // pin/channel/length are finalized.
    Gw6205Bus(uint16_t numLeds, uint8_t pin, uint8_t rmtChannel = 0,
              Gw6205ColorOrder order = GW6205_ORDER_RGB);
    ~Gw6205Bus();

    // Non-copyable
    Gw6205Bus(const Gw6205Bus&) = delete;
    Gw6205Bus& operator=(const Gw6205Bus&) = delete;

    // Allocate buffers, configure RMT, install driver. Returns true on
    // success. Safe to call once per instance.
    bool Begin();

    // Write an 8-bit RGB pixel. Upscales internally to 12-bit.
    // Extra 'w' parameter is ignored (GW6205 has no white channel) but
    // kept in the signature so callers from WLED's RGBW code path
    // compile without special-casing.
    void setPixelColor(uint16_t index, uint8_t r, uint8_t g, uint8_t b, uint8_t w = 0);

    // Retrieve 8-bit RGB for a previously-set pixel (downsampled from
    // stored 12-bit by dropping the 4 LSBs). Used by WLED for effects
    // that read back current state.
    uint32_t getPixelColor(uint16_t index) const;

    // Apply a master brightness (0-255) scaled into the 12-bit values.
    // Called by WLED via the PolyBus show() dispatch.
    void setBrightness(uint8_t brightness);

    // Clear all pixels to off. Does not transmit.
    void clear();

    // Build RMT symbol buffer and push to the strip. Blocks until TX
    // complete + reset latch.
    bool Show();

    // True when the previous Show() has completed (always true since
    // Show() is currently blocking). Provided for WLED API parity.
    bool canShow() const { return true; }

    uint16_t numPixels() const { return _numLeds; }
    uint8_t  getPin() const { return _pin; }

private:
    // Pixel storage: 12-bit R/G/B as packed uint16_t per channel
    // (upper 4 bits zero). 6 bytes per pixel.
    struct Gw6205Pixel {
        uint16_t r;
        uint16_t g;
        uint16_t b;
    };

    uint16_t         _numLeds;
    uint8_t          _pin;
    uint8_t          _channel;
    Gw6205ColorOrder _order;
    uint8_t          _brightness;
    bool             _initialized;

    Gw6205Pixel *_pixels;
    void        *_rmtItems;   // rmt_item32_t[numLeds * 36]

    static uint16_t expand8to12(uint8_t v) {
        // Bit replication: 0x00 -> 0x000, 0xFF -> 0xFFF, even distribution
        return ((uint16_t)v << 4) | (v >> 4);
    }
};

#endif // ARDUINO_ARCH_ESP32
