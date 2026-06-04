#pragma once

#include <stdint.h>
#include <stddef.h>

#define FIRMWARE_VERSION_MAJOR 1
#define FIRMWARE_VERSION_MINOR 11
#define FIRMWARE_VERSION_PATCH 0

// T962A pin mappings (ESP32 WROOM)
#define PIN_ZC_INTERRUPT       GPIO_NUM_4
#define PIN_SSR_GATE           GPIO_NUM_16
#define PIN_COOLING_FAN_SSR    GPIO_NUM_17
#define PIN_BUZZER             GPIO_NUM_18
#define PIN_BTN_F1_UP          GPIO_NUM_36   // input-only pin
#define PIN_BTN_F2_DOWN        GPIO_NUM_37   // input-only pin
#define PIN_BTN_F3_LEFT        GPIO_NUM_38   // input-only pin
#define PIN_BTN_F4_RIGHT       GPIO_NUM_39   // input-only pin
#define PIN_BTN_S_SELECT       GPIO_NUM_34   // input-only pin
#define PIN_ESTOP              GPIO_NUM_35
#define PIN_SYS_FAN_PWM        GPIO_NUM_0    // 10k pull-up to 3.3V required
#define PIN_I2C_SDA            GPIO_NUM_22
#define PIN_I2C_SCL            GPIO_NUM_23

// KS0108 128x64 GLCD (8-bit parallel 6800 mode)
#define PIN_LCD_D0             GPIO_NUM_25
#define PIN_LCD_D1             GPIO_NUM_26
#define PIN_LCD_D2             GPIO_NUM_27
#define PIN_LCD_D3             GPIO_NUM_32
#define PIN_LCD_D4             GPIO_NUM_33
#define PIN_LCD_D5             GPIO_NUM_19
#define PIN_LCD_D6             GPIO_NUM_21
#define PIN_LCD_D7             GPIO_NUM_5
#define PIN_LCD_RS             GPIO_NUM_14   // Register Select (A0)
#define PIN_LCD_E              GPIO_NUM_2    // Enable strobe
#define PIN_LCD_CS1            GPIO_NUM_15   // Chip select – left half
#define PIN_LCD_CS2            GPIO_NUM_13   // Chip select – right half

// ADS1015
#define ADS1015_ADDR           0x48
#define ADS1015_FSR_VOLTS      4.096f
#define TC_AMP_SENSITIVITY     0.01f

// LEDC (system fan PWM)
#define SYS_FAN_PWM_CHAN       LEDC_CHANNEL_0
#define SYS_FAN_PWM_FREQ       25000
#define SYS_FAN_PWM_RES        LEDC_TIMER_8_BIT

// Bresenham PID
#define BRESENHAM_CYCLES       256
#define ZC_HALF_CYCLE_US_DEFAULT 10000
#define PID_INTERVAL_MS_DEFAULT  2125
#define ZC_MEASURE_SAMPLES      20
#define PID_ZONES              5

// Safety limits
#define OVERTEMP_C             280.0f
#define SENSOR_FAULT_C         5.0f
#define SPATIAL_DELTA_C        45.0f
#define MAX_BOARD_TEMP_C       85.0f

// Display
#define DISPLAY_WIDTH          128
#define DISPLAY_HEIGHT         64
#define PLOT_Y_MAX             280
#define PLOT_DURATION_SECS     480

// NVS
#define NVS_NAMESPACE          "reflow"
#define MAX_RECIPES            10
#define MAX_RECIPE_NAME_LEN    24
#define NVS_CALIB_NAMESPACE    "calib"

// Calibration test
#define CALIB_TEST_TIMEOUT_SECS  300
#define CALIB_SAFETY_MARGIN      1.2f
#define CALIB_ZONE_0_MAX         100.0f
#define CALIB_ZONE_1_MAX         150.0f
#define CALIB_ZONE_2_MAX         200.0f
#define CALIB_ZONE_3_MAX         250.0f
#define CALIB_COOLDOWN_TEMP      120.0f
#define CALIB_DT_THRESHOLD       0.5f
#define CALIB_COOL_TIMEOUT_SECS  300

// Fine-tuning
#define FINE_TUNE_OVERSHOOT_C      3.0f
#define FINE_TUNE_STEADY_ERROR_C   2.0f
#define FINE_TUNE_OSCILLATION_C    3.0f
#define FINE_TUNE_ADJUST_RATE      0.05f
#define FINE_TUNE_DAMP_RATE        0.10f
#define FINE_TUNE_SETTLE_BAND_C    2.0f
#define FINE_TUNE_RISE_PCT         0.9f
#define FINE_TUNE_STEADY_FRAC      0.3f
#define FINE_TUNE_KP_MIN           0.1f
#define FINE_TUNE_KP_MAX           20.0f
#define FINE_TUNE_KI_MIN           0.0f
#define FINE_TUNE_KI_MAX           5.0f
#define FINE_TUNE_KD_MIN           0.0f
#define FINE_TUNE_KD_MAX           10.0f

// Feedforward
#define FF_CAP_PCT                 0.80f
#define COOLDOWN_RAMP_RESERVE_C    5.0f
