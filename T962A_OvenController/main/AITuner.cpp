#include "AITuner.h"
#include "Config.h"
#include <cmath>

AITuner::AITuner()
    : _heatDeadTimeSec(0.0f)
    , _coolDeadTimeSec(0.0f) {}

int AITuner::selectZone(ReflowStage stage) const {
    switch (stage) {
        case STAGE_PREHEAT:  return 0;
        case STAGE_SOAK:     return 1;
        case STAGE_REFLOW:   return 2;
        case STAGE_COOLDOWN: return 3;
        default:             return 4;
    }
}

PidGains AITuner::scheduleHeaterGains(const PidGains* zoneGains, int zone) const {
    return zoneGains[zone];
}

PidGains AITuner::scheduleCoolingGains(const PidGains* zoneGains, int zone) const {
    return zoneGains[zone];
}

void AITuner::setDeadTime(float heatDeadTimeSec, float coolDeadTimeSec) {
    _heatDeadTimeSec = heatDeadTimeSec;
    _coolDeadTimeSec = coolDeadTimeSec;
}

float AITuner::getHeatDeadTime() const { return _heatDeadTimeSec; }
float AITuner::getCoolDeadTime() const { return _coolDeadTimeSec; }

void AITuner::reset() {}

void AITuner::resetMetrics(ZoneMetrics& m, float setpoint, uint32_t elapsedSec) const {
    m.peakTemp = -999.0f;
    m.setpoint = setpoint;
    m.steadyError = 0.0f;
    m.oscillationAmp = 0.0f;
    m.riseTime90 = 0.0f;
    m.settlingTime = 0.0f;
    m.maxRate = 0.0f;
    m.sampleCount = 0;
    m.errorSum = 0.0f;
    m.errorAbsSum = 0.0f;
    m.tempMin = 999.0f;
    m.tempMax = -999.0f;
    m.lastTemp = -999.0f;
    m.prevSlope = 0.0f;
    m.zeroCrossCount = 0;
    m.entryTimeSec = elapsedSec;
    m.settled = false;
}

void AITuner::updateMetrics(ZoneMetrics& m, float temp, float setpoint, float dt) const {
    m.sampleCount++;
    m.errorSum += setpoint - temp;
    m.errorAbsSum += fabsf(setpoint - temp);

    if (temp > m.tempMax) m.tempMax = temp;
    if (temp < m.tempMin) m.tempMin = temp;

    if (temp > m.peakTemp) {
        m.peakTemp = temp;
        if (m.peakTemp >= setpoint * FINE_TUNE_RISE_PCT && m.riseTime90 == 0.0f) {
            m.riseTime90 = m.sampleCount * dt;
        }
    }

    if (!m.settled && fabsf(temp - setpoint) <= FINE_TUNE_SETTLE_BAND_C) {
        m.settlingTime = m.sampleCount * dt;
        m.settled = true;
    }

    if (m.lastTemp != -999.0f) {
        float slope = (temp - m.lastTemp) / dt;
        float absSlope = fabsf(slope);
        if (absSlope > m.maxRate) m.maxRate = absSlope;

        if (m.prevSlope != 0.0f && slope * m.prevSlope < 0.0f) {
            m.zeroCrossCount++;
        }
        m.prevSlope = slope;
    }
    m.lastTemp = temp;
}

void AITuner::fineTuneZone(const ZoneMetrics& m, PidGains& g) const {
    float overshoot = m.peakTemp - m.setpoint;
    float steadyErr = (m.sampleCount > 0) ? m.errorAbsSum / m.sampleCount : 0.0f;

    float range = m.tempMax - m.tempMin;
    float oscAmp = (m.zeroCrossCount > 2) ? range : 0.0f;

    if (overshoot > FINE_TUNE_OVERSHOOT_C) {
        g.kp *= (1.0f - FINE_TUNE_ADJUST_RATE);
        g.kd *= (1.0f + FINE_TUNE_ADJUST_RATE);
    }

    if (steadyErr > FINE_TUNE_STEADY_ERROR_C) {
        g.ki *= (1.0f + FINE_TUNE_ADJUST_RATE);
    }

    if (oscAmp > FINE_TUNE_OSCILLATION_C) {
        g.kp *= (1.0f - FINE_TUNE_ADJUST_RATE);
        g.kd *= (1.0f + FINE_TUNE_DAMP_RATE);
    }

    if (!m.settled && m.sampleCount > 10) {
        g.ki *= (1.0f - FINE_TUNE_ADJUST_RATE);
    }

    g.kp = fmaxf(FINE_TUNE_KP_MIN, fminf(FINE_TUNE_KP_MAX, g.kp));
    g.ki = fmaxf(FINE_TUNE_KI_MIN, fminf(FINE_TUNE_KI_MAX, g.ki));
    g.kd = fmaxf(FINE_TUNE_KD_MIN, fminf(FINE_TUNE_KD_MAX, g.kd));
}
