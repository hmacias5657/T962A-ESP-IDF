# AGENTS.md — OvenController ESP-IDF

## Project Overview

Adaptive PID Reflow Oven Controller for T962A oven conversion. Ported from Arduino (PlatformIO) to ESP-IDF v5.x native SDK. Hardware: ESP32 WROOM dual-core, 128x64 GLCD KS0108 via U8G2 (8-bit parallel), 2x K-type thermocouples via ADS1015 (I2C), zero-cross SSR heater control, Bresenham PID distribution. Line frequency (50Hz/60Hz) auto-detected at startup via ZC interrupt timing.

## Architecture

### Dual-Core FreeRTOS
- **Core 0 (controlTask)**: Reflow control loop at ~2.13–2.56s period (auto-detected from line frequency), priority 3, stack 4096
- **Core 1 (uiTask)**: UI refresh at ~30ms period, priority 1, stack 8192. Full menu navigation state machine: main menu (6 items), recipe list/edit, settings, bake setup, calibration (submenu with 3 items: TC offset, profile cal test, view cal info), running display with plot toggle, e-stop acknowledge
- Inter-task communication via `xQueueOverwrite` (1-slot queue) for telemetry
- I2C bus access serialized via `SemaphoreHandle_t` mutex

### Module Map

| Module | Files | Responsibility |
|--------|-------|----------------|
| Config | `Config.h` | Pin mappings, constants, limits, firmware version |
| SharedData | `SharedData.h` | Structs/enums: ReflowRecipe, PidGains, TemperatureData, ThermalTelemetry, SystemSettings |
| BresenhamPID | `BresenhamPID.h/.cpp` | ZC ISR (IRAM_ATTR) with frequency auto-measure (20-sample average), dual Bresenham channels (heater + cooling fan SSR), PID compute. Exposes `getHalfCycleUs()`, `getLineFrequency()`, `getPidIntervalMs()`, `measureLineFrequency()` |
| TemperatureReader | `TemperatureReader.h/.cpp` | ADS1015 I2C reads, calibration offsets, fault detection |
| ADS1015_Driver | `ADS1015_Driver.h/.cpp` | Direct I2C register access (replaces Adafruit library) |
| ProfileEngine | `ProfileEngine.h/.cpp` | 5-stage linear interpolation, bake mode setpoint generation |
| AITuner | `AITuner.h/.cpp` | Spatial damping + learning heuristic for gain scheduling |
| DisplayRenderer | `DisplayRenderer.h/.cpp` | KS0108 via U8G2 (full frame buffer), full menu system: main menu, recipe list/edit, running display with progress bars + mini graph, live plot, settings, bake setup, calibration submenu, calibration test screens (running with live temp graph + results), calibration info viewer (3-page: PID gains / cal data / recipe temps), e-stop. Built-in °C/°F unit conversion helpers (`toDisplayUnit`, `toDisplayRate`, `unitSuffix`, `rateSuffix`). Splash shows firmware version + detected line frequency |
| ButtonDebouncer | `ButtonDebouncer.h/.cpp` | 50ms non-blocking debounce, 5 T962A keys + E-STOP |
| Buzzer | `Buzzer.h/.cpp` | 6 buzzer pattern generator |
| Main | `main.cpp` | `app_main()` entry, NVS init, I2C bus init, ZC frequency measurement + NVS persist, splash screen, task creation. Navigation state (s_menuCursor, s_recipeCursor, s_recipeField, s_showPlot, etc.) drives uiTask menu machine |
| u8g2 (component) | `components/u8g2/` | U8G2 v2.36.19 library built as ESP-IDF component. Provides `u8x8_esp32.c` with generic GPIO callback (`u8x8_gpio_and_delay_esp32`) and byte function placeholder. KS0108 uses `u8x8_byte_ks0108` (U8G2 built-in) for the 8-bit parallel protocol. Define `U8X8_USE_PINS` before including `u8g2.h` |

### ESP-IDF Replacements (vs Arduino)
| Arduino API | ESP-IDF Replacement |
|-------------|-------------------|
| `Arduino.h` | `esp_log.h`, `freertos/FreeRTOS.h`, `driver/gpio.h`, `driver/ledc.h`, `driver/i2c_master.h`, `nvs_flash.h` |
| `pinMode`/`digitalWrite`/`digitalRead` | `gpio_set_direction`/`gpio_set_level`/`gpio_get_level` |
| `attachInterrupt` | `gpio_install_isr_service` + `gpio_isr_handler_add` |
| `millis()` | `esp_timer_get_time() / 1000` |
| `delay(ms)` | `vTaskDelay(pdMS_TO_TICKS(ms))` |
| `Serial.printf` | `ESP_LOGI` |
| `ledcSetup`/`ledcAttachPin` | `ledc_timer_config` + `ledc_channel_config` |
| `Preferences` | `nvs_open`/`nvs_get`/`nvs_set`/`nvs_commit`/`nvs_close` |
| `setup()`+`loop()` | `app_main()` |
| `Adafruit_ADS1X15` | Custom `ADS1015_Driver` using `i2c_master_transmit/receive` |
| U8G2 SPI constructor with pins | `u8g2_Setup_ks0108_128x64_f` + `u8x8_byte_ks0108` callback + `u8x8_SetPin` for all 12 parallel pins |

## Startup Flow

1. `nvs_flash_init()` — initialize NVS storage
2. I2C bus init (ADS1015)
3. GPIO init via `g_pid.init()` — enables ZC ISR (measurement begins automatically on first ZC edge)
5. `loadSettings()` — loads NVS params including previous `zcHalfCycUs`
6. `g_pid.measureLineFrequency()` — blocks until 20 ZC samples collected (~160–200ms) with 2s timeout fallback to default (50Hz)
7. Measured half-cycle compared to stored NVS value (50µs hysteresis); saved if changed
8. Tasks created with dynamic `getPidIntervalMs()` instead of hardcoded constant
9. Display initialized (`g_display.init()` configures U8G2 with `u8x8_byte_ks0108` + `u8x8_gpio_and_delay_esp32` callbacks)
10. Splash screen drawn: shows firmware version (from `Config.h`) and detected line frequency (from `BresenhamPID::getLineFrequency()`)

## Build System

- ESP-IDF CMake build system
- Custom partition table (`partitions.csv`) with OTA and NVS
- `main/CMakeLists.txt` registers all source files
- Requires `driver`, `nvs_flash`, `esp_timer` component dependencies

## Build Commands

```bash
idf.py set-target esp32
idf.py menuconfig    # CPU 240MHz, dual-core, custom partitions
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

## Firmware Versioning

A post-build script `rename_firmware.py` automatically versions the output binary:

```bash
python3 rename_firmware.py
```

Reads `FIRMWARE_VERSION_*` defines from `Config.h` and creates:
- `OvenController_v{major}.{minor}.{patch}_{timestamp}.bin` — timestamped archive
- `OvenController_v{major}.{minor}.{patch}.bin` — latest version copy

Run after `idf.py build` or integrate as a post-build step in your IDE/task runner.

## NVS Keys

All keys match Arduino version exactly:
`r%d_name`, `r%d_pre`, `r%d_soak`, `r%d_peak`, `r%d_ramp`, `r%d_refl`, `r%d_hold`, `r%d_z%d_kp/ki/kd`, `r%d_z%d_ckp/cki/ckd`, `recipeCount`, `maxBakeMin`, `useF`, `tc1Off`, `tc2Off`

Additional ESP-IDF keys:
`zcHalfCycUs` — measured zero-cross half-cycle period (µs), used for 50Hz/60Hz auto-detection
`z%d_rate` — per-zone max heating rate (int32 ×1000)
`z%d_crate` — per-zone max cooling rate (int32 ×1000)
`heatDTms` — heat dead time in milliseconds (uint32)
`coolDTms` — cool dead time in milliseconds (uint32)

Data stored as: strings (names), int32 scaled by 100 (temps) or 1000 (gains), u8 (booleans).

## Safety Features

- Over-temp protection at 280°C
- Sensor fault detection (<5°C reading)
- Spatial delta detection (>45°C between TC1 and TC2)
- Hardware E-STOP (dedicated GPIO)
- Emergency stop disables both SSR outputs immediately

## Files

| File | Purpose |
|------|---------|
| `rename_firmware.py` | Post-build script; versions `.bin` output with semver + timestamp |
| `CHANGELOG.md` | Release history per semantic version |
| `AGENTS.md` | Architecture guide for LLM agents and developers |
| `README.md` | Project overview, setup, build + flash instructions |
| `user_manual.md` | End-user operation guide |
| `components/u8g2/` | U8G2 v2.36.19 display library + `u8x8_esp32.c` GPIO callbacks |

## Conventions

- C++ with `extern "C"` for `app_main()`
- ISR functions marked `IRAM_ATTR`
- No dynamic memory allocation after startup
- All I2C operations protected by mutex
- Logging via `ESP_LOGI(TAG, ...)` with `#define TAG "OvenCtrl"`
- Frequency measurement resets scan state before ISR is enabled; 2s timeout prevents hang if no mains present
- ISR-only measurement uses `esp_timer_get_time()` (IRAM-safe) and minimal integer math; measurement flag set as last action
- Measured half-cycle persisted to NVS only when delta > 50µs to reduce write wear
- U8G2: define `U8X8_USE_PINS` before `#include "u8g2.h"` to enable `u8x8_SetPin`/`u8x8_GetPinValue` macros
- U8G2 callbacks (`u8x8_byte_ks0108`, `u8x8_gpio_and_delay_esp32`) are C functions; declare with `extern "C"` when called from C++
- KS0108 uses parallel 8-bit protocol (6800-style); no SPI bus needed. LCD pins are driven directly via GPIO.
