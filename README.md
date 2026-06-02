# OvenController — Adaptive PID Reflow Oven (ESP-IDF)

ESP32-based reflow oven controller for T962A conversions. Ported from Arduino PlatformIO to ESP-IDF v5.x native SDK.
**Beware, Not tested on hardware yet**

## Hardware

- **MCU**: ESP32 WROOM (dual-core, 240MHz)
- **Display**: 128x64 GLCD KS0108 (8-bit parallel) via U8G2
- **ADC**: ADS1015 12-bit I2C (2x K-type thermocouples using AD8495 as amplifiers))
- **Heater**: Zero-cross SSR with Bresenham PID (256 half-cycles)
- **Cooling**: SSR + PWM system fan (25kHz MOSFET)
- **Input**: T962A 5-key membrane (F1-UP, F2-DOWN, F3-LEFT, F4-RIGHT, S-START)
- **Safety**: Dedicated hardware E-STOP, over-temp, sensor fault, spatial delta

## Features

- Dual-core FreeRTOS: Core 0 (control ~2.13–2.56s), Core 1 (UI ~30ms)
- Auto-detected line frequency at startup (50Hz or 60Hz) — eliminates hardcoded regional assumption
- Bresenham PID power distribution — heater + cooling fan SSR
- 5 independent PID gain sets per recipe (both heater and cooling)
- ADS1015 12-bit external ADC with ±10°C per-sensor calibration (0.5°C steps)
- 8-minute live temperature plot (280°C Y-axis, 128x64 GLCD)
- Profile creation wizard (7 parameters: preheat/soak/peak temps, ramp/soak/reflow/hold times)
- Bake/drying mode (25–280°C, 1–999 min)
- °C/°F unit toggle (all temperatures auto-convert across running display, plot, recipe editor, bake setup, calibration info)
- Calibration info screen (3-page viewer: PID gains, calibration rates/dead times, recipe temperature setpoints)
- Calibration test with real-time temperature-vs-time graph (phase, zone, power, current/max rate)
- AITuner gain scheduler (spatial damping + learning heuristic)
- NVS persistence: 10 recipes, per-zone gains, settings, calibration
- Safety: over-temp (280°C), sensor fault (<5°C), spatial delta (>45°C), HW E-STOP
- Buzzer patterns for transitions, completion, errors, E-STOP

## Project Structure

```
OvenController/
├── CMakeLists.txt              # Top-level CMake
├── sdkconfig                   # idf.py menuconfig output
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
│   ├── BresenhamPID.h/.cpp     # ZC ISR, dual Bresenham, PID compute
│   ├── TemperatureReader.h/.cpp# ADS1015 I2C reads, calibration
│   ├── ProfileEngine.h/.cpp    # 5-stage linear interpolation
│   ├── AITuner.h/.cpp          # Gain scheduler + learning heuristic
│   ├── DisplayRenderer.h/.cpp  # KS0108 U8G2 menu machine + plot
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
```bash
idf.py set-target esp32
idf.py menuconfig
# Settings: CPU freq 240MHz, dual-core, custom partition table
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

### Firmware Versioning (Post-Build)
```bash
python3 rename_firmware.py
```
Appends version number (from `Config.h`) and build timestamp to the output `.bin`:
- `OvenController_v1.9.0_20260531_185000.bin` — unique timestamped archive
- `OvenController_v1.9.0.bin` — latest version shortcut

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
#define FIRMWARE_VERSION_MINOR 9
#define FIRMWARE_VERSION_PATCH 0
```
Run `python3 rename_firmware.py` after each build to generate versioned `.bin` files.

## Porting Notes

This is a from-scratch port of an Arduino reflow controller to ESP-IDF. All Arduino dependencies replaced:
- `Adafruit_ADS1X15` → custom `ADS1015_Driver` via `i2c_master`
- `U8G2` Arduino constructor → ESP-IDF pin setup via `u8x8_SetPin` (KS0108 parallel byte function)
- `Preferences` → `nvs_flash` direct API
- `Arduino.h` → native ESP-IDF headers

## License
MIT
