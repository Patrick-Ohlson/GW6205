#include <Arduino.h>
#include "gw6205_rmt.h"

// ── Configuration (override in platformio.ini build_flags) ──────────────
#ifndef GW6205_DATA_PIN
#define GW6205_DATA_PIN 16
#endif

#ifndef GW6205_NUM_LEDS
#define GW6205_NUM_LEDS 5
#endif

// Max brightness for the primary-color tests. Set below 0x0FFF to dim
// the test output (useful for diagnosing whether the chip is actually
// responding to 12-bit values or stuck at full intensity).
#ifndef TEST_BRIGHTNESS
#define TEST_BRIGHTNESS 0x0050
#endif

GW6205Strip strip(GW6205_DATA_PIN, GW6205_NUM_LEDS, /*rmtChannel=*/0, GW6205_RGB);

// ── Demo patterns ───────────────────────────────────────────────────────

// Show a single solid color on all LEDs
void showSolid(uint16_t r, uint16_t g, uint16_t b, const char *name, uint32_t ms) {
    Serial.printf("  %s: R=%04X G=%04X B=%04X\n", name, r, g, b);
    strip.fill(r, g, b);
    strip.show();
    delay(ms);
}

// Smooth 12-bit fade on one channel — demonstrates 4096-level resolution.
// At 8-bit this would show 16 visible steps per 256-count block;
// at 12-bit the fade should be perfectly smooth.
void demo12BitFade() {
    Serial.println("[Fade] 12-bit red gradient 0→4095");
    for (uint16_t val = 0; val < 4096; val += 4) {
        strip.fill(val, 0, 0);
        strip.show();
        delay(2);
    }
    Serial.println("[Fade] 12-bit green gradient 0→4095");
    for (uint16_t val = 0; val < 4096; val += 4) {
        strip.fill(0, val, 0);
        strip.show();
        delay(2);
    }
    Serial.println("[Fade] 12-bit blue gradient 0→4095");
    for (uint16_t val = 0; val < 4096; val += 4) {
        strip.fill(0, 0, val);
        strip.show();
        delay(2);
    }
}

// HSV-to-12bit-RGB conversion (hue 0-4095, sat/val 0-4095)
GW6205Color hsv12(uint16_t h, uint16_t s, uint16_t v) {
    // h: 0-4095 mapped to 0-360 degrees
    // s, v: 0-4095
    uint32_t region = (h * 6) >> 12;  // 0-5
    uint32_t remainder = ((h * 6) - (region << 12));  // fractional part 0-4095

    uint32_t p = (v * (4095 - s)) >> 12;
    uint32_t q = (v * (4095 - ((s * remainder) >> 12))) >> 12;
    uint32_t t = (v * (4095 - ((s * (4095 - remainder)) >> 12))) >> 12;

    GW6205Color c;
    switch (region) {
        case 0:  c = {(uint16_t)v, (uint16_t)t, (uint16_t)p}; break;
        case 1:  c = {(uint16_t)q, (uint16_t)v, (uint16_t)p}; break;
        case 2:  c = {(uint16_t)p, (uint16_t)v, (uint16_t)t}; break;
        case 3:  c = {(uint16_t)p, (uint16_t)q, (uint16_t)v}; break;
        case 4:  c = {(uint16_t)t, (uint16_t)p, (uint16_t)v}; break;
        default: c = {(uint16_t)v, (uint16_t)p, (uint16_t)q}; break;
    }
    return c;
}

// Rainbow cycle across all LEDs
void demoRainbow(uint32_t durationMs) {
    Serial.println("[Rainbow] cycling all hues");
    uint32_t start = millis();
    uint16_t offset = 0;

    while (millis() - start < durationMs) {
        for (uint16_t i = 0; i < strip.numPixels(); i++) {
            uint16_t hue = (offset + (i * 4096 / strip.numPixels())) & 0x0FFF;
            GW6205Color c = hsv12(hue, 4095, 4095);
            strip.setPixel(i, c.r, c.g, c.b);
        }
        strip.show();
        offset = (offset + 8) & 0x0FFF;
        delay(10);
    }
}

// Pixel chase — one lit pixel moving along the strip
void demoChase(uint16_t r, uint16_t g, uint16_t b, uint32_t durationMs) {
    Serial.printf("[Chase] R=%04X G=%04X B=%04X\n", r, g, b);
    uint32_t start = millis();
    uint16_t pos = 0;

    while (millis() - start < durationMs) {
        strip.clear();
        strip.setPixel(pos, r, g, b);
        strip.show();
        pos = (pos + 1) % strip.numPixels();
        delay(100);
    }
}

// 12-bit vs 8-bit comparison: alternate between 12-bit smooth and 8-bit quantized
void demo12vs8() {
    Serial.println("[12vs8] Left half: 12-bit smooth | Right half: 8-bit quantized");
    uint16_t half = strip.numPixels() / 2;
    if (half == 0) half = 1;

    for (uint16_t val = 0; val < 4096; val += 2) {
        // Left: full 12-bit resolution
        for (uint16_t i = 0; i < half; i++)
            strip.setPixel(i, val, 0, 0);

        // Right: quantized to 8-bit (keep only upper 8 bits, replicate to 12)
        uint16_t val8 = (val >> 4) << 4;  // truncate lower 4 bits
        for (uint16_t i = half; i < strip.numPixels(); i++)
            strip.setPixel(i, val8, 0, 0);

        strip.show();
        delay(1);
    }
}

// ── Main ────────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    delay(500);

    Serial.println("========================================");
    Serial.println("  GW6205 12-bit LED Test Firmware");
    Serial.printf("  GPIO: %d  |  LEDs: %d\n", GW6205_DATA_PIN, GW6205_NUM_LEDS);
    Serial.println("========================================");

    if (!strip.begin()) {
        Serial.println("FATAL: GW6205 init failed");
        while (1) delay(1000);
    }

    strip.clear();
    strip.show();
    delay(500);
}

void loop() {
    // ── Primary colors at full brightness ──
    Serial.println("\n[Solid Colors]");
    showSolid(TEST_BRIGHTNESS, 0x0000,         0x0000,         "RED",     2000);
    showSolid(0x0000,         TEST_BRIGHTNESS, 0x0000,         "GREEN",   2000);
    showSolid(0x0000,         0x0000,         TEST_BRIGHTNESS, "BLUE",    2000);
    showSolid(TEST_BRIGHTNESS, TEST_BRIGHTNESS, 0x0000,         "YELLOW",  2000);
    showSolid(TEST_BRIGHTNESS, 0x0000,         TEST_BRIGHTNESS, "MAGENTA", 2000);
    showSolid(0x0000,         TEST_BRIGHTNESS, TEST_BRIGHTNESS, "CYAN",    2000);
    showSolid(TEST_BRIGHTNESS, TEST_BRIGHTNESS, TEST_BRIGHTNESS, "WHITE",   2000);

    // ── 12-bit smooth fade ──
    Serial.println("\n[12-bit Fade]");
    demo12BitFade();

    // ── 12-bit vs 8-bit comparison ──
    Serial.println("\n[12-bit vs 8-bit]");
    demo12vs8();

    // ── Rainbow ──
    Serial.println("\n[Rainbow]");
    demoRainbow(10000);

    // ── Chase ──
    Serial.println("\n[Chase]");
    demoChase(TEST_BRIGHTNESS, 0x0000, 0x0000, 5000);
    demoChase(0x0000, TEST_BRIGHTNESS, 0x0000, 5000);
    demoChase(0x0000, 0x0000, TEST_BRIGHTNESS, 5000);

    // ── Off, then restart ──
    strip.clear();
    strip.show();
    Serial.println("\n--- Cycle complete, restarting in 3s ---\n");
    delay(3000);
}
