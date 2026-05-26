# Build & install guide (for Claude / LLM agents)

This file is intended for AI coding assistants (Claude Code, etc.) helping a
user build and install firmware from this fork. Humans can read it too, but
the structure assumes an agent that will execute the commands rather than
read prose.

## What this firmware fork adds

vs. the upstream [justcallmekoko/ESP32Marauder](https://github.com/justcallmekoko/ESP32Marauder):

- **DLT-192 PPI pcap output with per-frame GPS tagging** — every captured
  802.11 frame carries a PPI Geolocation field (type 30002) with lat/lon/alt
  from the attached GPS module.
- **Radiotap header inside the PPI capsule** — per-frame RSSI
  (`radiotap.dbm_antsignal`) and channel/frequency (`radiotap.channel.freq`),
  making triangulation possible from a single capture.
- **`-g` / `--gps` CLI flag** on any `sniff*` command — opt-in per-capture.
- **"Sniff + GPS" device-side menu** on boards with onboard screens.
- 38400 baud GPS probe + altitude encoding fix (cherry-picked upstream PRs).
- 64-bit-overflow-safe pcap timestamps (no rollover after ~71 min uptime).

See the top of [README.md](README.md) for the longer human-facing version.

## Companion app

The matching Flipper companion app (with channel-lock / channel-hop menu
entries pre-wired with `-g`) lives at
[willc0de4food/Momentum-Apps](https://github.com/willc0de4food/Momentum-Apps),
in the `wifi_marauder_companion/` subdirectory.

---

# Build instructions

## Target

The primary supported board for this fork is the **Flipper Zero WiFi Dev
Board** (ESP32-S2, flag `MARAUDER_FLIPPER`). Other upstream targets compile
but aren't actively tested here. To find the FQBN / IDF version / flag for
any other board, check the matrix in
[`.github/workflows/build_parallel.yml`](.github/workflows/build_parallel.yml).

## Prerequisites

1. **`arduino-cli`** (any recent version, ≥0.30). On Arch: `sudo pacman -S arduino-cli`.
   On macOS: `brew install arduino-cli`. On Debian/Ubuntu: download the
   prebuilt tarball from the arduino-cli releases page.

2. **ESP32 core 2.0.11** (specifically — newer cores break the build):
   ```bash
   arduino-cli core update-index
   arduino-cli core install esp32:esp32@2.0.11
   ```

3. **`-zmuldefs` linker patch** applied to platform.txt. The upstream
   workflow does this; it has to be done locally too. Run once after
   installing the ESP32 core:
   ```bash
   for f in $(find ~/.arduino15/packages/esp32/hardware/esp32/ -name platform.txt); do
       sed -i 's/compiler.c.elf.libs.esp32s2=/compiler.c.elf.libs.esp32s2=-zmuldefs /' "$f"
       sed -i 's/compiler.c.elf.libs.esp32s3=/compiler.c.elf.libs.esp32s3=-zmuldefs /' "$f"
       sed -i 's/compiler.c.elf.libs.esp32c3=/compiler.c.elf.libs.esp32c3=-zmuldefs /' "$f"
       sed -i 's/compiler.c.elf.libs.esp32=/compiler.c.elf.libs.esp32=-zmuldefs /' "$f"
   done
   ```
   (If the user is on macOS, `sed -i` needs an empty string arg: `sed -i '' ...`.)

4. **Library dependencies** — checked out at specific tags (newer versions
   break the build). Create a flat directory the build will use as
   `--libraries`. Each library is a separate sub-directory:
   ```bash
   mkdir -p ~/marauder-build/libs
   cd ~/marauder-build/libs

   git clone --depth 1 --branch 1.6      https://github.com/marian-craciunescu/ESP32Ping.git
   git clone --depth 1 --branch v3.4.8   https://github.com/ESP32Async/AsyncTCP.git
   git clone --depth 1 --branch v2.0.6   https://github.com/stevemarple/MicroNMEA.git
   git clone --depth 1 --branch v3.8.1   https://github.com/ESP32Async/ESPAsyncWebServer.git
   git clone --depth 1 --branch 1.3.8    https://github.com/h2zero/NimBLE-Arduino.git
   git clone --depth 1                   https://github.com/adafruit/Adafruit_NeoPixel.git
   ```

   For TFT-equipped boards (NOT needed for Flipper Dev Board), also add:
   ```bash
   git clone --depth 1 --branch V2.5.34  https://github.com/Bodmer/TFT_eSPI.git
   git clone --depth 1 --branch v1.4     https://github.com/PaulStoffregen/XPT2046_Touchscreen.git
   git clone --depth 1 --branch 3.0.0    https://github.com/lvgl/lv_arduino.git
   git clone --depth 1 --branch 1.8.0    https://github.com/Bodmer/JPEGDecoder.git
   ```

## Build

From the repo root (this directory):

```bash
arduino-cli compile \
    --fqbn "esp32:esp32:esp32s2:PartitionScheme=min_spiffs,FlashSize=4M,PSRAM=enabled" \
    --warnings none \
    --build-property "compiler.cpp.extra_flags=-DMARAUDER_FLIPPER" \
    --libraries ~/marauder-build/libs \
    esp32_marauder/
```

Successful build prints `Sketch uses N bytes ...` and `Global variables use N bytes ...`.

The compiled binary lands at:
```
~/.cache/arduino/sketches/<hash>/esp32_marauder.ino.bin
```
where `<hash>` is the arduino-cli build hash for this sketch path. To find
it programmatically:
```bash
find ~/.cache/arduino/sketches/ -name esp32_marauder.ino.bin -newer /tmp/marker 2>/dev/null
```
(Or just `find ~/.cache/arduino/sketches/ -name esp32_marauder.ino.bin` and
sort by mtime.)

Copy it somewhere convenient:
```bash
cp ~/.cache/arduino/sketches/*/esp32_marauder.ino.bin ~/marauder_flipper.bin
```

## Install on the Flipper Dev Board

The Flipper has an ESP flasher app that can install `.bin` files onto the
attached WiFi Dev Board over UART:

1. Drop `marauder_flipper.bin` onto the Flipper's SD card (e.g. via qFlipper
   into `/ext/apps_data/esp_flasher/` or any convenient folder).
2. On the Flipper: open the **ESP Flasher** app (under `Apps → GPIO`). If
   it's not installed, get it from
   [0xchocolate/flipperzero-esp-flasher](https://github.com/0xchocolate/flipperzero-esp-flasher)
   or your Flipper firmware's app catalog.
3. In the ESP Flasher: select the `.bin`, flash address **0x1000**, target
   ESP32-S2. Hit "Flash."
4. Reboot the Dev Board (power-cycle by removing/replacing GPIO power, or
   use the Flipper companion app's reset).

If you don't have a Flipper, you can also flash via `esptool` over USB-C
directly to the Dev Board:
```bash
pip install esptool
esptool.py --chip esp32s2 --port /dev/ttyACM0 --baud 921600 \
    write_flash 0x1000 marauder_flipper.bin
```
(Adjust `--port` for your OS: `COM*` on Windows, `/dev/tty.usbmodem*` on macOS.)

# Troubleshooting

- **`error: multiple definition of ...`** — the `-zmuldefs` patch didn't
  apply. Re-run the platform.txt sed loop. Verify with
  `grep zmuldefs ~/.arduino15/packages/esp32/hardware/esp32/*/platform.txt`.
- **`fatal error: WiFi.h: No such file or directory`** — wrong ESP32 core
  version. Confirm with `arduino-cli core list`; should show
  `esp32:esp32 2.0.11`. If a newer version was installed, downgrade with
  `arduino-cli core install esp32:esp32@2.0.11`.
- **`fatal error: NimBLEDevice.h: No such file or directory`** (or similar
  for any other library) — library version mismatch or missing. Re-clone
  the library at the exact tag listed in the Prerequisites section. If the
  user has the libraries somewhere other than `~/marauder-build/libs`,
  point `--libraries` at the right path.
- **Build succeeds but device won't boot / boot-loops** — verify the
  flash address is `0x1000` and the board target matches
  `MARAUDER_FLIPPER` (different boards use different addresses; see the
  workflow matrix).
- **`error: -fno-rtti...` or other `-flto` issues** — the upstream
  workflow uses ESP32 core 2.0.11 specifically. Newer cores changed the
  default flags and break the build. Pin to 2.0.11.

# For Claude agents specifically

If a user asks "build the firmware for me" and they're on the Flipper Dev
Board target, the canonical sequence is:

1. Verify arduino-cli is installed + ESP32 core 2.0.11 is present.
2. Verify the `-zmuldefs` patch is applied (grep for it in platform.txt).
3. Verify libraries are checked out at the listed tags. If not, clone them.
4. Run the build command (the `arduino-cli compile` line above).
5. Copy the `.bin` somewhere the user can access (their home dir is fine).
6. Tell them where it is and how to flash it.

Don't try to flash for them unless they explicitly authorize it (flashing
can brick the board if the wrong target or address is used). Always confirm
the target board before flashing.

If the user is on a board OTHER than the Flipper Dev Board, look up their
target's row in `.github/workflows/build_parallel.yml` for the right
`fbqn`, `flag`, and `idf_ver`. Don't assume MARAUDER_FLIPPER.
