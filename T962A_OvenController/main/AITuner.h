#pragma once

#include "SharedData.h"
#include <stdint.h>

class AITuner {
public:
    AITuner();
    int selectZone(ReflowStage stage) const;
    PidGains scheduleHeaterGains(const PidGains* zoneGains, int zone) const;
    PidGains scheduleCoolingGains(const PidGains* zoneGains, int zone) const;
    void setDeadTime(float heatDeadTimeSec, float coolDeadTimeSec);
    float getHeatDeadTime() const;
    float getCoolDeadTime() const;
    void reset();
    void resetMetrics(ZoneMetrics& m, float setpoint, uint32_t elapsedSec) const;
    void updateMetrics(ZoneMetrics& m, float temp, float setpoint, float dt) const;
    void fineTuneZone(const ZoneMetrics& m, PidGains& g) const;
private:
    float _heatDeadTimeSec;
    float _coolDeadTimeSec;
};
