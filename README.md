# Inkling

Firmware for the **Xteink X3** e-paper reader: a reader with a virtual pet at its
heart. Built from scratch on the [FreeInk SDK](https://github.com/Free-Ink/freeink-sdk)
(MIT), which supplies the hardware drivers, the UI layer and the EPUB engine.

## Status

| Phase | Content | State |
|---|---|---|
| 0 | Skeleton: boot, X3 detection, display, buttons, SD, RTC, IMU, battery, power-off, simulator | done (not yet tested on a device) |
| 1 | Reader: library, EPUB/TXT via FreeInkBook, saved progress, reading stats | — |
| 2 | Pet engine: pure logic, host tests | — |
| 3 | Full experience: home and sleep screen with the pet, IMU gestures | — |
| 4 | Extras: games, flashcards, Wi-Fi | — |

## Layout

```
src/main.cpp          firmware entry: hardware bring-up and main loop
lib/InklingCore/      portable code (screens, logic) — no Arduino, builds on the host too
sim/                  host simulator: renders the screens to PNG
freeink-sdk/          FreeInk SDK, git submodule pinned to a tested commit
partitions.csv        OTA layout shared with stock/CrossPoint firmware
```

## Build

```bash
git submodule update --init --recursive
pio run -e x3                  # firmware
pio run -e x3 -t upload        # flash over USB-C
pio device monitor             # serial log
```

Simulator (macOS/Linux, needs CMake and a C++17 compiler):

```bash
cmake -S sim -B build/sim && cmake --build build/sim
build/sim/inkling-sim sim/out  # writes sim/out/*.png
```

## X3 hardware notes

- ESP32-C3, ~320 KB usable RAM, **no PSRAM**; 16 MB flash.
- 792×528 panel, UC8253 on early units and UC8279d on newer batches; detected at boot.
- 7 buttons: Back, Confirm, Left, Right, Up, Down on two ADC ladders (GPIO1/GPIO2) plus Power.
- DS3231 RTC, QMI8658 IMU and BQ27220 battery gauge on I²C (SDA 20 / SCL 0).
- MicroSD shares the display SPI bus (MISO 7, CS 12).
- Power-off drives GPIO13 low, which cuts the battery: the device cannot wake
  itself on a timer while on battery, so anything time-based (the pet) catches
  up from the RTC at the next power-on.
