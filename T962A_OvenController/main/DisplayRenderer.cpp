#include "DisplayRenderer.h"
#include <stdio.h>
#include <string.h>
#include <cmath>
#include "driver/gpio.h"

extern "C" {
    uint8_t u8x8_gpio_and_delay_esp32(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr);
}

DisplayRenderer::DisplayRenderer() {
    _buf[0] = '\0';
    _useFahrenheit = false;
}

float DisplayRenderer::toDisplayUnit(float tempC) const {
    if (_useFahrenheit) return tempC * 9.0f / 5.0f + 32.0f;
    return tempC;
}

const char* DisplayRenderer::unitSuffix() const {
    return _useFahrenheit ? "F" : "C";
}

float DisplayRenderer::toDisplayRate(float rateCPerS) const {
    if (_useFahrenheit) return rateCPerS * 9.0f / 5.0f;
    return rateCPerS;
}

const char* DisplayRenderer::rateSuffix() const {
    return _useFahrenheit ? "F/s" : "C/s";
}

void DisplayRenderer::init() {
    u8g2_Setup_ks0108_128x64_f(
        &_display, U8G2_R0,
        u8x8_byte_ks0108,
        u8x8_gpio_and_delay_esp32
    );
    u8x8_SetPin(u8g2_GetU8x8(&_display), U8X8_PIN_D0, PIN_LCD_D0);
    u8x8_SetPin(u8g2_GetU8x8(&_display), U8X8_PIN_D1, PIN_LCD_D1);
    u8x8_SetPin(u8g2_GetU8x8(&_display), U8X8_PIN_D2, PIN_LCD_D2);
    u8x8_SetPin(u8g2_GetU8x8(&_display), U8X8_PIN_D3, PIN_LCD_D3);
    u8x8_SetPin(u8g2_GetU8x8(&_display), U8X8_PIN_D4, PIN_LCD_D4);
    u8x8_SetPin(u8g2_GetU8x8(&_display), U8X8_PIN_D5, PIN_LCD_D5);
    u8x8_SetPin(u8g2_GetU8x8(&_display), U8X8_PIN_D6, PIN_LCD_D6);
    u8x8_SetPin(u8g2_GetU8x8(&_display), U8X8_PIN_D7, PIN_LCD_D7);
    u8x8_SetPin(u8g2_GetU8x8(&_display), U8X8_PIN_E, PIN_LCD_E);
    u8x8_SetPin(u8g2_GetU8x8(&_display), U8X8_PIN_DC, PIN_LCD_RS);
    u8x8_SetPin(u8g2_GetU8x8(&_display), U8X8_PIN_CS, PIN_LCD_CS1);
    u8x8_SetPin(u8g2_GetU8x8(&_display), U8X8_PIN_CS1, PIN_LCD_CS2);
    u8g2_InitDisplay(&_display);
    u8g2_SetPowerSave(&_display, 0);
    u8g2_SetFont(&_display, u8g2_font_5x7_tr);
    u8g2_SetFontRefHeightText(&_display);
    u8g2_SetFontPosTop(&_display);
}

void DisplayRenderer::splash(const char* version, float lineFreqHz) {
    u8g2_FirstPage(&_display);
    do {
        u8g2_DrawStr(&_display, 20, 8, "Reflow Oven");
        u8g2_DrawStr(&_display, 20, 24, "Controller");
        u8g2_DrawHLine(&_display, 10, 36, 108);

        snprintf(_buf, sizeof(_buf), "Ver %s", version ? version : "?");
        u8g2_DrawStr(&_display, 10, 42, _buf);

        if (lineFreqHz > 0) {
            snprintf(_buf, sizeof(_buf), "%.0f Hz", lineFreqHz);
        } else {
            snprintf(_buf, sizeof(_buf), "Measuring...");
        }
        u8g2_DrawStr(&_display, 10, 54, _buf);
    } while (u8g2_NextPage(&_display));
}

void DisplayRenderer::clear() {
    u8g2_ClearBuffer(&_display);
    u8g2_SendBuffer(&_display);
}

void DisplayRenderer::drawMenuText(int line, const char* text, bool selected) {
    int y = line * 8;
    if (selected) {
        u8g2_DrawStr(&_display, 0, y, ">");
        u8g2_DrawStr(&_display, 8, y, text);
    } else {
        u8g2_DrawStr(&_display, 8, y, text);
    }
}

void DisplayRenderer::drawProgressBar(int x, int y, int w, int h, float pct) {
    if (pct < 0.0f) pct = 0.0f;
    if (pct > 1.0f) pct = 1.0f;
    u8g2_DrawFrame(&_display, x, y, w, h);
    int fill = (int)((w - 2) * pct);
    if (fill > 0) {
        u8g2_DrawBox(&_display, x + 1, y + 1, fill, h - 2);
    }
}

void DisplayRenderer::drawTempGraph(const float* history, uint32_t count, float setpoint) {
    int gx = 4;
    int gy = 16;
    int gw = DISPLAY_WIDTH - 8;
    int gh = DISPLAY_HEIGHT - 20;

    u8g2_DrawFrame(&_display, gx, gy, gw, gh);

    int spy = gy + (int)((PLOT_Y_MAX - setpoint) * gh / PLOT_Y_MAX);
    spy = (spy < gy) ? gy : (spy > gy + gh - 1) ? gy + gh - 1 : spy;
    for (int x = gx + 2; x < gx + gw - 2; x += 4) {
        u8g2_DrawPixel(&_display, x, spy);
    }

    if (count == 0) return;

    int maxPoints = (int)count;
    if (maxPoints > gw - 4) maxPoints = gw - 4;
    int startIdx = (int)count - maxPoints;

    if (maxPoints == 1) {
        float t = history[startIdx];
        int px = gx + gw / 2;
        int py = gy + (int)((PLOT_Y_MAX - t) * gh / PLOT_Y_MAX);
        py = (py < gy) ? gy : (py > gy + gh - 1) ? gy + gh - 1 : py;
        u8g2_DrawPixel(&_display, px, py);
        return;
    }

    for (int i = 0; i < maxPoints - 1; i++) {
        float t0 = history[startIdx + i];
        float t1 = history[startIdx + i + 1];
        int x0 = gx + 2 + i * (gw - 4) / (maxPoints - 1);
        int x1 = gx + 2 + (i + 1) * (gw - 4) / (maxPoints - 1);
        int y0 = gy + (int)((PLOT_Y_MAX - t0) * gh / PLOT_Y_MAX);
        int y1 = gy + (int)((PLOT_Y_MAX - t1) * gh / PLOT_Y_MAX);
        y0 = (y0 < gy) ? gy : (y0 > gy + gh - 1) ? gy + gh - 1 : y0;
        y1 = (y1 < gy) ? gy : (y1 > gy + gh - 1) ? gy + gh - 1 : y1;
        u8g2_DrawLine(&_display, x0, y0, x1, y1);
    }
}

void DisplayRenderer::renderMainMenu(int cursor) {
    u8g2_FirstPage(&_display);
    do {
        u8g2_DrawStr(&_display, 5, 0, "=== Menu ===");
        const char* items[] = {
            "Run Reflow",
            "Create Recipe",
            "Edit Recipe",
            "Bake Mode",
            "Settings",
            "Calibration"
        };
        int n = sizeof(items) / sizeof(items[0]);
        if (cursor < 0) cursor = 0;
        if (cursor >= n) cursor = n - 1;
        for (int i = 0; i < n; i++) {
            drawMenuText(1 + i, items[i], i == cursor);
        }
    } while (u8g2_NextPage(&_display));
}

void DisplayRenderer::renderRunning(const ThermalTelemetry& telemetry) {
    u8g2_FirstPage(&_display);
    do {
        const char* stageNames[] = {
            "IDLE", "PREHEAT", "SOAK", "REFLOW", "COOLDOWN", "DONE", "FAULT"
        };
        uint8_t si = (uint8_t)telemetry.stage;
        if (si > 6) si = 0;

        snprintf(_buf, sizeof(_buf), "%s  %02d:%02d",
                 stageNames[si],
                 (int)(telemetry.elapsedSec / 60),
                 (int)(telemetry.elapsedSec % 60));
        u8g2_DrawStr(&_display, 0, 0, _buf);

        snprintf(_buf, sizeof(_buf), "Set:%.0f%s Act:%.0f%s",
                 toDisplayUnit(telemetry.setpoint), unitSuffix(),
                 toDisplayUnit(telemetry.actualTemp), unitSuffix());
        u8g2_DrawStr(&_display, 0, 8, _buf);

        u8g2_DrawStr(&_display, 0, 18, "PWR");
        drawProgressBar(28, 18, 72, 7, telemetry.heaterPower);
        snprintf(_buf, sizeof(_buf), "%d%%", (int)(telemetry.heaterPower * 100));
        u8g2_DrawStr(&_display, 104, 18, _buf);

        u8g2_DrawStr(&_display, 0, 28, "FAN");
        drawProgressBar(28, 28, 72, 7, telemetry.coolingPower);
        snprintf(_buf, sizeof(_buf), "%d%%", (int)(telemetry.coolingPower * 100));
        u8g2_DrawStr(&_display, 104, 28, _buf);

        snprintf(_buf, sizeof(_buf), "TC1:%.0f%s TC2:%.0f%s",
                 toDisplayUnit(telemetry.temps.tc1), unitSuffix(),
                 toDisplayUnit(telemetry.temps.tc2), unitSuffix());
        u8g2_DrawStr(&_display, 0, 40, _buf);

        int sgx = 4;
        int sgy = 50;
        int sgw = DISPLAY_WIDTH - 8;
        int sgh = 4;
        u8g2_DrawFrame(&_display, sgx, sgy, sgw, sgh);
        float ratio = telemetry.actualTemp / PLOT_Y_MAX;
        if (ratio > 1.0f) ratio = 1.0f;
        if (ratio > 0.0f) {
            u8g2_DrawBox(&_display, sgx + 1, sgy + 1, (int)((sgw - 2) * ratio), sgh - 2);
        }
        float spRatio = telemetry.setpoint / PLOT_Y_MAX;
        if (spRatio > 1.0f) spRatio = 1.0f;
        int spx = sgx + 1 + (int)((sgw - 2) * spRatio);
        u8g2_DrawLine(&_display, spx, sgy, spx, sgy + sgh - 1);
    } while (u8g2_NextPage(&_display));
}

void DisplayRenderer::renderLivePlot(const float* history, uint32_t count, float setpoint) {
    u8g2_FirstPage(&_display);
    do {
        snprintf(_buf, sizeof(_buf), "Plot SP:%.0f%s", toDisplayUnit(setpoint), unitSuffix());
        u8g2_DrawStr(&_display, 0, 0, _buf);
        drawTempGraph(history, count, setpoint);
    } while (u8g2_NextPage(&_display));
}

void DisplayRenderer::renderRecipeList(const ReflowRecipe* recipes, int count, int cursor) {
    u8g2_FirstPage(&_display);
    do {
        u8g2_DrawStr(&_display, 5, 0, "=== Recipes ===");
        if (count <= 0) {
            u8g2_DrawStr(&_display, 8, 16, "No recipes");
            u8g2_DrawStr(&_display, 8, 24, "defined yet.");
        } else {
            int start = 0;
            int maxVisible = 7;
            if (cursor >= maxVisible) start = cursor - maxVisible + 1;
            int end = start + maxVisible;
            if (end > count) end = count;
            for (int i = start; i < end; i++) {
                drawMenuText(1 + i - start, recipes[i].name, i == cursor);
            }
        }
    } while (u8g2_NextPage(&_display));
}

void DisplayRenderer::renderRecipeEdit(const ReflowRecipe& recipe, int field) {
    u8g2_FirstPage(&_display);
    do {
        u8g2_DrawStr(&_display, 5, 0, "=Edit Recipe=");

        snprintf(_buf, sizeof(_buf), "%sPreheat: %.0f%s", field == 0 ? ">" : " ",
                 toDisplayUnit(recipe.preheatTemp), unitSuffix());
        u8g2_DrawStr(&_display, 0, 8, _buf);

        snprintf(_buf, sizeof(_buf), "%sSoak:   %.0f%s", field == 1 ? ">" : " ",
                 toDisplayUnit(recipe.soakTemp), unitSuffix());
        u8g2_DrawStr(&_display, 0, 16, _buf);

        snprintf(_buf, sizeof(_buf), "%sPeak:   %.0f%s", field == 2 ? ">" : " ",
                 toDisplayUnit(recipe.peakTemp), unitSuffix());
        u8g2_DrawStr(&_display, 0, 24, _buf);

        snprintf(_buf, sizeof(_buf), "%sRamp:   %ds", field == 3 ? ">" : " ", recipe.rampTimeSec);
        u8g2_DrawStr(&_display, 0, 32, _buf);

        snprintf(_buf, sizeof(_buf), "%sSoak:   %ds", field == 4 ? ">" : " ", recipe.soakTimeSec);
        u8g2_DrawStr(&_display, 0, 40, _buf);

        snprintf(_buf, sizeof(_buf), "%sReflow: %ds", field == 5 ? ">" : " ", recipe.reflowTimeSec);
        u8g2_DrawStr(&_display, 0, 48, _buf);

        if (field == 6) {
            snprintf(_buf, sizeof(_buf), ">Hold:%ds Tune:%s", recipe.holdTimeSec,
                     recipe.fineTuneEnabled ? "ON" : "OFF");
        } else if (field == 7) {
            snprintf(_buf, sizeof(_buf), " Hold:%ds >Tune:%s", recipe.holdTimeSec,
                     recipe.fineTuneEnabled ? "ON" : "OFF");
        } else {
            snprintf(_buf, sizeof(_buf), " Hold:%ds Tune:%s", recipe.holdTimeSec,
                     recipe.fineTuneEnabled ? "ON" : "OFF");
        }
        u8g2_DrawStr(&_display, 0, 56, _buf);
    } while (u8g2_NextPage(&_display));
}

void DisplayRenderer::renderSettings(const SystemSettings& settings, int field) {
    u8g2_FirstPage(&_display);
    do {
        u8g2_DrawStr(&_display, 5, 0, "=== Settings ===");

        snprintf(_buf, sizeof(_buf), "%sUnits: %s",
                 field == 0 ? ">" : " ",
                 settings.useFahrenheit ? "Fahrenheit" : "Celsius");
        u8g2_DrawStr(&_display, 0, 12, _buf);

        snprintf(_buf, sizeof(_buf), "%sMaxBake:%dmin",
                 field == 1 ? ">" : " ",
                 settings.maxBakeMin);
        u8g2_DrawStr(&_display, 0, 20, _buf);

        u8g2_DrawStr(&_display, 0, 36, "[L] Back  [R] Toggle");
    } while (u8g2_NextPage(&_display));
}

void DisplayRenderer::renderBakeSetup(const BakeSettings& bake, int field) {
    u8g2_FirstPage(&_display);
    do {
        u8g2_DrawStr(&_display, 5, 0, "=== Bake Mode ===");

        snprintf(_buf, sizeof(_buf), "%sTemp: %.0f%s",
                 field == 0 ? ">" : " ",
                 toDisplayUnit((float)bake.temperature), unitSuffix());
        u8g2_DrawStr(&_display, 0, 12, _buf);

        snprintf(_buf, sizeof(_buf), "%sDur:  %dmin",
                 field == 1 ? ">" : " ",
                 bake.durationMin);
        u8g2_DrawStr(&_display, 0, 20, _buf);

        u8g2_DrawStr(&_display, 0, 36, "[L] Back [S] Start");
    } while (u8g2_NextPage(&_display));
}

void DisplayRenderer::renderCalibration() {
    u8g2_FirstPage(&_display);
    do {
        u8g2_DrawStr(&_display, 5, 0, "= Calibration =");
        u8g2_DrawStr(&_display, 0, 12, "Connect ref probe");
        u8g2_DrawStr(&_display, 0, 20, "to TC1, press S");
        u8g2_DrawStr(&_display, 0, 28, "to zero offset.");
        u8g2_DrawStr(&_display, 0, 44, "[Any key] to exit");
    } while (u8g2_NextPage(&_display));
}

void DisplayRenderer::renderCalibSubmenu(int cursor) {
    u8g2_FirstPage(&_display);
    do {
        u8g2_DrawStr(&_display, 5, 0, "= Calibration =");
        const char* items[] = {
            "TC Offset Info",
            "Profile Cal Test",
            "View Cal Info"
        };
        for (int i = 0; i < 3; i++) {
            drawMenuText(2 + i, items[i], i == cursor);
        }
    } while (u8g2_NextPage(&_display));
}

void DisplayRenderer::renderCalibTestRunning(bool isCooling, int zone, float temp, float maxRate, float curRate,
                                              uint32_t elapsedSec, const float* history, uint32_t historyCount) {
    u8g2_FirstPage(&_display);
    do {
        const char* phaseStr = isCooling ? "COOL" : "HEAT";
        const char* pwrStr = isCooling ? "100%FAN" : "100%PWR";

        snprintf(_buf, sizeof(_buf), "=%s Z%d/5 %s %.0f%s=",
                 phaseStr, zone, pwrStr, toDisplayUnit(temp), unitSuffix());
        u8g2_DrawStr(&_display, 2, 0, _buf);

        float dispCur = toDisplayRate(curRate);
        float dispMax = toDisplayRate(maxRate);
        if (dispCur >= 0) {
            snprintf(_buf, sizeof(_buf), "R:+%.1f%s M:+%.1f%s", dispCur, rateSuffix(), dispMax, rateSuffix());
        } else {
            snprintf(_buf, sizeof(_buf), "R:%.1f%s M:%.1f%s", dispCur, rateSuffix(), dispMax, rateSuffix());
        }
        u8g2_DrawStr(&_display, 0, 10, _buf);

        snprintf(_buf, sizeof(_buf), "%s  %02d:%02d", phaseStr, (int)(elapsedSec / 60), (int)(elapsedSec % 60));
        u8g2_DrawStr(&_display, 0, 20, _buf);
        u8g2_DrawStr(&_display, 92, 20, "[S]Stop");

        // Mini temperature-vs-time graph
        int gx = 4;
        int gy = 30;
        int gw = DISPLAY_WIDTH - 8;
        int gh = 24;
        u8g2_DrawFrame(&_display, gx, gy, gw, gh);

        if (historyCount > 0) {
            uint32_t maxPoints = historyCount;
            if (maxPoints > (uint32_t)(gw - 4)) maxPoints = gw - 4;
            uint32_t startIdx = historyCount - maxPoints;

            for (uint32_t i = 0; i < maxPoints - 1 && (i + 1) < maxPoints; i++) {
                float t0 = history[startIdx + i];
                float t1 = history[startIdx + i + 1];
                int x0 = gx + 2 + (int)(i * (gw - 4) / (maxPoints - 1));
                int x1 = gx + 2 + (int)((i + 1) * (gw - 4) / (maxPoints - 1));
                int y0 = gy + gh - 1 - (int)((t0 / PLOT_Y_MAX) * (gh - 2));
                int y1 = gy + gh - 1 - (int)((t1 / PLOT_Y_MAX) * (gh - 2));
                if (y0 < gy + 1) y0 = gy + 1;
                if (y1 < gy + 1) y1 = gy + 1;
                if (y0 > gy + gh - 2) y0 = gy + gh - 2;
                if (y1 > gy + gh - 2) y1 = gy + gh - 2;
                u8g2_DrawLine(&_display, x0, y0, x1, y1);
            }
        }

        snprintf(_buf, sizeof(_buf), "0%c %d%c", unitSuffix()[0], (int)toDisplayUnit((float)PLOT_Y_MAX), unitSuffix()[0]);
        u8g2_DrawStr(&_display, gx + 1, gy + gh + 2, _buf);
    } while (u8g2_NextPage(&_display));
}

void DisplayRenderer::renderCalibTestResults(int oldRamp, int newRamp, int oldReflow, int newReflow,
                                              const float* heatRates, const float* coolRates,
                                              uint32_t heatDeadTimeMs, uint32_t coolDeadTimeMs,
                                              int recipeCount) {
    u8g2_FirstPage(&_display);
    do {
        u8g2_DrawStr(&_display, 5, 0, "= Cal Results =");
        u8g2_DrawStr(&_display, 0, 9, "Heat:");
        snprintf(_buf, sizeof(_buf), "%.1f %.1f %.1f C/s",
                 heatRates[0], heatRates[1], heatRates[2]);
        u8g2_DrawStr(&_display, 30, 9, _buf);
        snprintf(_buf, sizeof(_buf), "%.1f %.1f C/s", heatRates[3], heatRates[4]);
        u8g2_DrawStr(&_display, 30, 17, _buf);

        u8g2_DrawStr(&_display, 0, 26, "Cool:");
        snprintf(_buf, sizeof(_buf), "%.1f %.1f %.1f C/s",
                 coolRates[0], coolRates[1], coolRates[2]);
        u8g2_DrawStr(&_display, 30, 26, _buf);
        snprintf(_buf, sizeof(_buf), "%.1f %.1f C/s", coolRates[3], coolRates[4]);
        u8g2_DrawStr(&_display, 30, 34, _buf);

        snprintf(_buf, sizeof(_buf), "DT: H%lums C%lums", (unsigned long)heatDeadTimeMs, (unsigned long)coolDeadTimeMs);
        u8g2_DrawStr(&_display, 0, 43, _buf);

        snprintf(_buf, sizeof(_buf), "R:%ds Rf:%ds", newRamp, newReflow);
        u8g2_DrawStr(&_display, 0, 52, _buf);

        snprintf(_buf, sizeof(_buf), "Applied to %d recipes", recipeCount);
        u8g2_DrawStr(&_display, 0, 61, _buf);
        u8g2_DrawStr(&_display, 80, 61, "[S]Save");
    } while (u8g2_NextPage(&_display));
}

void DisplayRenderer::renderCalibInfo(int recipeIdx, int recipeCount, const ReflowRecipe* recipes,
                                       const float* heatRates, const float* coolRates,
                                       uint32_t heatDeadTimeMs, uint32_t coolDeadTimeMs,
                                       int page) {
    u8g2_FirstPage(&_display);
    do {
        if (page == 1) {
            u8g2_DrawStr(&_display, 5, 0, "== Cal Data ==");
            u8g2_DrawStr(&_display, 0, 9, "Heat(C/s):");
            snprintf(_buf, sizeof(_buf), "%.1f %.1f %.1f", heatRates[0], heatRates[1], heatRates[2]);
            u8g2_DrawStr(&_display, 0, 17, _buf);
            snprintf(_buf, sizeof(_buf), "%.1f %.1f", heatRates[3], heatRates[4]);
            u8g2_DrawStr(&_display, 0, 25, _buf);
            u8g2_DrawStr(&_display, 0, 34, "Cool(C/s):");
            snprintf(_buf, sizeof(_buf), "%.1f %.1f %.1f", coolRates[0], coolRates[1], coolRates[2]);
            u8g2_DrawStr(&_display, 0, 42, _buf);
            snprintf(_buf, sizeof(_buf), "%.1f %.1f", coolRates[3], coolRates[4]);
            u8g2_DrawStr(&_display, 0, 50, _buf);
            snprintf(_buf, sizeof(_buf), "DT H:%lums C:%lums", (unsigned long)heatDeadTimeMs, (unsigned long)coolDeadTimeMs);
            u8g2_DrawStr(&_display, 0, 59, _buf);
            u8g2_DrawStr(&_display, 105, 0, "S:Nxt");
        } else if (page == 2) {
            u8g2_DrawStr(&_display, 5, 0, "= Recipe Temps =");
            u8g2_DrawStr(&_display, 110, 0, "S:Nxt");
            if (recipeIdx < recipeCount) {
                const ReflowRecipe& r = recipes[recipeIdx];
                snprintf(_buf, sizeof(_buf), "R%d/%d:", recipeIdx + 1, recipeCount);
                size_t prefixLen = strlen(_buf);
                snprintf(_buf + prefixLen, sizeof(_buf) - prefixLen, "%.*s",
                         (int)(sizeof(_buf) - prefixLen - 1), r.name);
                u8g2_DrawStr(&_display, 0, 9, _buf);
                snprintf(_buf, sizeof(_buf), "Pre:%.0f%s Soak:%.0f%s",
                         toDisplayUnit(r.preheatTemp), unitSuffix(),
                         toDisplayUnit(r.soakTemp), unitSuffix());
                u8g2_DrawStr(&_display, 0, 18, _buf);
                snprintf(_buf, sizeof(_buf), "Peak:%.0f%s",
                         toDisplayUnit(r.peakTemp), unitSuffix());
                u8g2_DrawStr(&_display, 0, 27, _buf);
                snprintf(_buf, sizeof(_buf), "Ramp:%ds Soak:%ds", r.rampTimeSec, r.soakTimeSec);
                u8g2_DrawStr(&_display, 0, 36, _buf);
                snprintf(_buf, sizeof(_buf), "Reflow:%ds Hold:%ds", r.reflowTimeSec, r.holdTimeSec);
                u8g2_DrawStr(&_display, 0, 44, _buf);
            } else {
                u8g2_DrawStr(&_display, 0, 18, "No recipes loaded");
            }
            u8g2_DrawStr(&_display, 0, 59, "^v:Rec <:Back");
        } else {
            u8g2_DrawStr(&_display, 5, 0, "= Cal PID Info =");
            u8g2_DrawStr(&_display, 105, 0, "S:Nxt");
            if (recipeIdx < recipeCount) {
                const ReflowRecipe& r = recipes[recipeIdx];
                snprintf(_buf, sizeof(_buf), "R%d/%d:", recipeIdx + 1, recipeCount);
                size_t prefixLen = strlen(_buf);
                snprintf(_buf + prefixLen, sizeof(_buf) - prefixLen, "%.*s",
                         (int)(sizeof(_buf) - prefixLen - 1), r.name);
                u8g2_DrawStr(&_display, 0, 9, _buf);
                for (int z = 0; z < PID_ZONES; z++) {
                    snprintf(_buf, sizeof(_buf),
                             "Z%d H:%.1f/%.2f/%.2f C:%.1f/%.2f/%.2f",
                             z,
                             r.heaterGains[z].kp, r.heaterGains[z].ki, r.heaterGains[z].kd,
                             r.coolingGains[z].kp, r.coolingGains[z].ki, r.coolingGains[z].kd);
                    u8g2_DrawStr(&_display, 0, (int)(18 + z * 8), _buf);
                }
            } else {
                u8g2_DrawStr(&_display, 0, 18, "No recipes loaded");
            }
            u8g2_DrawStr(&_display, 0, 59, "^v:Rec <:Back");
        }
    } while (u8g2_NextPage(&_display));
}

void DisplayRenderer::renderEstop() {
    u8g2_FirstPage(&_display);
    do {
        u8g2_DrawStr(&_display, 5, 8, "EMERGENCY STOP");
        u8g2_DrawHLine(&_display, 0, 20, DISPLAY_WIDTH);
        u8g2_DrawStr(&_display, 5, 28, "Over-temp / Fault");
        u8g2_DrawStr(&_display, 5, 44, "Press SELECT to");
        u8g2_DrawStr(&_display, 5, 52, "acknowledge.");
    } while (u8g2_NextPage(&_display));
}

void DisplayRenderer::renderMessage(const char* msg) {
    u8g2_FirstPage(&_display);
    do {
        u8g2_DrawStr(&_display, 0, 0, msg);
    } while (u8g2_NextPage(&_display));
}
