#pragma once

#include "SharedData.h"
#include <stdint.h>

class ProfileEngine {
public:
    ProfileEngine();
    void loadRecipe(const ReflowRecipe& recipe);
    float getSetpoint(uint32_t elapsedSec) const;
    ReflowStage getStage(uint32_t elapsedSec) const;
    uint32_t getTotalDuration() const;
    const ReflowRecipe* getRecipe() const;
private:
    ReflowRecipe _recipe;
    bool _loaded;
    float interpolate(float t, float t0, float t1, float v0, float v1) const;
};
