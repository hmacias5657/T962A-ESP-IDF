# Adaptive PID Reflow Oven Controller — Ground-Up ESP-IDF Implementation Guide

**Date**: May 31, 2026  
**Target**: Port from Arduino framework (PlatformIO) to ESP-IDF v5.x native SDK  
**Base Project**: T962A oven conversion, ESP32 WROOM dual-core, 128x64 GLCD (KS0108), 2x K-type TC via ADS1015

---

## 1. Project Structure (ESP-IDF CMake)

```
OvenController/
├── CMakeLists.txt                  # Top-level CMake (idf_component_register)
├── sdkconfig                       # idf.py menuconfig output
├── main/
│   ├── CMakeLists.txt              # main component registration
│   ├── Config.h                    # Pin mappings, constants, limits, firmware version
│   ├── SharedData.h                # ReflowRecipe, PidGains, ThermalTelemetry, state/stage enums
│   ├── main.cpp                    # app_main(), FreeRTOS dual-core task creation
│   ├── BresenhamPID.h / .cpp       # ZC ISR (IRAM_ATTR), dual Bresenham channels, heater + cooling PID
│   ├── TemperatureReader.h / .cpp  # ADS1015 I2C driver (no Adafruit), calibration offsets
│   ├── ProfileEngine.h / .cpp      # 5-stage linear interpolation, bake mode
│   ├── AITuner.h / .cpp            # Spatial damping + learning heuristic heater/cooling gain scheduler
│   ├── DisplayRenderer.h / .cpp    # KS0108 via U8g2 ESP-IDF, menu machine, live plot, 8-min axes, °C/°F conversion, cal test graph, 3-page cal info
│   ├── ButtonDebouncer.h / .cpp    # 50ms non-blocking debounce, 5 T962A keys + E-STOP
│   ├── Buzzer.h / .cpp             # 6 buzzer pattern generator
│   └── ADS1015_Driver.h / .cpp     # NEW: Direct I2C read for ADS1015 (replaces Adafruit library)
├── components/
│   └── u8g2/                       # U8g2 library as ESP-IDF component with GPIO callbacks for KS0108 parallel display
├── rename_firmware.py              # (optional) post-build firmware versioning
└── partitions.csv                  # Custom partition table with NVS
```

### Step 0: Initialize ESP-IDF Project

```bash
idf.py create-project OvenController
cd OvenController
idf.py set-target esp32
idf.py menuconfig  # Set: flash size, CPU freq 240MHz, UART baud 115200
```

Manual edits to `sdkconfig` (or menuconfig):
- `CONFIG_FREERTOS_UNICORE=n`
- `CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ=240`
- `CONFIG_PARTITION_TABLE_CUSTOM=y` (use `partitions.csv` below)

### Partition Table (`partitions.csv`)

```
# Name,   Type, SubType, Offset,  Size, Flags
nvs,      data, nvs,     0x9000,  0x5000,
otadata,  data, ota,     0xe000,  0x2000,
app0,     app,  ota_0,   0x10000, 0x1E0000,
app1,     app,  ota_1,   0x1F0000,0x1E0000,
nvs2,     data, nvs,     0x3D0000,0x30000,
coredump, data, coredump,0x400000,0x10000,
```

---

## 2. Key ESP-IDF Replacements vs Arduino

| Arduino API | ESP-IDF Replacement |
|---|---|
| `Arduino.h` | `esp_log.h`, `freertos/FreeRTOS.h`, `driver/gpio.h`, `driver/ledc.h`, `driver/i2c_master.h`, `nvs_flash.h` |
| `pinMode(pin, OUTPUT)` | `gpio_set_direction(pin, GPIO_MODE_OUTPUT)` |
| `digitalWrite(pin, HIGH/LOW)` | `gpio_set_level(pin, 0/1)` |
| `digitalRead(pin)` | `gpio_get_level(pin)` |
| `attachInterrupt(pin, fn, RISING)` | `gpio_set_intr_type(pin, GPIO_INTR_POSEDGE)` + `gpio_install_isr_service(0)` + `gpio_isr_handler_add(pin, fn, NULL)` |
| `millis()` | `esp_timer_get_time() / 1000` (or `pdTICKS_TO_MS(xTaskGetTickCount())`) |
| `delay(ms)` | `vTaskDelay(pdMS_TO_TICKS(ms))` |
| `Serial.printf(...)` | `ESP_LOGI(TAG, ...)` |
| `constrain(x, min, max)` | Manual `fmax(fmin(...))` or `MAX(MIN(...))` |
| `abs()` / `fabs()` | `abs()` / `fabsf()` from `math.h` |
| `itoa(val, str, base)` | `snprintf(str, sizeof(str), "%d", val)` |
| `ledcSetup(chan, freq, res)` | `ledc_timer_config_t` + `ledc_timer_config()` |
| `ledcAttachPin(pin, chan)` | `ledc_channel_config_t` + `ledc_channel_config()` |
| `ledcWrite(chan, duty)` | `ledc_set_duty(chan, duty)` + `ledc_update_duty(chan)` |
| `Preferences` (NVS Arduino) | `nvs_open()` / `nvs_get_*()` / `nvs_set_*()` / `nvs_commit()` / `nvs_close()` |
| `Arduino String` | `std::string` or `char[]` + `snprintf` |
| `setup()` + `loop()` | `app_main()` |
| `Adafruit_ADS1X15` library | Direct I2C driver to read ADS1015 registers (Section 8) |
| `U8G2_SSD1306_128X64_NONAME_1_4W_HW_SPI(rotation, cs, dc, rst)` | `u8g2_Setup_ks0108_128x64_f(u8g2, U8G2_R0, u8x8_byte_ks0108, u8x8_gpio_and_delay_esp32)` + `u8x8_SetPin()` for all 12 parallel pins |

---

## 3. NVS Flash Initialization (ESP-IDF)

In `main.cpp`'s `app_main()`, first thing:

```cpp
esp_err_t ret = nvs_flash_init();
if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
}
ESP_ERROR_CHECK(ret);
```

Replace all `Preferences` calls with:

```cpp
nvs_handle_t nvs;
nvs_open("reflow", NVS_READWRITE, &nvs);
nvs_get_i32(nvs, "recipeCount", &storedRecipeCount);
nvs_set_i32(nvs, "recipeCount", newCount);
nvs_commit(nvs);
nvs_close(nvs);
```

**All NVS key names and schemas from the Arduino version remain identical.**  
Keys: `r%d_name`, `r%d_pre`, `r%d_soak`, `r%d_peak`, `r%d_ramp`, `r%d_soakT`, `r%d_refl`, `r%d_hold`, `r%d_z%d_kp/ki/kd`, `r%d_z%d_ckp/cki/ckd`, `recipeCount`, `maxBakeMin`, `useF`, `tc1Off`, `tc2Off`.

**Data type mapping**:  
- `putInt` → `nvs_set_i32` / `nvs_get_i32`  
- `putFloat` → `nvs_set_i32` (store as `* 1000` integer) or `nvs_set_blob` for raw float  
- `putString` → `nvs_set_str` / `nvs_get_str`  
- `putUChar` → `nvs_set_u8` / `nvs_get_u8`

---

## 4. U8G2 Display (ESP-IDF — No Arduino)

U8G2 must be added as an ESP-IDF component. The KS0108 display uses the parallel 8-bit (6800) interface, not SPI.

### Adding U8G2 component
Create `components/u8g2/` with the U8G2 source + a `CMakeLists.txt`:

```cmake
file(GLOB U8G2_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.c")
idf_component_register(SRCS ${U8G2_SOURCES}
                       INCLUDE_DIRS "."
                       REQUIRES driver)
```

The `file(GLOB .../*.c)` automatically picks up `u8x8_d_ks0108.c` (display driver) and `u8x8_byte.c` (contains `u8x8_byte_ks0108`).

### Constructor change (critical)

**Arduino version (SSD1306 SPI)**:
```cpp
U8G2_SSD1306_128X64_NONAME_1_4W_HW_SPI(U8G2_R0, PIN_DISPLAY_CS, PIN_DISPLAY_DC, PIN_DISPLAY_RST);
```

**ESP-IDF version (KS0108 parallel)**:
```cpp
U8G2 _display;
// In begin():
u8g2_Setup_ks0108_128x64_f(&_display, U8G2_R0, u8x8_byte_ks0108, u8x8_gpio_and_delay_esp32);
u8x8_SetPin(u8g2_GetU8x8(&_display), U8X8_PIN_D0, PIN_LCD_D0);
u8x8_SetPin(u8g2_GetU8x8(&_display), U8X8_PIN_D1, PIN_LCD_D1);
// ... D2–D7, ENABLE, DC, CS, CS1, CS2
```

No SPI bus initialization needed. The KS0108 is driven entirely via GPIO.

### 12-pin KS0108 Parallel Bus
| Signal | GPIO | Notes |
|--------|------|-------|
| D0–D7  | 25,26,27,32,33,19,21,5 | 8-bit data bus |
| RS     | 14 | Register select (0=cmd, 1=data) |
| E      | 2  | Enable strobe |
| CS1    | 15 | Chip select — left half (columns 0–63) |
| CS2    | 13 | Chip select — right half (columns 64–127) |
| RST    | —  | Hardware RC reset (10k + 1µF) — no GPIO |

---

## 5. Bresenham PID — IRAM ISR

The `IRAM_ATTR` attribute already used in the Arduino version works identically in ESP-IDF for ISR functions.

### GPIO ISR registration change

**Arduino**:
```cpp
pinMode(PIN_ZC_INTERRUPT, INPUT_PULLUP);
attachInterrupt(digitalPinToInterrupt(PIN_ZC_INTERRUPT), _isrZeroCrossing, RISING);
```

**ESP-IDF**:
```cpp
gpio_config_t zc_io = {
    .pin_bit_mask = (1ULL << PIN_ZC_INTERRUPT),
    .mode = GPIO_MODE_INPUT,
    .pull_up_en = GPIO_PULLUP_ENABLE,
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .intr_type = GPIO_INTR_POSEDGE,
};
gpio_config(&zc_io);

gpio_install_isr_service(0);  // once at startup
gpio_isr_handler_add(PIN_ZC_INTERRUPT, _isrZeroCrossing, NULL);
```

### GPIO output for SSR

**Arduino**:
```cpp
pinMode(PIN_SSR_GATE, OUTPUT);
digitalWrite(PIN_SSR_GATE, LOW);
```

**ESP-IDF**:
```cpp
gpio_set_direction(PIN_SSR_GATE, GPIO_MODE_OUTPUT);
gpio_set_level(PIN_SSR_GATE, 0);
```

---

## 6. LEDC PWM (System Cooling Fan)

**Arduino**:
```cpp
ledcSetup(SYS_FAN_PWM_CHAN, SYS_FAN_PWM_FREQ, SYS_FAN_PWM_RES);
ledcAttachPin(PIN_SYS_FAN_PWM, SYS_FAN_PWM_CHAN);
```

**ESP-IDF**:
```cpp
ledc_timer_config_t timer_cfg = {
    .speed_mode = LEDC_LOW_SPEED_MODE,
    .duty_resolution = LEDC_TIMER_8_BIT,
    .timer_num = LEDC_TIMER_0,
    .freq_hz = SYS_FAN_PWM_FREQ,
    .clk_cfg = LEDC_AUTO_CLK,
};
ledc_timer_config(&timer_cfg);

ledc_channel_config_t chan_cfg = {
    .gpio_num = PIN_SYS_FAN_PWM,
    .speed_mode = LEDC_LOW_SPEED_MODE,
    .channel = LEDC_CHANNEL_0,
    .timer_sel = LEDC_TIMER_0,
    .duty = 0,
    .hpoint = 0,
};
ledc_channel_config(&chan_cfg);
```

Duty update:
```cpp
ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
```

---

## 7. FreeRTOS Tasks (nearly identical)

The FreeRTOS API is shared between Arduino and ESP-IDF. The existing `xTaskCreatePinnedToCore`, `xQueueOverwrite`, `xSemaphoreTake`, `vTaskDelayUntil` all work unchanged.

**Main entry point change**: `setup()` + `loop()` → `app_main()`

```cpp
extern "C" void app_main() {
    // 1. Initialize NVS
    // 2. Initialize I2C bus (ADS1015)
    // 3. Initialize GPIO (SSR, ZC, buttons, buzzer)
    // 4. Initialize LEDC (system fan)
    // 5. Load NVS data (recipes, settings, calibration)
    // 6. Create objects
    // 7. Create queues / mutexes
    // 8. Create Core 0 + Core 1 tasks
    // 9. Delete self task (vTaskDelete(NULL))
}
```

The existing `loop()` body (`vTaskDelete(NULL)`) works in ESP-IDF too.

---

## 8. ADS1015 I2C Driver (NEW — replaces Adafruit library)

The Adafruit ADS1X15 library is Arduino-only. Implement direct I2C communication using the ESP-IDF I2C driver.

### I2C Bus Initialization

```cpp
#include "driver/i2c_master.h"

i2c_master_bus_handle_t bus_handle;
i2c_master_bus_config_t bus_cfg = {
    .i2c_port = I2C_NUM_0,
    .sda_io_num = PIN_I2C_SDA,
    .scl_io_num = PIN_I2C_SCL,
    .clk_source = I2C_CLK_SRC_DEFAULT,
    .glitch_ignore_cnt = 7,
    .flags = { .enable_internal_pullup = true },
};
i2c_new_master_bus(&bus_cfg, &bus_handle);

i2c_master_dev_handle_t ads1015_handle;
i2c_device_config_t dev_cfg = {
    .dev_addr_length = I2C_ADDR_BIT_LEN_7,
    .device_address = ADS1015_ADDR,
    .scl_speed_hz = 400000,
};
i2c_master_bus_add_device(bus_handle, &dev_cfg, &ads1015_handle);
```

### ADS1015 Register Read

```
ADS1015 Registers:
 0x00 = Conversion Register (read-only, 12-bit)
 0x01 = Config Register (write to configure)
 0x02 = Lo_thresh
 0x03 = Hi_thresh

Config register (16-bit):
 Bit 15:     Operational status / start conversion (1=start)
 Bits 14-12: MUX input selection (000=A0, 001=A1, ...)
 Bits 11-9:  PGA gain
 Bit 8:      Mode (0=continuous, 1=single-shot)
 Bits 7-5:   Data rate
 Bits 4-0:   Comparator config
```

```cpp
// Read single-ended channel (0=A0, 1=A1)
int16_t readADS1015(uint8_t channel) {
    // Write config to start single-shot conversion
    uint16_t config = 0x8000    // Start conversion
                   | (0x04 | (channel << 4)) << 12  // MUX: A0/A1/GND
                   | 0x0200    // PGA ±4.096V (GAIN_ONE)
                   | 0x0100    // Single-shot mode
                   | 0x0060;   // 1600 SPS data rate
    uint8_t config_bytes[3] = {0x01, (uint8_t)(config >> 8), (uint8_t)(config & 0xFF)};
    i2c_master_transmit(ads1015_handle, config_bytes, 3, pdMS_TO_TICKS(10));

    // Wait for conversion (typical ~600µs at 1600 SPS)
    vTaskDelay(pdMS_TO_TICKS(2));

    // Read conversion register (0x00)
    uint8_t reg_addr = 0x00;
    uint8_t raw[2];
    i2c_master_transmit_receive(ads1015_handle, &reg_addr, 1, raw, 2, pdMS_TO_TICKS(10));

    int16_t value = ((int16_t)raw[0] << 8) | raw[1];
    return value >> 4;  // 12-bit right-aligned
}
```

**Temperature conversion** (no change — stays as `voltage / 0.01` where `voltage = raw * 4.096 / 2048`):

```cpp
float rawToCelsius(int16_t raw) {
    float voltage = (raw * ADS1015_FSR_VOLTS) / 2048.0f;
    return voltage / TC_AMP_SENSITIVITY;
}
```

---

## 9. Task Stack Sizes

ESP-IDF default stack size is larger than Arduino FreeRTOS. The existing values work:
- Core 0: 4096 bytes (priority 3)
- Core 1: 8192 bytes (priority 1)

If U8G2 rendering requires more stack on Core 1, increase to 10000.

---

## 10. Build Configuration (`CMakeLists.txt`)

### Top-level `CMakeLists.txt`

```cmake
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(OvenController)
```

### `main/CMakeLists.txt`

```cmake
idf_component_register(
    SRCS "main.cpp" "BresenhamPID.cpp" "TemperatureReader.cpp"
         "ProfileEngine.cpp" "AITuner.cpp" "DisplayRenderer.cpp"
         "ButtonDebouncer.cpp" "Buzzer.cpp" "ADS1015_Driver.cpp"
    INCLUDE_DIRS "."
    REQUIRES driver nvs_flash esp_timer u8g2
)
```

---

## 11. All Remaining Source Code (Unchanged Logic)

The following files require **no algorithmic changes** — only the API substitutions listed above:

| File | Only Change Needed |
|---|---|
| `Config.h` | Replace `#include <stdint.h>` with ESP-IDF style. Pin defines stay same. |
| `SharedData.h` | Replace `#include <stdint.h>`. Enums/structs unchanged. |
| `BresenhamPID.h` | Replace `#include <Arduino.h>` with `#include "Config.h"` + `#include "SharedData.h"` + `#include <stdint.h>` |
| `ProfileEngine.h/.cpp` | Remove `#include "Config.h"` from `.cpp` if not needed. No Arduino API used. |
| `AITuner.h/.cpp` | Remove `#include <math.h>` — use `<cmath>`. No Arduino API. |
| `Buzzer.h/.cpp` | Replace `digitalWrite` with `gpio_set_level`. Replace `millis()` with `esp_timer_get_time() / 1000`. |

### `ButtonDebouncer.cpp` — Specific Changes

Replace:
```cpp
pinMode(PIN_BTN_F1_UP, INPUT_PULLUP);
```
With:
```cpp
gpio_set_direction(PIN_BTN_F1_UP, GPIO_MODE_INPUT);
gpio_set_pull_mode(PIN_BTN_F1_UP, GPIO_PULLUP_ONLY);
```

Replace `digitalRead(btn.pin)` with `gpio_get_level(btn.pin)`.

Replace `millis()` with `esp_timer_get_time() / (unsigned long)1000`.

### `Buzzer.cpp` — Specific Changes

Replace `digitalWrite(PIN_BUZZER, HIGH/LOW)` with `gpio_set_level(PIN_BUZZER, 1/0)`.

Replace `pinMode(PIN_BUZZER, OUTPUT)` with `gpio_set_direction(PIN_BUZZER, GPIO_MODE_OUTPUT)`.

### `BresenhamPID.cpp` — Specific Changes

Replace:
```cpp
pinMode(PIN_SSR_GATE, OUTPUT);
digitalWrite(PIN_SSR_GATE, LOW);
pinMode(PIN_COOLING_FAN_SSR, OUTPUT);
digitalWrite(PIN_COOLING_FAN_SSR, LOW);
pinMode(PIN_ZC_INTERRUPT, INPUT_PULLUP);
attachInterrupt(digitalPinToInterrupt(PIN_ZC_INTERRUPT), _isrZeroCrossing, RISING);
```
With GPIO config structs + ISR service (see Section 5).

Replace all `digitalWrite()` in `handleZeroCrossing()` and `emergencyStop()` with `gpio_set_level()`.

---

## 12. TemperatureReader — Complete Rewrite

Replace `Adafruit_ADS1015` with `ADS1015_Driver` class wrapping the I2C master handle. The `TemperatureData` struct, calibration offsets, and fault detection logic remain unchanged.

```cpp
// ADS1015_Driver.h
class ADS1015_Driver {
public:
    void init(i2c_master_bus_handle_t bus);
    int16_t readChannel(uint8_t channel);
private:
    i2c_master_dev_handle_t _dev;
};

// TemperatureReader.h — same interface, no Adafruit dependency
class TemperatureReader {
public:
    TemperatureReader();
    void init(i2c_master_bus_handle_t bus);
    TemperatureData readSensors();
    void setCalibrationOffset(int sensor, float offset);
    float getCalibrationOffset(int sensor) const;
private:
    ADS1015_Driver _ads;
    float _tc1Offset, _tc2Offset;
    float rawToCelsius(int16_t raw);
};
```

---

## 13. DisplayRenderer — U8G2 Constructor

Replace the constructor call as described in Section 4. The rendering methods (`renderMainMenu`, `renderLivePlot`, etc.) are all pure U8g2 API calls that work identically between Arduino and ESP-IDF. No rendering logic changes needed.

---

## 14. Porting Checklist

- [ ] **NVS**: Replace all `Preferences` calls with `nvs_open`/`nvs_get`/`nvs_set`/`nvs_commit`/`nvs_close`. Data types: int32 for ints, blob for floats (or scaled int32), str for strings.
- [ ] **GPIO**: Replace all `pinMode`/`digitalWrite`/`digitalRead` with `gpio_set_direction`/`gpio_set_level`/`gpio_get_level`.
- [ ] **ISR**: Replace `attachInterrupt` with `gpio_install_isr_service` + `gpio_isr_handler_add`. Keep `IRAM_ATTR`.
- [ ] **I2C (ADS1015)**: Remove Adafruit ADS1X15 library. Implement `i2c_master_bus_add_device` + `i2c_master_transmit`/`receive` for config + conversion register reads.
- [ ] **KS0108 (Display)**: Use `u8g2_Setup_ks0108_128x64_f` + `u8x8_byte_ks0108` callback. Set all 12 parallel pins via `u8x8_SetPin`. No SPI bus needed.
- [ ] **LEDC (System Fan)**: Replace `ledcSetup`/`ledcAttachPin` with `ledc_timer_config`/`ledc_channel_config`.
- [ ] **Timer**: Replace `millis()` with `esp_timer_get_time() / 1000`.
- [ ] **Serial**: Replace `Serial.printf` with `ESP_LOGI`.
- [ ] **Startup**: Replace `setup()` + `loop()` with `app_main()`.
- [ ] **Build**: Create `CMakeLists.txt` files, remove `platformio.ini`, manage `sdkconfig` via `idf.py menuconfig`.
- [ ] **Dependencies**: Replace `olikraus/U8g2` with IDF component version. Remove `adafruit/Adafruit ADS1X15`. Remove `Arduino.h` includes.
- [ ] **Debug logging**: Use `ESP_LOGI(TAG, fmt, ...)` with `#define TAG "OvenCtrl"`. Enable via `idf.py menuconfig` → Component config → Log output.

---

## 15. Build & Flash Commands

```bash
# Configure
idf.py set-target esp32
idf.py menuconfig

# Build
idf.py build

# Flash and monitor
idf.py -p /dev/ttyUSB0 flash monitor

# Clean
idf.py fullclean
```

---

## 16. Feature Summary (unchanged from v1.7.0)

- Dual-core FreeRTOS: Core 0 (control, ~2.13s period), Core 1 (UI, ~30ms refresh)
- Bresenham power distribution (256 half-cycles, zero-cross) — heater + cooling fan
- 5 independent PID gain sets per recipe for both heater and cooling
- ADS1015 12-bit external ADC via I2C with per-sensor calibration (±10°C, 0.5°C steps)
- 8-minute live temperature plot (280°C Y-axis, 128x64 KS0108 GLCD)
- Profile creation wizard (7-parameter: preheat/soak/peak temps, ramp/soak/reflow/hold times)
- Baking/drying mode (80–150°C, 1–300 min)
- °C/°F unit toggle
- Calibration Run (auto-tune recipe times with 1.2x margin, live temperature-vs-time graph)
- Calibration Info viewer (per-recipe per-zone PID gains + calibration test rates/dead times)
- System cooling fan PWM (temp-proportional 25kHz MOSFET)
- Oven cooling fan (Bresenham SSR, proportional cooldown + overshoot suppression)
- AITuner gain scheduler (spatial damping + learning heuristic)
- Safety: over-temp (280°C), sensor fault (<5°C), spatial delta (>45°C), dedicated HW E-STOP
- T962A 5-key membrane (F1-UP, F2-DOWN, F3-LEFT, F4-RIGHT, S-SELECT/START) + E-STOP
- Buzzer patterns: phase transition, cycle complete, error codes, E-STOP alarm
- NVS persistence: 10 recipes, per-zone heater/cooling gains, settings, calibration offsets
