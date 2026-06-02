#include "ProfileEngine.h"
#include <cmath>

ProfileEngine::ProfileEngine() : _loaded(false) {
    _recipe = ReflowRecipe();
}

void ProfileEngine::loadRecipe(const ReflowRecipe& recipe) {
    _recipe = recipe;
    _loaded = true;
}

float ProfileEngine::getSetpoint(uint32_t elapsedSec) const {
    if (!_loaded) return 0.0f;

    uint32_t t = elapsedSec;
    float ambient = 25.0f;

    uint32_t tRampEnd = _recipe.rampTimeSec;
    uint32_t tSoakEnd = tRampEnd + _recipe.soakTimeSec;
    uint32_t tReflowEnd = tSoakEnd + _recipe.reflowTimeSec;
    uint32_t tHoldEnd = tReflowEnd + _recipe.holdTimeSec;

    if (t <= tRampEnd) {
        return interpolate((float)t, 0, (float)tRampEnd, ambient, _recipe.soakTemp);
    } else if (t <= tSoakEnd) {
        return _recipe.soakTemp;
    } else if (t <= tReflowEnd) {
        return interpolate((float)t, (float)tSoakEnd, (float)tReflowEnd, _recipe.soakTemp, _recipe.peakTemp);
    } else if (t <= tHoldEnd) {
        return _recipe.peakTemp;
    } else {
        return interpolate((float)t, (float)tHoldEnd, (float)(tHoldEnd + 60), _recipe.peakTemp, ambient);
    }
}

ReflowStage ProfileEngine::getStage(uint32_t elapsedSec) const {
    if (!_loaded) return STAGE_IDLE;

    uint32_t t = elapsedSec;
    if (t < _recipe.rampTimeSec) return STAGE_PREHEAT;
    t -= _recipe.rampTimeSec;
    if (t < _recipe.soakTimeSec) return STAGE_SOAK;
    t -= _recipe.soakTimeSec;
    if (t < _recipe.reflowTimeSec) return STAGE_REFLOW;
    t -= _recipe.reflowTimeSec;
    if (t < _recipe.holdTimeSec + 60) return STAGE_COOLDOWN;
    return STAGE_COMPLETE;
}

uint32_t ProfileEngine::getTotalDuration() const {
    if (!_loaded) return 0;
    return _recipe.rampTimeSec + _recipe.soakTimeSec +
           _recipe.reflowTimeSec + _recipe.holdTimeSec + 60;
}

const ReflowRecipe* ProfileEngine::getRecipe() const {
    return _loaded ? &_recipe : nullptr;
}

float ProfileEngine::interpolate(float t, float t0, float t1, float v0, float v1) const {
    if (t1 <= t0) return v0;
    float ratio = (t - t0) / (t1 - t0);
    ratio = fmaxf(0.0f, fminf(1.0f, ratio));
    return v0 + ratio * (v1 - v0);
}
