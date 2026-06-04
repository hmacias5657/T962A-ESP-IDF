# T962A-ESP-IDF — Adaptive PID Reflow Oven (ESP-IDF)

ESP32-based reflow oven controller for T962A conversions. Ported from Arduino PlatformIO to ESP-IDF v5.x native SDK.
**Beware, this code has not been tested on hardware yet**
> **Repo**: [github.com/hmacias5657/T962A-ESP-IDF](https://github.com/hmacias5657/T962A-ESP-IDF)  
> **Project root**: `T962A_OvenController/` (run `idf.py` commands from this directory)

## Hardware

- **MCU**: ESP32 WROOM (dual-core, 240MHz)
- **Display**: 128x64 GLCD KS0108 (8-bit parallel) via U8G2
- **ADC**: ADS1015 12-bit I2C (2x K-type thermocouples using AD8495 as amplifiers with cold junction compensation)
- **Heater**: Zero-cross SSR with Bresenham PID (256 half-cycles)
- **Cooling**: SSR + PWM system fan (25kHz MOSFET)
- **Input**: T962A 5-key membrane (F1-UP, F2-DOWN, F3-LEFT, F4-RIGHT, S-START)
- **Safety**: Dedicated hardware E-STOP, over-temp, sensor fault, spatial delta

## Features

- Dual-core FreeRTOS: Core 0 (control ~2.13–2.56s), Core 1 (UI ~30ms)
- Auto-detected line frequency at startup (50Hz or 60Hz) — eliminates hardcoded regional assumption
- Bresenham PID power distribution with feedforward control — PID + feedforward from profile ramp rate, capped at 80%
- 5 independent PID gain sets per recipe (both heater and cooling), with stage-based scheduling (PREHEAT/SOAK/REFLOW/COOLDOWN)
- Per-profile adaptive fine-tuning — gains adjusted ±5% after each zone based on overshoot, steady error, oscillation, settling time. Sequence-numbered precedence resolves calibration test vs per-profile tuning
- Ziegler-Nichols base gains computed from deadtime + per-zone heating rates during calibration test
- EMA temperature filtering (alpha=0.15) on both thermocouples with filtered derivative in PID
- ADS1015 12-bit external ADC with ±10°C per-sensor calibration (0.5°C steps)
- 8-minute live temperature plot (280°C Y-axis, 128x64 GLCD)
- Profile creation wizard (8 parameters: temps, timings, fine-tune enable toggle)
- Bake/drying mode (25–280°C, 1–999 min)
- °C/°F unit toggle (all temperatures auto-convert across running display, plot, recipe editor, bake setup, calibration info)
- Calibration info screen (3-page viewer: PID gains, calibration rates/dead times, recipe temperature setpoints)
- Calibration test with real-time temperature-vs-time graph (phase, zone, power, current/max rate)
- Integral management: reset on zone transition, Ki suppression near peak
- Cooldown: heater forced off during cooldown stage
- NVS persistence: 10 recipes, per-zone gains, settings, calibration, sequence tracking
- Post-build auto-versioning: `rename_firmware.py` runs automatically via CMake POST_BUILD
- Safety: over-temp (280°C), sensor fault (<5°C), spatial delta (>45°C), HW E-STOP
- Buzzer patterns for transitions, completion, errors, E-STOP

## Project Structure

```
T962A_OvenController/
├── CMakeLists.txt              # Top-level CMake (project OvenController)
├── sdkconfig                   # idf.py menuconfig output (gitignored)
├── partitions.csv              # Custom partition table (NVS + OTA)
├── rename_firmware.py          # Post-build firmware versioning script
├── CHANGELOG.md                # Release history
├── AGENTS.md                   # Architecture guide for AI agents
├── README.md                   # This file
├── user_manual.md              # End-user operation guide
├── main/
│   ├── CMakeLists.txt          # Component registration
│   ├── Config.h                # Pin mappings, constants, limits, firmware version
│   ├── SharedData.h            # Structs, enums, recipes
│   ├── main.cpp                # app_main(), task creation
│   ├── BresenhamPID.h/.cpp     # ZC ISR, dual Bresenham, PID with feedforward + filtered derivative
│   ├── TemperatureReader.h/.cpp# ADS1015 I2C reads, EMA filter, calibration offsets
│   ├── ProfileEngine.h/.cpp    # 5-stage linear interpolation + ramp rate for feedforward
│   ├── AITuner.h/.cpp          # Stage-based gain scheduling, ZoneMetrics, per-zone fine-tuning
│   ├── DisplayRenderer.h/.cpp  # KS0108 U8G2 menu machine + plot (8-field recipe editor)
│   ├── ButtonDebouncer.h/.cpp  # 50ms debounce, 6 keys
│   ├── Buzzer.h/.cpp           # 6 buzzer patterns
│   └── ADS1015_Driver.h/.cpp   # Direct I2C ADS1015 (no Adafruit)
└── components/
    └── u8g2/                   # U8G2 v2.36.19 display library + ESP-IDF GPIO callbacks
```

## Getting Started

### Prerequisites
- ESP-IDF v5.x (install via VSCode extension or `git clone`)
- ESP32 target

### Build & Flash
All commands run from the `T962A_OvenController/` directory:
```bash
cd T962A_OvenController
idf.py set-target esp32
idf.py menuconfig
# Settings: CPU freq 240MHz, dual-core, custom partition table
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

### Firmware Versioning (Auto Post-Build)
The `rename_firmware.py` script runs **automatically** as a CMake POST_BUILD step after every `idf.py build`. It reads the version from `Config.h` and creates:
- `OvenController_v1.11.0_20260603_120000.bin` — unique timestamped archive
- `OvenController_v1.11.0.bin` — latest version shortcut

### First Run
1. Flash firmware to ESP32
2. Connect GLCD, ADS1015, SSR, buttons per `Config.h` pins
3. Power on — splash screen appears with version and line frequency
4. Use **F1 (UP) / F2 (DOWN)** to navigate the menu, **S (SELECT)** to enter
5. Create or select a reflow recipe via menu
6. Press **S** to begin reflow cycle
7. Toggle between running display and live plot with **F3 (LEFT) / F4 (RIGHT)**

## Configuration

Edit `main/Config.h` for pin mappings and constants. Key NVS settings autoload:
- Calibration offsets (TC1/TC2)
- °C/°F preference
- Max bake duration
- Up to 10 saved recipes with per-zone PID gains

## Versioning

Firmware version is defined in `main/Config.h`:
```c
#define FIRMWARE_VERSION_MAJOR 1
#define FIRMWARE_VERSION_MINOR 11
#define FIRMWARE_VERSION_PATCH 0
```
The `rename_firmware.py` script runs automatically via CMake POST_BUILD after each `idf.py build`.

## Porting Notes

This is a from-scratch port of my PlatformIO ESP32 reflow controller to ESP-IDF. All Arduino dependencies replaced:
- `Adafruit_ADS1X15` → custom `ADS1015_Driver` via `i2c_master`
- `U8G2` Arduino constructor → ESP-IDF pin setup via `u8x8_SetPin` (KS0108 parallel byte function)
- `Preferences` → `nvs_flash` direct API
- `Arduino.h` → native ESP-IDF headers

## License

Source available at [github.com/hmacias5657/T962A-ESP-IDF](https://github.com/hmacias5657/T962A-ESP-IDF). No license specified.
