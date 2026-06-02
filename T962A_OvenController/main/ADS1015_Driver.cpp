#include "ADS1015_Driver.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void ADS1015_Driver::init(i2c_master_bus_handle_t bus, uint8_t addr) {
    _addr = addr;
    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address = _addr;
    dev_cfg.scl_speed_hz = 400000;
    i2c_master_bus_add_device(bus, &dev_cfg, &_dev);
}

int16_t ADS1015_Driver::readChannel(uint8_t channel) {
    uint16_t config = 0x8000
                    | (0x04 | (channel << 4)) << 12
                    | 0x0200
                    | 0x0100
                    | 0x0060;
    writeRegister(0x01, config);
    vTaskDelay(pdMS_TO_TICKS(2));
    int16_t raw = readRegister(0x00);
    return raw >> 4;
}

int16_t ADS1015_Driver::readRegister(uint8_t reg) {
    uint8_t data[2];
    i2c_master_transmit_receive(_dev, &reg, 1, data, 2, pdMS_TO_TICKS(10));
    return (int16_t)((data[0] << 8) | data[1]);
}

void ADS1015_Driver::writeRegister(uint8_t reg, uint16_t value) {
    uint8_t data[3] = {reg, (uint8_t)(value >> 8), (uint8_t)(value & 0xFF)};
    i2c_master_transmit(_dev, data, 3, pdMS_TO_TICKS(10));
}
