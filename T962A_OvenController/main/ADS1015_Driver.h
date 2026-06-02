#pragma once

#include <stdint.h>
#include "driver/i2c_master.h"

class ADS1015_Driver {
public:
    void init(i2c_master_bus_handle_t bus, uint8_t addr = 0x48);
    int16_t readChannel(uint8_t channel);
private:
    i2c_master_dev_handle_t _dev;
    uint8_t _addr;
    int16_t readRegister(uint8_t reg);
    void writeRegister(uint8_t reg, uint16_t value);
};
