#pragma once

#include "SharedData.h"
#include "Config.h"
#include <stdint.h>
#define U8X8_USE_PINS
#include "u8g2.h"

class DisplayRenderer {
public:
    DisplayRenderer();
    void init();
    void splash(const char* version, float lineFreqHz);
    void renderMainMenu(int cursor);
    void renderRunning(const ThermalTelemetry& telemetry);
    void renderLivePlot(const float* history, uint32_t count, float setpoint);
    void renderRecipeList(const ReflowRecipe* recipes, int count, int selected);
    void renderRecipeEdit(const ReflowRecipe& recipe, int field);
    void renderSettings(const SystemSettings& settings, int field);
    void renderBakeSetup(const BakeSettings& bake, int field);
    void renderCalibration();
    void renderCalibSubmenu(int cursor);
    void renderCalibTestRunning(bool isCooling, int zone, float temp, float maxRate, float curRate,
                                uint32_t elapsedSec, const float* history, uint32_t historyCount);
    void renderCalibTestResults(int oldRamp, int newRamp, int oldReflow, int newReflow,
                                const float* heatRates, const float* coolRates,
                                uint32_t heatDeadTimeMs, uint32_t coolDeadTimeMs,
                                int recipeCount);
    void renderCalibInfo(int recipeIdx, int recipeCount, const ReflowRecipe* recipes,
                         const float* heatRates, const float* coolRates,
                         uint32_t heatDeadTimeMs, uint32_t coolDeadTimeMs,
                         int page);
    void renderEstop();
    void renderMessage(const char* msg);
    void setUseFahrenheit(bool useF) { _useFahrenheit = useF; }
    void clear();
private:
    float toDisplayUnit(float tempC) const;
    float toDisplayRate(float rateCPerS) const;
    const char* unitSuffix() const;
    const char* rateSuffix() const;
    void drawProgressBar(int x, int y, int w, int h, float pct);
    void drawTempGraph(const float* history, uint32_t count, float setpoint);
    void drawMenuText(int line, const char* text, bool selected);
    u8g2_t _display;
    char _buf[64];
    bool _useFahrenheit;
};
