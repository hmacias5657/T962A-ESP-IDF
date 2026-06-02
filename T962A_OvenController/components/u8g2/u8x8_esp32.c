#define U8X8_USE_PINS
#include "u8x8.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "u8g2_esp32";

// ----- SPI byte function (SSD1306 / OLED) -----
static spi_device_handle_t _spi_dev = NULL;
static int _sdc_pin = -1;
static int _scs_pin = -1;

uint8_t u8x8_byte_esp32_hw_spi(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr) {
    (void)u8x8;
    (void)arg_ptr;
    switch (msg) {
        case U8X8_MSG_BYTE_INIT: {
            if (_spi_dev != NULL) return 1;
            spi_device_interface_config_t dev_cfg = {};
            dev_cfg.mode = 0;
            dev_cfg.clock_speed_hz = 8 * 1000 * 1000;
            dev_cfg.spics_io_num = _scs_pin;
            dev_cfg.queue_size = 1;
            esp_err_t ret = spi_bus_add_device(SPI2_HOST, &dev_cfg, &_spi_dev);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "spi_bus_add_device failed: %d", ret);
                return 0;
            }
            return 1;
        }
        case U8X8_MSG_BYTE_SET_DC:
            gpio_set_level((gpio_num_t)_sdc_pin, arg_int);
            return 1;
        case U8X8_MSG_BYTE_START_TRANSFER:
            return 1;
        case U8X8_MSG_BYTE_END_TRANSFER:
            return 1;
        case U8X8_MSG_BYTE_SEND: {
            if (_spi_dev == NULL || arg_ptr == NULL || arg_int == 0) return 0;
            spi_transaction_t trans = {};
            trans.length = arg_int * 8;
            trans.tx_buffer = arg_ptr;
            esp_err_t ret = spi_device_transmit(_spi_dev, &trans);
            if (ret != ESP_OK) return 0;
            return 1;
        }
        default:
            return 0;
    }
}

// ----- Common GPIO + delay callback -----
uint8_t u8x8_gpio_and_delay_esp32(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr) {
    (void)arg_ptr;
    switch (msg) {
        case U8X8_MSG_GPIO_AND_DELAY_INIT: {
            uint64_t pin_mask = 0;
            // Collect every standard / KS0108 pin that has been set
            int indices[] = {
                U8X8_PIN_D0, U8X8_PIN_D1, U8X8_PIN_D2, U8X8_PIN_D3,
                U8X8_PIN_D4, U8X8_PIN_D5, U8X8_PIN_D6, U8X8_PIN_D7,
                U8X8_PIN_E, U8X8_PIN_CS, U8X8_PIN_DC, U8X8_PIN_RESET,
                U8X8_PIN_CS1, U8X8_PIN_CS2
            };
            for (size_t i = 0; i < sizeof(indices)/sizeof(indices[0]); i++) {
                int p = (int)u8x8_GetPinValue(u8x8, indices[i]);
                if (p >= 0) pin_mask |= (1ULL << p);
            }
            if (pin_mask) {
                gpio_config_t io_conf = {
                    .pin_bit_mask = pin_mask,
                    .mode = GPIO_MODE_OUTPUT,
                    .pull_up_en = GPIO_PULLUP_DISABLE,
                    .pull_down_en = GPIO_PULLDOWN_DISABLE,
                    .intr_type = GPIO_INTR_DISABLE,
                };
                gpio_config(&io_conf);
            }
            // Capture DC & CS for legacy SPI byte function
            _sdc_pin = (int)u8x8_GetPinValue(u8x8, U8X8_PIN_DC);
            _scs_pin = (int)u8x8_GetPinValue(u8x8, U8X8_PIN_CS);
            return 1;
        }
        case U8X8_MSG_DELAY_MILLI:
            if (arg_int > 0) {
                int ticks = arg_int / portTICK_PERIOD_MS;
                if (ticks < 1) ticks = 1;
                vTaskDelay(ticks);
            }
            return 1;
        case U8X8_MSG_DELAY_10MICRO: {
            esp_rom_delay_us((uint32_t)arg_int * 10);
            return 1;
        }
        case U8X8_MSG_DELAY_100NANO: {
            esp_rom_delay_us(((uint32_t)arg_int + 99) / 100);
            return 1;
        }
        case U8X8_MSG_DELAY_NANO: {
            esp_rom_delay_us(((uint32_t)arg_int + 999) / 1000);
            return 1;
        }
        default:
            if (msg >= U8X8_MSG_GPIO(0)) {
                int pin = (int)u8x8_GetPinValue(u8x8, msg - U8X8_MSG_GPIO(0));
                if (pin >= 0) {
                    gpio_set_level((gpio_num_t)pin, arg_int);
                }
                return 1;
            }
            return 0;
    }
}
