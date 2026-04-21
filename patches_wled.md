# WLED Patches for GW6205 Support

This document describes every change applied to the upstream WLED tree
(`../wled/wled00/`) by the WledGW6205 fork. The changes are also captured
mechanically as a git diff in `patches/wled-gw6205.patch` for re-application
after an upstream pull.

**Total impact**: 3 files touched, ~265 lines changed (~50 added, the rest are
`#ifndef` gates threaded through existing switch statements).

All changes are gated on `WLED_GW6205_ONLY` (set in `WledGW6205/platformio.ini`)
so the unmodified upstream code paths still work in a non-GW6205 build.

---

## File 1: `wled/wled00/const.h`

**Purpose**: Register the new LED type ID and switch the default type when
running in GW6205-only mode.

### Change 1 — Add `TYPE_GW6205` define

In the digital LED type block (around line 311), append a new entry between
`TYPE_SM16825` and the closing `TYPE_DIGITAL_MAX`:

```c
#define TYPE_SM16825             34            //RGB + WW + CW
// GW6205 12-bit (custom, added by WledGW6205 fork)
#define TYPE_GW6205              35            //GW6205 12-bit per channel, 800kHz NRZ
#define TYPE_DIGITAL_MAX         39            // last usable digital type
```

ID `35` was chosen because it's the next free slot in WLED's digital type
range (16–39) and doesn't collide with any existing or known upstream type.

### Change 2 — Gate `DEFAULT_LED_TYPE`

Around line 669, replace the unconditional default with a gated version:

```c
#ifdef WLED_GW6205_ONLY
#define DEFAULT_LED_TYPE TYPE_GW6205
#else
#define DEFAULT_LED_TYPE TYPE_WS2812_RGB
#endif
```

This makes "GW6205 12-bit" the pre-selected entry in the LED type dropdown
on a fresh install.

**Total**: ~6 lines added. No existing lines modified or removed.

---

## File 2: `wled/wled00/bus_wrapper.h`

**Purpose**: Plug the `Gw6205Bus` driver into WLED's `PolyBus` dispatcher so
that selecting `TYPE_GW6205` in the UI routes through our RMT driver. Also
gate the unused NeoPixelBus template instantiations so the linker drops
them.

This is the largest patch — ~37 GW6205 references, 10 paired
`#ifndef WLED_GW6205_ONLY` gates.

### Change 1 — Include the driver header

After the `#include "NeoPixelBus.h"` line near the top:

```c
#include "NeoPixelBus.h"

// GW6205 12-bit LED driver (custom, added by WledGW6205 fork).
// Provides a non-NeoPixelBus Gw6205Bus class that we plug into PolyBus.
#if defined(ARDUINO_ARCH_ESP32)
#include "Gw6205Bus.h"
#endif
```

The `Gw6205Bus.h` lookup is handled by the `-I` path in
`WledGW6205/platformio.ini`.

### Change 2 — Add I-type ID

Near the other `I_32_*` defines (around line 81):

```c
/*** ESP32 Neopixel methods ***/
// GW6205 12-bit (custom, WledGW6205 fork) — picked in the high range to
// avoid colliding with existing or future I_32_* IDs.
#define I_32_GW6205_3 100
```

ID `100` is well above the existing `I_32_*` range (1–54) so future WLED
upstream additions won't collide.

### Change 3 — Add GW6205 branches in 9 PolyBus switch statements

Each ESP32 switch block in `PolyBus` follows the same pattern:

```c
#ifdef ARDUINO_ARCH_ESP32
  // RMT cases for WS2812, SK6812, etc.
  case I_32_RN_NEO_3: ... break;
  // ... many more cases ...
  // I2S cases
  #ifndef CONFIG_IDF_TARGET_ESP32C3
  case I_32_I2_NEO_3: ... break;
  #endif
#endif
```

The patch wraps these blocks like so:

```c
#ifdef ARDUINO_ARCH_ESP32
  // GW6205 first, ALWAYS compiled
  case I_32_GW6205_3: (static_cast<Gw6205Bus*>(busPtr))->Begin(); break;
  #ifndef WLED_GW6205_ONLY
  // RMT + I2S cases for everything else, gated out
  case I_32_RN_NEO_3: ... break;
  // ...
  #endif // !WLED_GW6205_ONLY
#endif
```

The 9 switch statements modified are:

| Method | Line (after patch) | GW6205 case body |
|---|---|---|
| `begin()` | ~444 | `static_cast<Gw6205Bus*>(busPtr)->Begin();` |
| `create()` | ~554 | `busPtr = new Gw6205Bus(len, pins[0], _rmtChannel++);` |
| `show()` | ~657 | `static_cast<Gw6205Bus*>(busPtr)->Show();` |
| `canShow()` | ~757 | `return static_cast<Gw6205Bus*>(busPtr)->canShow();` |
| `setPixelColor()` | ~885 | `static_cast<Gw6205Bus*>(busPtr)->setPixelColor(pix, col.R, col.G, col.B, 0);` |
| `getPixelColor()` | ~989 | `Gw6205Bus::getPixelColor()` decoded into `RgbwColor` |
| `cleanup()` | ~1109 | `delete static_cast<Gw6205Bus*>(busPtr);` |
| `getDataSize()` | ~1213 | `count * (36 * 8 + 6)` (RMT symbols + pixel buffer) |
| `memUsage()` | ~1303 | `count * (36 * 8 + 6)` |

Why `_rmtChannel++` in `create()`: this matches the pattern already used by
`B_32_RN_*` cases — PolyBus tracks RMT channel allocation across all buses,
and `Gw6205Bus`'s constructor takes a channel index that maps directly onto
the ESP32 RMT hardware channel.

### Change 4 — Add GW6205 branch to `getI()`

`getI()` maps WLED LED types (`TYPE_*`) to internal driver IDs (`I_*`). Add
a case for `TYPE_GW6205` ahead of the gated rest:

```c
switch (busType) {
  // GW6205 12-bit (custom, WledGW6205 fork). Always RMT, no I2S variant.
  case TYPE_GW6205:
    t = I_32_GW6205_3; break;
#ifndef WLED_GW6205_ONLY
  case TYPE_WS2812_1CH_X3:
  case TYPE_WS2812_2CH_X3:
  case TYPE_WS2812_RGB:
  // ... rest of existing cases ...
#endif
}
```

The GW6205 case uses a fixed ID (no `+ offset`) because there's no I2S
variant — the driver only does RMT.

### Why this gating saves flash

Each `case I_32_RN_NEO_3: busPtr = new B_32_RN_NEO_3(...)` line *forces*
the compiler to instantiate `NeoPixelBus<NeoGrbFeature, NeoEsp32RmtMethod(Ws2812x)>`,
which pulls in the full WS2812 RMT driver code. Without the gate, the
linker can't drop these instantiations because they're referenced by reachable
`switch` cases. Wrapping in `#ifndef WLED_GW6205_ONLY` removes the case
entirely, leaving the template uninstantiated, and the linker garbage-collects
the unused driver code. Estimated savings: ≥100 KB of flash.

**Total bus_wrapper.h impact**: ~50 lines added (GW6205 branches + include +
ID), ~900 lines wrapped in `#ifndef WLED_GW6205_ONLY` gates (no content
removed).

---

## File 3: `wled/wled00/bus_manager.cpp`

**Purpose**: Make GW6205 appear in the WLED web-UI LED type dropdown, and
hide the other digital types when running in GW6205-only mode.

### Change — Patch `BusDigital::getLEDTypes()`

Around line 352, prepend the GW6205 entry and gate the rest:

```cpp
std::vector<LEDType> BusDigital::getLEDTypes() {
  return {
    // GW6205 12-bit (custom, WledGW6205 fork). Listed first so it's the
    // default in the UI dropdown for GW6205-only builds.
    {TYPE_GW6205,        "D",  PSTR("GW6205 12-bit")},
#ifndef WLED_GW6205_ONLY
    {TYPE_WS2812_RGB,    "D",  PSTR("WS281x")},
    // ... other entries unchanged ...
    {TYPE_P9813,         "2P", PSTR("PP9813")},
#endif // !WLED_GW6205_ONLY
  };
}
```

This is the only edit in `bus_manager.cpp`. Everything else in the file
(BusPwm, BusOnOff, BusNetwork, BusHub75Matrix and their `getLEDTypes()`)
remains untouched — analog, on/off, virtual, and HUB75 types are still
selectable. They cost zero flash for digital-only setups but stay available
for users who want to mix GW6205 with PWM or relay outputs.

**Total bus_manager.cpp impact**: ~5 lines added.

---

## What was NOT changed

To keep the upstream sync surface small, these were deliberately left alone
even though they reference the gated-out `TYPE_*` defines:

- **`bus_manager.h:177`** — `Bus::hasRGB()` references `TYPE_WS2812_1CH`
  and `TYPE_WS2812_WWA`. Since the `TYPE_*` defines stay in `const.h` (just
  the *display* in the dropdown is hidden), this code still compiles.
- **`cfg.cpp:234`** — uses `TYPE_WS2812_RGB` as the JSON-default LED type
  for missing config entries. Harmless: a fresh install will pick the
  patched `DEFAULT_LED_TYPE` instead.
- **`bus_manager.cpp:130`** — `bc.type == TYPE_TM1814` comparison in
  `BusDigital`'s constructor for refresh-required handling. Always evaluates
  false on a GW6205-only build, which is correct.
- **`bus_manager.cpp:478+`** — analog (PWM) bus code referencing
  `TYPE_ANALOG_*`. Lives in `BusPwm`, not `BusDigital`, and stays
  functional.
- **`platformio.ini`** — the upstream WLED config is untouched. The
  WledGW6205 project has its own `platformio.ini` that uses
  `src_dir = ../wled/wled00`.

This minimizes merge friction — only 3 files need attention during an
upstream sync, and 2 of them have tiny diffs.

---

## Re-applying after an upstream pull

```bash
cd ../wled
git checkout wled00/const.h wled00/bus_wrapper.h wled00/bus_manager.cpp
git pull
git apply ../WledGW6205/patches/wled-gw6205.patch
```

If `git apply` fails, the most likely culprit is `bus_wrapper.h` (upstream
adds new LED chipsets there frequently). The fix is mechanical: each new
case needs to land **inside** the `#ifndef WLED_GW6205_ONLY` block.

After resolving conflicts, regenerate the patch file:

```bash
cd ../wled
git diff wled00/const.h wled00/bus_wrapper.h wled00/bus_manager.cpp \
    > ../WledGW6205/patches/wled-gw6205.patch
```
