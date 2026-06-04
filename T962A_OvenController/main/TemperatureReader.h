#pragma once

#include "SharedData.h"
#include "ADS1015_Driver.h"
#include "driver/i2c_master.h"

class TemperatureReader {
public:
    TemperatureReader();
    void init(i2c_master_bus_handle_t bus);
    TemperatureData readSensors();
    void setCalibrationOffset(int sensor, float offset);
    float getCalibrationOffset(int sensor) const;
    void resetFilters();
private:
    ADS1015_Driver _ads;
    float _tc1Offset;
    float _tc2Offset;
    float _tc1Filtered;
    float _tc2Filtered;
    bool _filterInitialized;
    float rawToCelsius(int16_t raw);
};
