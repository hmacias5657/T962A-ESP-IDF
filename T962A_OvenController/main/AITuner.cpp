#include "AITuner.h"
#include <cmath>

AITuner::AITuner()
    : _spatialDamping(0.85f)
    , _learningRate(0.01f)
    , _lastError(0.0f)
    , _heatDeadTimeSec(0.0f)
    , _coolDeadTimeSec(0.0f) {}

int AITuner::selectZone(float setpoint) const {
    if (setpoint < 100.0f) return 0;
    if (setpoint < 150.0f) return 1;
    if (setpoint < 200.0f) return 2;
    if (setpoint < 250.0f) return 3;
    return 4;
}

PidGains AITuner::scheduleHeaterGains(const PidGains* zoneGains, int zone) const {
    PidGains g = zoneGains[zone];
    g.kp *= _spatialDamping;
    g.ki *= _spatialDamping;
    g.kd *= _spatialDamping;
    return g;
}

PidGains AITuner::scheduleCoolingGains(const PidGains* zoneGains, int zone) const {
    PidGains g = zoneGains[zone];
    g.kp *= (1.0f - _spatialDamping);
    g.ki *= (1.0f - _spatialDamping);
    g.kd *= (1.0f - _spatialDamping);
    return g;
}

void AITuner::learn(float actualTemp, float setpoint, float output, float dt) {
    float error = setpoint - actualTemp;
    float errorDelta = (error - _lastError) / (dt + 0.001f);
    _lastError = error;
    _spatialDamping += _learningRate * error * errorDelta * dt;
    _spatialDamping = fmaxf(0.5f, fminf(1.0f, _spatialDamping));
}

void AITuner::setDeadTime(float heatDeadTimeSec, float coolDeadTimeSec) {
    _heatDeadTimeSec = heatDeadTimeSec;
    _coolDeadTimeSec = coolDeadTimeSec;
    float dtFactor = 1.0f / (1.0f + heatDeadTimeSec + coolDeadTimeSec);
    _learningRate = 0.01f * dtFactor;
}

float AITuner::getHeatDeadTime() const { return _heatDeadTimeSec; }
float AITuner::getCoolDeadTime() const { return _coolDeadTimeSec; }

void AITuner::reset() {
    _spatialDamping = 0.85f;
    _lastError = 0.0f;
}
