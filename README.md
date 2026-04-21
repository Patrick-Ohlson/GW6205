# WledGW6205

A strip-down WLED fork targeting the **GW6205 12-bit addressable LED chipset**
on ESP32. All other LED types are compiled out for flash savings.

This project sits alongside the vendored `../wled/` tree and builds against
its source files — we do **not** maintain a separate copy of WLED. A small
patch file (`patches/wled-gw6205.patch`) records the in-tree edits that plug
the GW6205 driver into WLED's `PolyBus` dispatcher.

## Project layout

```
WledGW6205/
├── platformio.ini              # Project config, references ../wled/wled00
├── README.md                   # This file
├── InstallPatches.bat          # apply patches + refresh NeoESP32RmtHI copy
├── RemovePatches.bat           # revert wled/wled00/ to pristine
├── .vscode/                    # extensions, settings, build/upload tasks
├── lib/
│   ├── Gw6205Bus/              # 12-bit RMT driver (ported from TestGW6205)
│   │   ├── library.json
│   │   ├── include/Gw6205Bus.h
│   │   └── src/Gw6205Bus.cpp
│   └── NeoESP32RmtHI/          # local copy of WLED's high-priority RMT lib
│       ├── library.json        # (vendored from ../wled/lib/NeoESP32RmtHI/)
│       ├── include/NeoEsp32RmtHIMethod.h
│       └── src/{*.cpp,*.S}
└── patches/
    └── wled-gw6205.patch       # git diff vs pristine wled/wled00/
```

`lib/NeoESP32RmtHI/` is a copy of the same-named library inside the
upstream WLED tree. We keep a local copy because PlatformIO's library
auto-discovery is unreliable when `src_dir` lives outside `PROJECT_DIR`,
which is exactly the situation here. `InstallPatches.bat` re-copies it
from `../wled/lib/NeoESP32RmtHI/` after each upstream sync, so the local
copy stays in step with whatever WLED ships.

Only three files in the upstream tree are touched:
- `wled/wled00/const.h` — adds `TYPE_GW6205 = 35`, flips `DEFAULT_LED_TYPE`
  under `WLED_GW6205_ONLY`.
- `wled/wled00/bus_wrapper.h` — adds GW6205 branches to the `PolyBus` switch
  statements (begin, create, show, canShow, setPixelColor, getPixelColor,
  cleanup, getDataSize, memUsage, getI). All non-GW6205 ESP32 cases are
  wrapped in `#ifndef WLED_GW6205_ONLY` so the linker drops the NeoPixelBus
  template instantiations.
- `wled/wled00/bus_manager.cpp` — adds GW6205 to `BusDigital::getLEDTypes()`
  so it appears in the web-UI dropdown; the other entries are gated out.

## Building

### Prerequisites

1. PlatformIO (CLI or VSCode extension)
2. Node.js and npm (needed once to build WLED's web UI)
3. Upstream WLED sources already at `../wled/` with the GW6205 patches applied

### One-time UI build

WLED generates `html_*.h` headers from the `data/` web assets at build time.
This only needs to run once per upstream sync:

```bash
cd ../wled
npm ci
npm run build
cd ../WledGW6205
```

### Build + flash

```bash
pio run                    # compile
pio run -t upload          # flash the connected ESP32
pio device monitor         # open serial monitor at 115200
```

## Hardware wiring

Same as TestGW6205:
- **GPIO 16** → LED strip DIN (default; override in the WLED web UI)
- **24 V** DC PSU → VDD (with dropping resistor per GW6205 datasheet)
- **Common ground** between ESP32 and PSU

## Runtime config

On first boot the device creates a WiFi AP (`WLED-AP`, password
`wled1234`). Connect, open http://4.3.2.1, and go to **Config → LED
Preferences**:

- **LED Type** dropdown: should show **only** "GW6205 12-bit"
- **Length**: number of LEDs in your strip
- **Data pin**: defaults to 16
- **Color Order**: leave on the default **GRB** (see note below)

Save, and the strip should light up with the current effect.

### Why color order = GRB?

Counter-intuitive, but correct. WLED's "color order" field is named
after WS2812's wire convention — internally `col.G` is the slot
clocked out *first on the wire*, `col.R` is second, `col.B` is third.
The default GRB mode means "no swap relative to WS2812 layout", which
is exactly what `Gw6205Bus` expects: it consumes `(col.R, col.G,
col.B)` from `bus_wrapper.h` in that argument order, and the GW6205
chip's first 12-bit slot is its red phosphor. With WLED set to GRB the
user's red ends up in `col.R`, which is passed first, which lights the
red phosphor. Picking "RGB" in the dropdown would swap red and green
on the wire and show everything inverted.

## How the 12-bit upscaling works

WLED's internal color pipeline is 8-bit-per-channel. The `Gw6205Bus::setPixelColor()`
method expands each 8-bit value to 12-bit via bit replication:

```
val12 = (val8 << 4) | (val8 >> 4)
```

This maps `0x00 → 0x000` and `0xFF → 0xFFF` with evenly distributed
intermediate values. No gamma-aware LUT yet — that's a v0.2 enhancement.

The master brightness slider is applied in the 12-bit domain inside `Show()`:

```
bri12 = (val12 * brightness_8bit + 128) >> 8
```

This keeps fine-grained brightness control without 8-bit quantization at the
output.

## Upstream sync procedure

When WLED releases a new version, follow these steps to rebase our patches:

```bash
# 1. From the wled/ directory, revert our patches
cd ../wled
git checkout wled00/const.h wled00/bus_wrapper.h wled00/bus_manager.cpp

# 2. Pull upstream
git pull origin main   # or whichever branch/tag you track

# 3. Reapply our patch
git apply ../WledGW6205/patches/wled-gw6205.patch

# If the patch applies cleanly, you're done. If there are conflicts,
# git apply will fail and tell you which hunks. Resolve manually, then
# regenerate the patch so it reflects the new upstream baseline:
git diff wled00/const.h wled00/bus_wrapper.h wled00/bus_manager.cpp \
    > ../WledGW6205/patches/wled-gw6205.patch

# 4. Rebuild the web UI (upstream may have changed data/)
npm ci && npm run build

# 5. Back to WledGW6205 and build
cd ../WledGW6205
pio run
```

### What to look out for during rebase

The hunks most likely to conflict are in `bus_wrapper.h` — WLED upstream
regularly adds new LED chipsets, which means new switch-case lines that may
appear adjacent to our `#ifndef WLED_GW6205_ONLY` markers. The fix is always
mechanical: make sure the new cases land **inside** the gated block.

The `const.h` and `bus_manager.cpp` changes are much smaller and rarely
conflict.

## Troubleshooting

### `pio run` keeps re-downloading packages and never terminates

If the project lives inside a Dropbox / OneDrive / Google Drive synced
folder (which this one does), the cloud sync agent rewrites file
timestamps in `.pio/libdeps/` and `.pio/build/` while PIO is working,
making PIO think the cache is dirty and triggering a fresh
package fetch on every invocation.

`platformio.ini` already pushes the workspace outside the synced tree
via:

```ini
workspace_dir = ${sysenv.LOCALAPPDATA}/pio_wled_gw6205
```

This puts all build artifacts under `C:\Users\<you>\AppData\Local\pio_wled_gw6205\`
which Dropbox does not touch. If you previously ran a build with the
default workspace, delete the leftover `.pio/` folder once and the loop
will stop.

### `fatal error: NeoEsp32RmtHIMethod.h: No such file or directory`

This means PlatformIO's library finder didn't pick up
`lib/NeoESP32RmtHI/`. Two things to check:

1. **The directory exists**: `lib\NeoESP32RmtHI\library.json`,
   `lib\NeoESP32RmtHI\include\NeoEsp32RmtHIMethod.h`,
   `lib\NeoESP32RmtHI\src\NeoEsp32RmtHIMethod.cpp`,
   `lib\NeoESP32RmtHI\src\NeoEsp32RmtHI.S`. If any are missing, run
   `InstallPatches.bat` to refresh from upstream.
2. **`lib_compat_mode = soft`** is set in `[env:wled_gw6205_esp32]`.
   The `strict` default rejects libraries that don't declare a
   `frameworks` field in their `library.json`, which NeoESP32RmtHI
   omits.

### `git apply` conflicts when re-applying after upstream sync

The patch is sensitive to changes around the `bus_wrapper.h` switch
statements. If upstream WLED added new chipsets there, the patch's
context lines won't match. Fix manually:

1. Open `wled\wled00\bus_wrapper.h` and `WledGW6205\patches_wled.md`
   side by side.
2. Re-add the GW6205 cases to each `PolyBus::*()` switch by following
   the change list in `patches_wled.md`.
3. Regenerate the patch:
   ```
   cd ..\wled
   git diff wled00/const.h wled00/bus_wrapper.h wled00/bus_manager.cpp ^
       > ..\WledGW6205\patches\wled-gw6205.patch
   ```

## Known limitations

- **MCU**: ESP32 only. No S3/S2/C3 support yet — the Gw6205Bus driver uses
  the legacy IDF `driver/rmt.h` API which works on plain ESP32 but the
  channel-memory model differs on newer variants.
- **GW6205 speed**: 800 kHz only. 400 kHz variant (`GW6205_400`) not
  implemented.
- **Color order**: defaults to RGB. If your strip looks wrong, we can add
  GRB/BRG support to `Gw6205Bus` easily; it's already wired through the
  constructor.
- **8→12-bit expansion**: simple bit replication. A gamma-aware 12-bit LUT
  would give smoother low-end ramps but costs ~8 KB flash.
- **Web UI build**: run `npm run build` in `../wled/` whenever WLED
  updates. This project does not auto-invoke it.
