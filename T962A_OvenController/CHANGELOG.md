# Changelog — OvenController

All notable changes to this project will be documented in this file.

## [1.10.0] — 2026-06-01

### Added
- **Unit conversion system**: All temperature displays throughout the UI (running display, live plot, recipe editor, bake setup, calibration test, calibration info) automatically convert °C ↔ °F based on the user's selection in **Settings → Units**. Internal calculations remain in °C; display values and unit suffixes (`C`/`F`, `C/s`/`F/s`) update in real time. Added `toDisplayUnit()`, `toDisplayRate()`, `unitSuffix()`, `rateSuffix()` helpers to `DisplayRenderer`.
- **Live temperature graph during calibration test**: The profile calibration test screen now records temperature history and renders a real-time temperature-vs-time graph (24px tall, 0–280°C/536°F Y-axis) alongside phase, zone, power, current temperature, current/max rates, and elapsed time.
- **3-page "View Cal Info" screen** under Calibration submenu:
  - Page 0: Per-zone heater/cooling PID gains (Kp/Ki/Kd) for each recipe, scrollable via UP/DOWN
  - Page 1: Calibration test results — per-zone heating and cooling rates + heat/cool dead times
  - Page 2: Recipe temperature setpoints (preheat/soak/peak) and timing parameters, all in user-selected units
- `MENU_CALIB_INFO` menu screen enum added to SharedData.h

### Changed
- `DisplayRenderer` now has `_useFahrenheit` member set each UI cycle from `g_settings.useFahrenheit`
- `renderCalibTestRunning()` signature extended with `history` and `historyCount` parameters for live graph
- Calibration submenu extended to 3 items (TC Offset Info, Profile Cal Test, View Cal Info)
- `g_tempHistory` now populated during `STATE_CALIB_TEST` in `controlTask()`
- Recipe editor, bake setup, and calibration test running display now show temperatures in user-selected units

## [1.9.0] — 2026-05-31

### Changed
- Display driver migrated from SSD1306 (SPI OLED) to KS0108 (8-bit parallel GLCD) — matching T962A stock hardware
- Pinout redesigned: 12 pins allocated for LCD parallel bus (D0–D7, RS, E, CS1, CS2)
- Freed GPIO 18/23 from display SPI; resolved conflict with buzzer (GPIO 18) and I2C SCL (GPIO 23)
- All 5 front buttons moved to input-only pins (GPIO 34,36–39) to free output-capable pins for LCD bus
- Fan PWM moved to GPIO 0 (requires 10k external pull-up for safe boot)
- `main.cpp`: removed `spi_bus_initialize` and `driver/spi_master.h` include
- `DisplayRenderer.cpp`: uses `u8g2_Setup_ks0108_128x64_f` with `u8x8_byte_ks0108` instead of SSD1306 SPI setup
- `u8x8_esp32.c`: INIT now configures all 12 KS0108 pins (D0–D7, E, CS, DC, CS1, CS2) as outputs; removed SPI device dependency

### Hardware
- Display: 128x64 KS0108 GLCD (8-bit parallel 6800) via U8G2
- 25 of 26 usable GPIOs consumed; only free pin is GPIO 12 (dangerous/strapping)
- External 10k pull-ups required on GPIO 0, 34, 36–39
- LCD reset via hardware RC circuit (no GPIO)

## [1.8.0] — 2026-05-31

### Added
- Line frequency auto-detection at startup (50Hz/60Hz) via ZC interrupt timing
- U8G2 display driver integration as ESP-IDF component with custom SPI callbacks
- Splash screen shows firmware version + detected line frequency
- NVS persistence of measured ZC half-cycle period (`zcHalfCycUs` key)
- Full menu UI system: main menu (6 items), recipe list with scrolling, recipe editor (7 fields), settings, bake setup, calibration info screen
- Running display: stage name + elapsed time, setpoint/actual temps, heater/fan power bars, TC1/TC2 temps, mini temp bar
- Live plot toggle: full-screen temperature graph with setpoint dashed line
- E-stop screen with acknowledge-to-reset flow
- Menu navigation state machine in uiTask: UP/DOWN cursor, LEFT/RIGHT field edit, SELECT confirm
- Bake mode: creates constant-temp recipe from user-set temp + duration, runs as STATE_BAKING

### Changed
- Firmware version bumped to 1.8.0
- ZC half-cycle period and PID interval now measured dynamically instead of hardcoded
- `DisplayRenderer::splash()` now accepts version and frequency parameters
- `renderMainMenu()` signature: `MenuScreen` → `int cursor` for cleaner cursor tracking
- Removed unused `g_bakeSettings` global (bake config now lives in `s_editBake` navigation state)
- Fixed Python 3.5 compatibility in `rename_firmware.py` (replaced f-strings with `.format()`)

## [1.7.0] — 2026-05-31

### Added
- ESP-IDF v5.x native port from Arduino PlatformIO framework
- Dual-core FreeRTOS architecture: Core 0 (control ~2.13s), Core 1 (UI ~30ms)
- Custom ADS1015 I2C driver replacing Adafruit library dependency
- Direct I2C register access for ADS1015 configuration + conversion reads
- NVS flash persistence using `nvs_open`/`nvs_get`/`nvs_set` native API
- GPIO control via `gpio_set_direction`/`gpio_set_level`/`gpio_get_level`
- LEDC PWM configuration via `ledc_timer_config`/`ledc_channel_config`
- Zero-cross ISR via `gpio_install_isr_service` + `gpio_isr_handler_add`
- ESP-IDF CMake build system with custom partition table (NVS + OTA)
- `rename_firmware.py` post-build versioning script
- AGENTS.md, CHANGELOG.md, user_manual.md documentation

### Changed
- Entry point: `setup()` + `loop()` → `app_main()`
- Timing: `millis()` → `esp_timer_get_time() / 1000`
- Debug output: `Serial.printf()` → `ESP_LOGI()`
- String formatting: `itoa()` → `snprintf()`
- Delay: `delay()` → `vTaskDelay()`
- Math: `constrain()` → `fmaxf()`/`fminf()`
- Struct initialization: designated initializers → `= {}` + field assignment
- NVS key buffer: 16 → 24 bytes to prevent truncation warnings
- ISR callback signature: `void()` → `void(void*)` for ESP-IDF compatibility

### Removed
- Arduino.h dependency
- Adafruit ADS1X15 library dependency
- PlatformIO build system
- Arduino-style Preferences API

### Fixed
- `IRAM_ATTR` attribute placement in header declaration
- `int` → `int32_t` type for NVS API compatibility
- Missing `getCoolingOutput()` method reference

### Hardware
- ESP32 WROOM dual-core @ 240MHz
- 128x64 OLED SSD1306 (SPI) via U8G2
- ADS1015 12-bit I2C ADC (2x K-type thermocouples)
- Zero-cross SSR with Bresenham PID (256 half-cycles)
- T962A 5-key membrane + HW E-STOP
- System cooling fan PWM (25kHz)

### Safety
- Over-temp protection at 280°C
- Sensor fault detection (<5°C)
- Spatial delta detection (>45°C)
- Dedicated hardware E-STOP
