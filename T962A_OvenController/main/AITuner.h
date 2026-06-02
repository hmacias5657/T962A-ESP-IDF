#pragma once

#include "SharedData.h"
#include <stdint.h>

class AITuner {
public:
    AITuner();
    int selectZone(float setpoint) const;
    PidGains scheduleHeaterGains(const PidGains* zoneGains, int zone) const;
    PidGains scheduleCoolingGains(const PidGains* zoneGains, int zone) const;
    void learn(float actualTemp, float setpoint, float output, float dt);
    void setDeadTime(float heatDeadTimeSec, float coolDeadTimeSec);
    float getHeatDeadTime() const;
    float getCoolDeadTime() const;
    void reset();
private:
    float _spatialDamping;
    float _learningRate;
    float _lastError;
    float _heatDeadTimeSec;
    float _coolDeadTimeSec;
};
