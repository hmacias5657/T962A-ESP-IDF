#pragma once

#include "Config.h"
#include "SharedData.h"
#include <stdint.h>
#include <cstdint>
#include "esp_attr.h"

class BresenhamPID {
public:
    BresenhamPID();
    void init();
    void setGains(const PidGains& gains);
    void setCoolingGains(const PidGains& gains);
    void setSetpoint(float setpoint);
    float compute(float input, float dt);
    void setRawHeaterPower(float power);
    void setRawCoolingPower(float power);
    float getOutput() const;
    float getCoolingOutput() const;
    void emergencyStop();
    bool isEstopped() const;
    void reset();
    static void handleZeroCrossing(void* arg);

    static void measureLineFrequency();
    static uint32_t getHalfCycleUs();
    static float getLineFrequency();
    static uint32_t getPidIntervalMs();

private:
    float _kp, _ki, _kd;
    float _ckp, _cki, _ckd;
    float _setpoint;
    float _integral;
    float _prevError;
    float _heaterOutput;
    float _coolingOutput;
    bool _estopped;

    void bresenhamPower(float power);
    void bresenhamCooling(float power);

    static volatile int _zcCount;
    static volatile int _heaterOnCycles;
    static volatile int _coolingOnCycles;
    static volatile bool _zcFlag;

    static volatile bool _measureDone;
    static volatile uint32_t _measuredHalfCycleUs;
    static volatile int _measureCount;
    static volatile int64_t _measureFirstTime;
};
