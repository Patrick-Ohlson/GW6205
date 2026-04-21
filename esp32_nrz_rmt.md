# ESP32 RMT for NRZ LED Protocols

## What is RMT?

RMT (Remote Control Transceiver) is a hardware peripheral built into the ESP32 chip. Originally designed for infrared remote control signals, it generates precise pulse waveforms on GPIO pins independently of the CPU.

## Why RMT for Addressable LEDs

NRZ LED protocols (WS2812, GW6205, etc.) require nanosecond-precise timing on a single data wire. Software bit-banging with `digitalWrite()` fails because any interrupt (WiFi, Bluetooth, RTOS scheduler) corrupts the timing and causes visible flickering.

RMT solves this by generating the waveform **in hardware**. Once started, the signal cannot be disrupted — not by WiFi, not by user code, nothing.

## How It Works

1. **Clock configuration**: RMT uses a configurable clock divider from the 80 MHz APB clock. With `clk_div=2`, the RMT runs at 40 MHz (25 ns per tick), giving more than enough resolution for NRZ timing.

2. **RMT symbols**: Each symbol is a pair of (duration, level) values — a high period followed by a low period. One NRZ bit = one RMT symbol:
   - Logic 1: 30 ticks high (750 ns) + 20 ticks low (500 ns)
   - Logic 0: 10 ticks high (250 ns) + 40 ticks low (1000 ns)

3. **Translator callback**: A function registered with `rmt_translator_init()` that runs in **ISR (interrupt) context**. The RMT hardware fires interrupts to request more symbols, and the translator converts pixel data into RMT symbols on the fly. This avoids pre-allocating a massive symbol buffer.

4. **Transmission**: Calling `rmt_write_sample()` starts the hardware. The RMT peripheral pulls symbols from the translator via interrupts and clocks them out on the GPIO pin. The CPU is free to do other work.

## RMT Symbol Memory

The ESP32 RMT has 512 symbols of shared memory across 8 channels (64 symbols per channel by default). With the translator callback, this small buffer is refilled via interrupts, allowing arbitrarily long LED chains without proportional memory use.

Per-LED memory cost:
- Pixel data: 6 bytes (3 × uint16_t for 12-bit RGB)
- RMT symbols: only the 64-symbol hardware buffer is needed (shared, refilled on the fly)

## GW6205-Specific Notes

The GW6205 uses the same NRZ encoding as WS2812 but transmits **36 bits per pixel** (12 bits × 3 channels) instead of 24. The only difference in the RMT driver is that the translator emits 36 symbols per pixel instead of 24. The bit timing is identical to WS2812 at 800 kHz.

| Parameter | WS2812 | GW6205 |
|-----------|--------|--------|
| Bits per pixel | 24 | 36 |
| RMT symbols per pixel | 24 | 36 |
| Bit timing | Same | Same |
| Data rate | 800 kHz | 800 kHz |
| Time per pixel | 30 us | 45 us |

## ESP32 RMT Availability

| Chip | RMT TX Channels | RMT RX Channels |
|------|-----------------|-----------------|
| ESP32 | 8 (shared TX/RX) | 8 (shared TX/RX) |
| ESP32-S2 | 4 | 4 |
| ESP32-S3 | 4 | 4 |
| ESP32-C3 | 2 | 2 |
| ESP32-C6 | 2 | 2 |

## API Notes (Legacy vs New)

- **Legacy API** (`driver/rmt.h`): `rmt_config()`, `rmt_driver_install()`, `rmt_translator_init()`, `rmt_write_sample()`. Used in Arduino ESP32 core v2.x (ESP-IDF 4.4). Still available but deprecated in ESP-IDF 5.x.
- **New API** (ESP-IDF 5.x): `rmt_new_tx_channel()`, custom `rmt_encoder_t` interface. More flexible but more verbose. Arduino ESP32 core v3.x supports this.

Our TestGW6205 firmware uses the legacy API for broad compatibility.
