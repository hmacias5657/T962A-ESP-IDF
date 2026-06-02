#include "TemperatureReader.h"
#include "Config.h"
#include <cmath>

TemperatureReader::TemperatureReader() : _tc1Offset(0.0f), _tc2Offset(0.0f) {}

void TemperatureReader::init(i2c_master_bus_handle_t bus) {
    _ads.init(bus, ADS1015_ADDR);
}

TemperatureData TemperatureReader::readSensors() {
    TemperatureData data;
    int16_t raw1 = _ads.readChannel(0);
    int16_t raw2 = _ads.readChannel(1);

    data.tc1 = rawToCelsius(raw1) + _tc1Offset;
    data.tc2 = rawToCelsius(raw2) + _tc2Offset;
    data.avg = (data.tc1 + data.tc2) * 0.5f;
    data.delta = fabsf(data.tc1 - data.tc2);
    data.fault = (data.avg < SENSOR_FAULT_C) || (data.delta > SPATIAL_DELTA_C);

    return data;
}

void TemperatureReader::setCalibrationOffset(int sensor, float offset) {
    if (sensor == 0) _tc1Offset = offset;
    else _tc2Offset = offset;
}

float TemperatureReader::getCalibrationOffset(int sensor) const {
    return (sensor == 0) ? _tc1Offset : _tc2Offset;
}

float TemperatureReader::rawToCelsius(int16_t raw) {
    float voltage = (raw * ADS1015_FSR_VOLTS) / 2048.0f;
    return voltage / TC_AMP_SENSITIVITY;
}
