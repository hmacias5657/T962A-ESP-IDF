#include <stdio.h>
#include <string.h>
#include <cmath>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "driver/gpio.h"

#include "Config.h"
#include "SharedData.h"
#include "BresenhamPID.h"
#include "TemperatureReader.h"
#include "ProfileEngine.h"
#include "AITuner.h"
#include "DisplayRenderer.h"
#include "ButtonDebouncer.h"
#include "Buzzer.h"

static const char* TAG = "OvenCtrl";

// Global objects
static BresenhamPID g_pid;
static TemperatureReader g_tempReader;
static ProfileEngine g_profile;
static AITuner g_tuner;
static DisplayRenderer g_display;
static ButtonDebouncer g_buttons;
static Buzzer g_buzzer;

// Shared state
static ThermalTelemetry g_telemetry;
static SystemSettings g_settings;
static ReflowRecipe g_recipes[MAX_RECIPES];
static int32_t g_recipeCount = 0;
static SystemState g_systemState = STATE_MENU;
static MenuScreen g_menuScreen = MENU_MAIN;
static float g_tempHistory[480];
static uint32_t g_tempHistoryCount = 0;
static uint32_t g_calibSeq = 0;

// Navigation state
static int s_menuCursor = 0;
static int s_recipeCursor = 0;
static int s_recipeField = 0;
static int s_settingsField = 0;
static int s_bakeField = 0;
static int s_listAction = 0;
static ReflowRecipe s_editRecipe = ReflowRecipe();
static BakeSettings s_editBake = BakeSettings();
static bool s_showPlot = false;

// Calibration test state
static int s_calibMenuCursor = 0;
static int s_calibPhase = 0;
static int s_calibZone = 0;
static float s_calibMaxHeatRates[PID_ZONES];
static float s_calibMaxCoolRates[PID_ZONES];
static bool s_calibZoneVisited[PID_ZONES];
static float s_calibCurrentRate = 0.0f;
static float s_calibPrevTemp = 0.0f;
static ReflowRecipe s_calibRecipe;
static int s_calibRecipeIdx = 0;
static uint32_t s_calibElapsedSec = 0;
static uint32_t s_calibPhaseStartMs = 0;
static bool s_calibDeadTimeMeasured = false;
static uint32_t s_calibHeatDeadTimeMs = 0;
static uint32_t s_calibCoolDeadTimeMs = 0;
static bool s_calibDone = false;
static int s_calibOldRamp = 0;
static int s_calibNewRamp = 0;
static int s_calibOldReflow = 0;
static int s_calibNewReflow = 0;

// Calibration info screen state
static int s_calibInfoRecipeIdx = 0;
static int s_calibInfoPage = 0;

// Inter-task communication
static QueueHandle_t g_telemetryQueue = NULL;
static SemaphoreHandle_t g_i2cMutex = NULL;

// Zone fine-tuning tracking
static ZoneMetrics g_zoneMetrics;
static ReflowStage g_prevStage = STAGE_IDLE;
static int g_prevZone = -1;

static void computeZieglerNichols(float deadTimeSec, float heatingRate,
                                   PidGains& outGains) {
    if (deadTimeSec < 0.1f) deadTimeSec = 0.1f;
    if (heatingRate < 0.01f) heatingRate = 0.01f;
    float tau = 1.0f / heatingRate;
    outGains.kp = 1.2f * tau / deadTimeSec;
    outGains.ki = outGains.kp / (2.0f * deadTimeSec);
    outGains.kd = outGains.kp * 0.5f * deadTimeSec;
    outGains.kp = fmaxf(FINE_TUNE_KP_MIN, fminf(FINE_TUNE_KP_MAX, outGains.kp));
    outGains.ki = fmaxf(FINE_TUNE_KI_MIN, fminf(FINE_TUNE_KI_MAX, outGains.ki));
    outGains.kd = fmaxf(FINE_TUNE_KD_MIN, fminf(FINE_TUNE_KD_MAX, outGains.kd));
}

static void applyCalibOverrideToRecipe(ReflowRecipe& recipe) {
    float hDt = (s_calibHeatDeadTimeMs > 0) ? s_calibHeatDeadTimeMs / 1000.0f : 0.5f;
    for (int z = 0; z < PID_ZONES; z++) {
        float heatRate = (s_calibMaxHeatRates[z] > 0.01f) ? s_calibMaxHeatRates[z] : 0.5f;
        computeZieglerNichols(hDt, heatRate, recipe.heaterGains[z]);
        computeZieglerNichols(hDt, s_calibMaxCoolRates[z] > 0.01f ? fabsf(s_calibMaxCoolRates[z]) : 0.3f,
                              recipe.coolingGains[z]);
    }
    recipe.tuneSeq = g_calibSeq;
}

static void ensureRecipeGainsFromCalib(ReflowRecipe& recipe) {
    if (recipe.tuneSeq < g_calibSeq) {
        applyCalibOverrideToRecipe(recipe);
    }
}

// NVS helpers
static void loadSettings() {
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) != ESP_OK) return;

    uint8_t useF = 0;
    nvs_get_u8(nvs, "useF", &useF);
    g_settings.useFahrenheit = useF != 0;

    int32_t tc1Off = 0, tc2Off = 0;
    nvs_get_i32(nvs, "tc1Off", &tc1Off);
    nvs_get_i32(nvs, "tc2Off", &tc2Off);
    g_settings.tc1Offset = tc1Off / 100.0f;
    g_settings.tc2Offset = tc2Off / 100.0f;

    int32_t maxBake = 300;
    nvs_get_i32(nvs, "maxBakeMin", &maxBake);
    g_settings.maxBakeMin = (uint16_t)maxBake;

    int32_t calibSeq = 0;
    nvs_get_i32(nvs, "calibSeq", &calibSeq);
    g_calibSeq = (uint32_t)calibSeq;

    nvs_get_i32(nvs, "recipeCount", &g_recipeCount);
    if (g_recipeCount > MAX_RECIPES) g_recipeCount = MAX_RECIPES;

    for (int r = 0; r < g_recipeCount; r++) {
        char key[24];
        snprintf(key, sizeof(key), "r%d_name", r);
        size_t len = MAX_RECIPE_NAME_LEN;
        nvs_get_str(nvs, key, g_recipes[r].name, &len);

        g_recipes[r].fineTuneEnabled = true;
        g_recipes[r].tuneSeq = 0;

        snprintf(key, sizeof(key), "r%d_tuneEn", r);
        uint8_t tuneEn = 1;
        nvs_get_u8(nvs, key, &tuneEn);
        g_recipes[r].fineTuneEnabled = (tuneEn != 0);

        snprintf(key, sizeof(key), "r%d_tuneSeq", r);
        int32_t tuneSeq = 0;
        nvs_get_i32(nvs, key, &tuneSeq);
        g_recipes[r].tuneSeq = (uint32_t)tuneSeq;

        int32_t val;
        snprintf(key, sizeof(key), "r%d_pre", r);
        if (nvs_get_i32(nvs, key, &val) == ESP_OK) g_recipes[r].preheatTemp = val / 100.0f;
        snprintf(key, sizeof(key), "r%d_soak", r);
        if (nvs_get_i32(nvs, key, &val) == ESP_OK) g_recipes[r].soakTemp = val / 100.0f;
        snprintf(key, sizeof(key), "r%d_peak", r);
        if (nvs_get_i32(nvs, key, &val) == ESP_OK) g_recipes[r].peakTemp = val / 100.0f;
        snprintf(key, sizeof(key), "r%d_ramp", r);
        if (nvs_get_i32(nvs, key, &val) == ESP_OK) g_recipes[r].rampTimeSec = (uint16_t)val;
        snprintf(key, sizeof(key), "r%d_refl", r);
        if (nvs_get_i32(nvs, key, &val) == ESP_OK) g_recipes[r].reflowTimeSec = (uint16_t)val;
        snprintf(key, sizeof(key), "r%d_hold", r);
        if (nvs_get_i32(nvs, key, &val) == ESP_OK) g_recipes[r].holdTimeSec = (uint16_t)val;

        for (int z = 0; z < PID_ZONES; z++) {
            snprintf(key, sizeof(key), "r%d_z%d_kp", r, z);
            if (nvs_get_i32(nvs, key, &val) == ESP_OK) g_recipes[r].heaterGains[z].kp = val / 1000.0f;
            snprintf(key, sizeof(key), "r%d_z%d_ki", r, z);
            if (nvs_get_i32(nvs, key, &val) == ESP_OK) g_recipes[r].heaterGains[z].ki = val / 1000.0f;
            snprintf(key, sizeof(key), "r%d_z%d_kd", r, z);
            if (nvs_get_i32(nvs, key, &val) == ESP_OK) g_recipes[r].heaterGains[z].kd = val / 1000.0f;
            snprintf(key, sizeof(key), "r%d_z%d_ckp", r, z);
            if (nvs_get_i32(nvs, key, &val) == ESP_OK) g_recipes[r].coolingGains[z].kp = val / 1000.0f;
            snprintf(key, sizeof(key), "r%d_z%d_cki", r, z);
            if (nvs_get_i32(nvs, key, &val) == ESP_OK) g_recipes[r].coolingGains[z].ki = val / 1000.0f;
            snprintf(key, sizeof(key), "r%d_z%d_ckd", r, z);
            if (nvs_get_i32(nvs, key, &val) == ESP_OK) g_recipes[r].coolingGains[z].kd = val / 1000.0f;
        }

        ensureRecipeGainsFromCalib(g_recipes[r]);
    }

    nvs_close(nvs);
}

static void saveSettings() {
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs) != ESP_OK) return;

    nvs_set_u8(nvs, "useF", g_settings.useFahrenheit ? 1 : 0);
    nvs_set_i32(nvs, "tc1Off", (int32_t)(g_settings.tc1Offset * 100));
    nvs_set_i32(nvs, "tc2Off", (int32_t)(g_settings.tc2Offset * 100));
    nvs_set_i32(nvs, "maxBakeMin", g_settings.maxBakeMin);
    nvs_set_i32(nvs, "recipeCount", g_recipeCount);
    nvs_set_i32(nvs, "calibSeq", (int32_t)g_calibSeq);

    for (int r = 0; r < g_recipeCount; r++) {
        char key[24];
        snprintf(key, sizeof(key), "r%d_name", r);
        nvs_set_str(nvs, key, g_recipes[r].name);
        snprintf(key, sizeof(key), "r%d_pre", r);
        nvs_set_i32(nvs, key, (int32_t)(g_recipes[r].preheatTemp * 100));
        snprintf(key, sizeof(key), "r%d_soak", r);
        nvs_set_i32(nvs, key, (int32_t)(g_recipes[r].soakTemp * 100));
        snprintf(key, sizeof(key), "r%d_peak", r);
        nvs_set_i32(nvs, key, (int32_t)(g_recipes[r].peakTemp * 100));
        snprintf(key, sizeof(key), "r%d_ramp", r);
        nvs_set_i32(nvs, key, g_recipes[r].rampTimeSec);
        snprintf(key, sizeof(key), "r%d_refl", r);
        nvs_set_i32(nvs, key, g_recipes[r].reflowTimeSec);
        snprintf(key, sizeof(key), "r%d_hold", r);
        nvs_set_i32(nvs, key, g_recipes[r].holdTimeSec);

        snprintf(key, sizeof(key), "r%d_tuneEn", r);
        nvs_set_u8(nvs, key, g_recipes[r].fineTuneEnabled ? 1 : 0);
        snprintf(key, sizeof(key), "r%d_tuneSeq", r);
        nvs_set_i32(nvs, key, (int32_t)g_recipes[r].tuneSeq);

        for (int z = 0; z < PID_ZONES; z++) {
            snprintf(key, sizeof(key), "r%d_z%d_kp", r, z);
            nvs_set_i32(nvs, key, (int32_t)(g_recipes[r].heaterGains[z].kp * 1000));
            snprintf(key, sizeof(key), "r%d_z%d_ki", r, z);
            nvs_set_i32(nvs, key, (int32_t)(g_recipes[r].heaterGains[z].ki * 1000));
            snprintf(key, sizeof(key), "r%d_z%d_kd", r, z);
            nvs_set_i32(nvs, key, (int32_t)(g_recipes[r].heaterGains[z].kd * 1000));
            snprintf(key, sizeof(key), "r%d_z%d_ckp", r, z);
            nvs_set_i32(nvs, key, (int32_t)(g_recipes[r].coolingGains[z].kp * 1000));
            snprintf(key, sizeof(key), "r%d_z%d_cki", r, z);
            nvs_set_i32(nvs, key, (int32_t)(g_recipes[r].coolingGains[z].ki * 1000));
            snprintf(key, sizeof(key), "r%d_z%d_ckd", r, z);
            nvs_set_i32(nvs, key, (int32_t)(g_recipes[r].coolingGains[z].kd * 1000));
        }
    }

    nvs_commit(nvs);
    nvs_close(nvs);
}

// Calibration test helpers
static int getZoneForTemp(float temp) {
    if (temp < CALIB_ZONE_0_MAX) return 0;
    if (temp < CALIB_ZONE_1_MAX) return 1;
    if (temp < CALIB_ZONE_2_MAX) return 2;
    if (temp < CALIB_ZONE_3_MAX) return 3;
    return 4;
}

static void loadCalibData(float* heatRates, float* coolRates,
                           uint32_t& heatDeadTimeMs, uint32_t& coolDeadTimeMs) {
    nvs_handle_t nvs;
    if (nvs_open(NVS_CALIB_NAMESPACE, NVS_READONLY, &nvs) != ESP_OK) return;
    for (int z = 0; z < PID_ZONES; z++) {
        char key[16];
        int32_t val = 0;
        snprintf(key, sizeof(key), "z%d_rate", z);
        if (nvs_get_i32(nvs, key, &val) == ESP_OK) heatRates[z] = val / 1000.0f;
        else heatRates[z] = 0.0f;
        snprintf(key, sizeof(key), "z%d_crate", z);
        if (nvs_get_i32(nvs, key, &val) == ESP_OK) coolRates[z] = val / 1000.0f;
        else coolRates[z] = 0.0f;
    }
    int32_t dt = 0;
    if (nvs_get_i32(nvs, "heatDTms", &dt) == ESP_OK) heatDeadTimeMs = (uint32_t)dt;
    else heatDeadTimeMs = 0;
    dt = 0;
    if (nvs_get_i32(nvs, "coolDTms", &dt) == ESP_OK) coolDeadTimeMs = (uint32_t)dt;
    else coolDeadTimeMs = 0;
    nvs_close(nvs);
}

static void saveCalibData(const float* heatRates, const float* coolRates,
                           uint32_t heatDeadTimeMs, uint32_t coolDeadTimeMs) {
    nvs_handle_t nvs;
    if (nvs_open(NVS_CALIB_NAMESPACE, NVS_READWRITE, &nvs) != ESP_OK) return;
    for (int z = 0; z < PID_ZONES; z++) {
        char key[16];
        snprintf(key, sizeof(key), "z%d_rate", z);
        nvs_set_i32(nvs, key, (int32_t)(heatRates[z] * 1000));
        snprintf(key, sizeof(key), "z%d_crate", z);
        nvs_set_i32(nvs, key, (int32_t)(coolRates[z] * 1000));
    }
    nvs_set_i32(nvs, "heatDTms", (int32_t)heatDeadTimeMs);
    nvs_set_i32(nvs, "coolDTms", (int32_t)coolDeadTimeMs);
    nvs_commit(nvs);
    nvs_close(nvs);
}

static void adjustRecipeRampReflow(const ReflowRecipe& recipe, const float* heatRates,
                                    int& outNewRamp, int& outNewReflow) {
    outNewRamp = recipe.rampTimeSec;
    outNewReflow = recipe.reflowTimeSec;
    float soak = recipe.soakTemp;
    float peak = recipe.peakTemp;
    float ambient = 25.0f;
    int soakZone = getZoneForTemp(soak);
    int peakZone = getZoneForTemp(peak);

    float rampSum = 0.0f;
    int rampCnt = 0;
    for (int z = 0; z <= soakZone && z < PID_ZONES; z++) {
        if (heatRates[z] > 0.0f) { rampSum += heatRates[z]; rampCnt++; }
    }
    if (rampCnt > 0) {
        float avg = rampSum / rampCnt;
        float range = soak - ambient;
        if (range > 0.0f && avg > 0.0f) {
            outNewRamp = (int)(range / avg * CALIB_SAFETY_MARGIN + 0.5f);
            if (outNewRamp < 5) outNewRamp = 5;
        }
    }

    float reflSum = 0.0f;
    int reflCnt = 0;
    for (int z = soakZone; z <= peakZone && z < PID_ZONES; z++) {
        if (heatRates[z] > 0.0f) { reflSum += heatRates[z]; reflCnt++; }
    }
    if (reflCnt > 0) {
        float avg = reflSum / reflCnt;
        float range = peak - soak;
        if (range > 0.0f && avg > 0.0f) {
            outNewReflow = (int)(range / avg * CALIB_SAFETY_MARGIN + 0.5f);
            if (outNewReflow < 5) outNewReflow = 5;
        }
    }
}

static void applyCalibToAllRecipes(const float* heatRates) {
    int appliedCount = 0;
    for (int i = 0; i < g_recipeCount; i++) {
        int newRamp, newReflow;
        adjustRecipeRampReflow(g_recipes[i], heatRates, newRamp, newReflow);
        g_recipes[i].rampTimeSec = (uint16_t)newRamp;
        g_recipes[i].reflowTimeSec = (uint16_t)newReflow;
        applyCalibOverrideToRecipe(g_recipes[i]);
        appliedCount++;
    }
    if (appliedCount > 0) {
        ESP_LOGI(TAG, "Calibrated %d recipes (seq=%lu)", appliedCount, (unsigned long)g_calibSeq);
    }
}

// Core 0 task: Reflow control loop (~2.13s period)
static void controlTask(void* pvParameters) {
    (void)pvParameters;
    TickType_t lastWake = xTaskGetTickCount();
    g_prevStage = STAGE_IDLE;
    g_prevZone = -1;

    while (1) {
        if (g_systemState == STATE_CALIB_TEST) {
            if (xSemaphoreTake(g_i2cMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                TemperatureData temps = g_tempReader.readSensors();
                xSemaphoreGive(g_i2cMutex);

                g_telemetry.temps = temps;
                float dt = g_pid.getPidIntervalMs() / 1000.0f;

                if (s_calibElapsedSec == 0) {
                    s_calibPrevTemp = temps.avg;
                    s_calibPhaseStartMs = (uint32_t)(esp_timer_get_time() / 1000);
                    s_calibDeadTimeMeasured = false;
                }

                if (temps.fault || temps.avg > OVERTEMP_C) {
                    g_pid.emergencyStop();
                    g_buzzer.play(BUZZER_ESTOP_ALARM);
                    g_systemState = STATE_ESTOP;
                    g_telemetry.stage = STAGE_FAULT;
                    ESP_LOGE(TAG, "CALIB FAULT: avg=%.1f delta=%.1f", temps.avg, temps.delta);
                } else {
                    s_calibElapsedSec += (uint32_t)(g_pid.getPidIntervalMs() / 1000);

                    if (s_calibPhase == 0) {
                        g_pid.setRawHeaterPower(1.0f);

                        float dT = temps.avg - s_calibPrevTemp;
                        s_calibCurrentRate = (dt > 0.0f) ? dT / dt : 0.0f;
                        s_calibPrevTemp = temps.avg;

                        if (!s_calibDeadTimeMeasured && s_calibCurrentRate > CALIB_DT_THRESHOLD) {
                            uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
                            s_calibHeatDeadTimeMs = now - s_calibPhaseStartMs;
                            s_calibDeadTimeMeasured = true;
                            ESP_LOGI(TAG, "Heat dead time: %lu ms", (unsigned long)s_calibHeatDeadTimeMs);
                        }

                        int zone = getZoneForTemp(temps.avg);
                        if (zone >= PID_ZONES) zone = PID_ZONES - 1;
                        s_calibZoneVisited[zone] = true;
                        s_calibZone = zone;

                        if (s_calibCurrentRate > s_calibMaxHeatRates[zone]) {
                            s_calibMaxHeatRates[zone] = s_calibCurrentRate;
                        }

                        bool allVisited = true;
                        for (int z = 0; z < PID_ZONES; z++) {
                            if (!s_calibZoneVisited[z]) { allVisited = false; break; }
                        }

                        float maxPeak = s_calibRecipe.peakTemp + 10.0f;
                        if (temps.avg >= maxPeak || s_calibElapsedSec >= CALIB_TEST_TIMEOUT_SECS) {
                            allVisited = true;
                        }

                        if (allVisited) {
                            s_calibPhase = 1;
                            s_calibPhaseStartMs = (uint32_t)(esp_timer_get_time() / 1000);
                            s_calibDeadTimeMeasured = false;
                            for (int z = 0; z < PID_ZONES; z++) {
                                s_calibZoneVisited[z] = false;
                            }
                            s_calibPrevTemp = temps.avg;
                            ESP_LOGI(TAG, "Heating phase complete, starting cooling");
                        }

                        g_telemetry.elapsedSec = s_calibElapsedSec;
                        g_telemetry.stage = (ReflowStage)zone;
                        g_telemetry.heaterPower = 1.0f;
                        g_telemetry.coolingPower = 0.0f;
                    }

                    if (s_calibPhase == 1) {
                        g_pid.setRawCoolingPower(1.0f);

                        float dT = temps.avg - s_calibPrevTemp;
                        s_calibCurrentRate = (dt > 0.0f) ? dT / dt : 0.0f;
                        s_calibPrevTemp = temps.avg;

                        if (!s_calibDeadTimeMeasured && s_calibCurrentRate < -CALIB_DT_THRESHOLD) {
                            uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
                            s_calibCoolDeadTimeMs = now - s_calibPhaseStartMs;
                            s_calibDeadTimeMeasured = true;
                            ESP_LOGI(TAG, "Cool dead time: %lu ms", (unsigned long)s_calibCoolDeadTimeMs);
                        }

                        int zone = getZoneForTemp(temps.avg);
                        if (zone >= PID_ZONES) zone = PID_ZONES - 1;
                        s_calibZoneVisited[zone] = true;
                        s_calibZone = zone;

                        if (s_calibCurrentRate < s_calibMaxCoolRates[zone]) {
                            s_calibMaxCoolRates[zone] = s_calibCurrentRate;
                        }

                        uint32_t coolingElapsed = s_calibElapsedSec;
                        uint32_t coolingStart = (uint32_t)(s_calibPhaseStartMs / 1000);
                        bool coolingDone = (coolingElapsed > coolingStart + CALIB_COOL_TIMEOUT_SECS);

                        if (temps.avg <= CALIB_COOLDOWN_TEMP || coolingDone) {
                            g_pid.setRawCoolingPower(0.0f);
                            g_pid.reset();

                            s_calibOldRamp = s_calibRecipe.rampTimeSec;
                            s_calibOldReflow = s_calibRecipe.reflowTimeSec;
                            s_calibNewRamp = s_calibOldRamp;
                            s_calibNewReflow = s_calibOldReflow;
                            adjustRecipeRampReflow(s_calibRecipe, s_calibMaxHeatRates,
                                                    s_calibNewRamp, s_calibNewReflow);

                            g_telemetry.stage = STAGE_COMPLETE;
                            g_telemetry.elapsedSec = s_calibElapsedSec;
                            g_telemetry.heaterPower = 0.0f;
                            g_telemetry.coolingPower = 0.0f;
                            s_calibDone = true;
                            g_systemState = STATE_MENU;
                            g_menuScreen = MENU_CALIB_RESULTS;
                            g_buzzer.play(BUZZER_CYCLE_COMPLETE);
                            ESP_LOGI(TAG, "Calib done: ramp %d->%d reflow %d->%d",
                                     s_calibOldRamp, s_calibNewRamp,
                                     s_calibOldReflow, s_calibNewReflow);
                        }

                        g_telemetry.elapsedSec = s_calibElapsedSec;
                        g_telemetry.stage = (ReflowStage)zone;
                        g_telemetry.heaterPower = 0.0f;
                        g_telemetry.coolingPower = 1.0f;
                    }

                    g_telemetry.setpoint = s_calibRecipe.peakTemp;
                    g_telemetry.actualTemp = temps.avg;

                    if (g_tempHistoryCount < 480) {
                        g_tempHistory[g_tempHistoryCount++] = temps.avg;
                    }

                    if (g_telemetryQueue) {
                        xQueueOverwrite(g_telemetryQueue, &g_telemetry);
                    }
                }
            }
        } else if (g_systemState == STATE_RUNNING || g_systemState == STATE_BAKING) {
            if (xSemaphoreTake(g_i2cMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                TemperatureData temps = g_tempReader.readSensors();
                xSemaphoreGive(g_i2cMutex);

                g_telemetry.temps = temps;

                if (temps.fault || temps.avg > OVERTEMP_C) {
                    g_pid.emergencyStop();
                    g_buzzer.play(BUZZER_ESTOP_ALARM);
                    g_systemState = STATE_ESTOP;
                    g_telemetry.stage = STAGE_FAULT;
                    ESP_LOGE(TAG, "FAULT: avg=%.1f delta=%.1f", temps.avg, temps.delta);
                } else {
                    float setpoint = g_profile.getSetpoint(g_telemetry.elapsedSec);
                    g_telemetry.setpoint = setpoint;
                    g_pid.setSetpoint(setpoint);

                    ReflowStage curStage = g_profile.getStage(g_telemetry.elapsedSec);
                    g_telemetry.stage = curStage;

                    int zone = g_tuner.selectZone(curStage);
                    const ReflowRecipe* recipe = g_profile.getRecipe();
                    if (recipe) {
                        PidGains hg = recipe->heaterGains[zone];
                        PidGains cg = recipe->coolingGains[zone];
                        g_pid.setGains(hg);
                        g_pid.setCoolingGains(cg);
                    }

                    // Zone transition handling
                    if (curStage != g_prevStage || zone != g_prevZone) {
                        if (g_prevZone >= 0 && recipe && recipe->fineTuneEnabled) {
                            // Fine-tune the completed zone
                            ReflowRecipe* r = const_cast<ReflowRecipe*>(g_profile.getRecipe());
                            if (r && g_zoneMetrics.sampleCount > 2) {
                                g_tuner.fineTuneZone(g_zoneMetrics, r->heaterGains[g_prevZone]);
                                g_tuner.fineTuneZone(g_zoneMetrics, r->coolingGains[g_prevZone]);
                                r->tuneSeq = g_calibSeq;
                                ESP_LOGI(TAG, "Zone %d fine-tuned: kp=%.2f ki=%.3f kd=%.2f",
                                         g_prevZone,
                                         r->heaterGains[g_prevZone].kp,
                                         r->heaterGains[g_prevZone].ki,
                                         r->heaterGains[g_prevZone].kd);
                                saveSettings();
                            }
                        }
                        g_pid.resetIntegral();
                        g_tuner.resetMetrics(g_zoneMetrics, setpoint, g_telemetry.elapsedSec);
                        g_prevStage = curStage;
                        g_prevZone = zone;
                    }

                    // Update zone metrics
                    float dt = g_pid.getPidIntervalMs() / 1000.0f;
                    g_tuner.updateMetrics(g_zoneMetrics, temps.avg, setpoint, dt);

                    // Ki suppression near peak (layer 2 integral management)
                    float error = setpoint - temps.avg;
                    float dTemp = (temps.avg - g_telemetry.actualTemp) / dt;
                    if (fabsf(error) < COOLDOWN_RAMP_RESERVE_C && fabsf(dTemp) < 0.5f && curStage == STAGE_REFLOW) {
                        PidGains suppressed = recipe ? recipe->heaterGains[zone] : PidGains();
                        if (recipe) {
                            suppressed.ki = 0.0f;
                            g_pid.setGains(suppressed);
                        }
                    }

                    // Feedforward computation
                    float rampRate = g_profile.getRampRate(g_telemetry.elapsedSec);
                    float feedforward = 0.0f;
                    if (rampRate > 0.0f && s_calibMaxHeatRates[zone] > 0.01f) {
                        feedforward = (rampRate / s_calibMaxHeatRates[zone]) * FF_CAP_PCT;
                        if (feedforward > FF_CAP_PCT) feedforward = FF_CAP_PCT;
                    }
                    g_pid.setFeedforward(feedforward);

                    // Heater off during cooldown
                    if (curStage == STAGE_COOLDOWN) {
                        g_pid.setFeedforward(0.0f);
                        g_pid.setRawHeaterPower(0.0f);
                        g_telemetry.heaterPower = 0.0f;
                        g_telemetry.coolingPower = g_pid.getCoolingOutput();
                    } else {
                        g_pid.compute(temps.avg, dt);
                        g_telemetry.heaterPower = g_pid.getOutput();
                        g_telemetry.coolingPower = g_pid.getCoolingOutput();
                    }

                    g_telemetry.actualTemp = temps.avg;
                    g_telemetry.elapsedSec += (g_pid.getPidIntervalMs() / 1000);

                    if (g_telemetry.stage == STAGE_COMPLETE) {
                        // Save fine-tuned gains on profile completion
                        if (recipe && recipe->fineTuneEnabled) {
                            ReflowRecipe* r = const_cast<ReflowRecipe*>(g_profile.getRecipe());
                            if (r) {
                                r->tuneSeq = g_calibSeq;
                                saveSettings();
                                ESP_LOGI(TAG, "Profile complete, fine-tuned gains saved (seq=%lu)",
                                         (unsigned long)g_calibSeq);
                            }
                        }
                        g_buzzer.play(BUZZER_CYCLE_COMPLETE);
                        g_pid.reset();
                        g_systemState = STATE_MENU;
                        g_prevStage = STAGE_IDLE;
                        g_prevZone = -1;
                    }

                    if (g_tempHistoryCount < 480) {
                        g_tempHistory[g_tempHistoryCount++] = temps.avg;
                    }

                    if (g_telemetryQueue) {
                        xQueueOverwrite(g_telemetryQueue, &g_telemetry);
                    }
                }
            }
        }

        vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(g_pid.getPidIntervalMs()));
    }
}

// Core 1 task: UI refresh (~30ms period)
static void uiTask(void* pvParameters) {
    (void)pvParameters;
    TickType_t lastWake = xTaskGetTickCount();
    ThermalTelemetry localTelemetry = {};

    // Fields per menu:
    // Recipe edit: 0=preheat, 1=soak, 2=peak, 3=ramp, 4=soakTime, 5=reflow, 6=hold, 7=fineTune
    static const int RECIPE_FIELD_COUNT = 8;

    while (1) {
        g_buttons.update();
        g_buzzer.update();

        if (g_buttons.isPressed(PIN_ESTOP)) {
            g_pid.emergencyStop();
            g_buzzer.play(BUZZER_ESTOP_ALARM);
            g_systemState = STATE_ESTOP;
            g_menuScreen = MENU_MAIN;
            g_buttons.clearPress(PIN_ESTOP);
        }

        if (g_telemetryQueue) {
            xQueuePeek(g_telemetryQueue, &localTelemetry, 0);
        }

        g_display.setUseFahrenheit(g_settings.useFahrenheit);

        switch (g_systemState) {
        case STATE_MENU:
            s_showPlot = false;
            switch (g_menuScreen) {
            case MENU_MAIN: {
                const int MENU_ITEM_COUNT = 6;
                if (g_buttons.isPressed(PIN_BTN_F1_UP)) {
                    s_menuCursor = (s_menuCursor > 0) ? s_menuCursor - 1 : MENU_ITEM_COUNT - 1;
                    g_buttons.clearPress(PIN_BTN_F1_UP);
                }
                if (g_buttons.isPressed(PIN_BTN_F2_DOWN)) {
                    s_menuCursor = (s_menuCursor < MENU_ITEM_COUNT - 1) ? s_menuCursor + 1 : 0;
                    g_buttons.clearPress(PIN_BTN_F2_DOWN);
                }
                if (g_buttons.isPressed(PIN_BTN_S_SELECT)) {
                    g_buttons.clearPress(PIN_BTN_S_SELECT);
                    switch (s_menuCursor) {
                    case 0:
                        if (g_recipeCount > 0) {
                            g_menuScreen = MENU_RECIPE_LIST;
                            s_recipeCursor = 0;
                            s_listAction = 0;
                        }
                        break;
                    case 1: {
                        ReflowRecipe r = ReflowRecipe();
                        snprintf(r.name, MAX_RECIPE_NAME_LEN, "New%d", (int)(g_recipeCount + 1));
                        r.preheatTemp = 150.0f;
                        r.soakTemp = 180.0f;
                        r.peakTemp = 220.0f;
                        r.rampTimeSec = 90;
                        r.soakTimeSec = 60;
                        r.reflowTimeSec = 45;
                        r.holdTimeSec = 10;
                        r.fineTuneEnabled = true;
                        r.tuneSeq = g_calibSeq;
                        for (int z = 0; z < PID_ZONES; z++) {
                            r.heaterGains[z].kp = 2.0f;
                            r.heaterGains[z].ki = 0.1f;
                            r.heaterGains[z].kd = 0.5f;
                            r.coolingGains[z].kp = 1.0f;
                            r.coolingGains[z].ki = 0.05f;
                            r.coolingGains[z].kd = 0.2f;
                        }
                        ensureRecipeGainsFromCalib(r);
                        s_editRecipe = r;
                        s_recipeField = 0;
                        g_menuScreen = MENU_RECIPE_EDIT;
                        break;
                    }
                    case 2:
                        if (g_recipeCount > 0) {
                            g_menuScreen = MENU_RECIPE_LIST;
                            s_recipeCursor = 0;
                            s_listAction = 1;
                        }
                        break;
                    case 3:
                        s_editBake = BakeSettings();
                        s_editBake.temperature = 100;
                        s_editBake.durationMin = 30;
                        s_bakeField = 0;
                        g_menuScreen = MENU_BAKE_SETUP;
                        break;
                    case 4:
                        s_settingsField = 0;
                        g_menuScreen = MENU_SETTINGS;
                        break;
                    case 5:
                        s_calibMenuCursor = 0;
                        g_menuScreen = MENU_CALIB_SUBMENU;
                        break;
                    }
                }
                g_display.renderMainMenu(s_menuCursor);
                break;
            }
            case MENU_RECIPE_LIST:
                if (g_buttons.isPressed(PIN_BTN_F1_UP)) {
                    s_recipeCursor = (s_recipeCursor > 0) ? s_recipeCursor - 1 : g_recipeCount - 1;
                    g_buttons.clearPress(PIN_BTN_F1_UP);
                }
                if (g_buttons.isPressed(PIN_BTN_F2_DOWN)) {
                    s_recipeCursor = (s_recipeCursor < g_recipeCount - 1) ? s_recipeCursor + 1 : 0;
                    g_buttons.clearPress(PIN_BTN_F2_DOWN);
                }
                if (g_buttons.isPressed(PIN_BTN_F3_LEFT)) {
                    g_menuScreen = MENU_MAIN;
                    g_buttons.clearPress(PIN_BTN_F3_LEFT);
                }
                if (g_buttons.isPressed(PIN_BTN_S_SELECT)) {
                    g_buttons.clearPress(PIN_BTN_S_SELECT);
                    if (g_recipeCount > 0 && s_recipeCursor < g_recipeCount) {
                        if (s_listAction == 0) {
                            g_profile.loadRecipe(g_recipes[s_recipeCursor]);
                            g_pid.reset();
                            g_tempReader.resetFilters();
                            g_tempHistoryCount = 0;
                            g_prevStage = STAGE_IDLE;
                            g_prevZone = -1;
                            g_tuner.resetMetrics(g_zoneMetrics, 0, 0);
                            g_systemState = STATE_RUNNING;
                        } else {
                            s_editRecipe = g_recipes[s_recipeCursor];
                            s_recipeField = 0;
                            g_menuScreen = MENU_RECIPE_EDIT;
                        }
                    }
                }
                g_display.renderRecipeList(g_recipes, g_recipeCount, s_recipeCursor);
                break;
            case MENU_RECIPE_EDIT:
                if (g_buttons.isPressed(PIN_BTN_F1_UP)) {
                    switch (s_recipeField) {
                    case 0: s_editRecipe.preheatTemp = fminf(s_editRecipe.preheatTemp + 5.0f, 280.0f); break;
                    case 1: s_editRecipe.soakTemp    = fminf(s_editRecipe.soakTemp + 5.0f, 280.0f); break;
                    case 2: s_editRecipe.peakTemp    = fminf(s_editRecipe.peakTemp + 5.0f, 280.0f); break;
                    case 3: if (s_editRecipe.rampTimeSec < 600)   s_editRecipe.rampTimeSec += 5; break;
                    case 4: if (s_editRecipe.soakTimeSec < 300)   s_editRecipe.soakTimeSec += 5; break;
                    case 5: if (s_editRecipe.reflowTimeSec < 300) s_editRecipe.reflowTimeSec += 5; break;
                    case 6: if (s_editRecipe.holdTimeSec < 120)   s_editRecipe.holdTimeSec += 5; break;
                    case 7: s_editRecipe.fineTuneEnabled = !s_editRecipe.fineTuneEnabled; break;
                    }
                    g_buttons.clearPress(PIN_BTN_F1_UP);
                }
                if (g_buttons.isPressed(PIN_BTN_F2_DOWN)) {
                    switch (s_recipeField) {
                    case 0: s_editRecipe.preheatTemp = fmaxf(s_editRecipe.preheatTemp - 5.0f, 25.0f); break;
                    case 1: s_editRecipe.soakTemp    = fmaxf(s_editRecipe.soakTemp - 5.0f, 25.0f); break;
                    case 2: s_editRecipe.peakTemp    = fmaxf(s_editRecipe.peakTemp - 5.0f, 25.0f); break;
                    case 3: if (s_editRecipe.rampTimeSec >= 5)    s_editRecipe.rampTimeSec -= 5; break;
                    case 4: if (s_editRecipe.soakTimeSec >= 5)    s_editRecipe.soakTimeSec -= 5; break;
                    case 5: if (s_editRecipe.reflowTimeSec >= 5)  s_editRecipe.reflowTimeSec -= 5; break;
                    case 6: if (s_editRecipe.holdTimeSec >= 5)    s_editRecipe.holdTimeSec -= 5; break;
                    case 7: s_editRecipe.fineTuneEnabled = !s_editRecipe.fineTuneEnabled; break;
                    }
                    g_buttons.clearPress(PIN_BTN_F2_DOWN);
                }
                if (g_buttons.isPressed(PIN_BTN_F3_LEFT)) {
                    g_menuScreen = MENU_MAIN;
                    g_buttons.clearPress(PIN_BTN_F3_LEFT);
                }
                if (g_buttons.isPressed(PIN_BTN_F4_RIGHT)) {
                    s_recipeField = (s_recipeField < RECIPE_FIELD_COUNT - 1) ? s_recipeField + 1 : 0;
                    g_buttons.clearPress(PIN_BTN_F4_RIGHT);
                }
                if (g_buttons.isPressed(PIN_BTN_S_SELECT)) {
                    g_buttons.clearPress(PIN_BTN_S_SELECT);
                    bool found = false;
                    for (int i = 0; i < g_recipeCount; i++) {
                        if (strcmp(g_recipes[i].name, s_editRecipe.name) == 0) {
                            g_recipes[i] = s_editRecipe;
                            found = true;
                            break;
                        }
                    }
                    if (!found && g_recipeCount < MAX_RECIPES) {
                        g_recipes[g_recipeCount] = s_editRecipe;
                        g_recipeCount++;
                    }
                    saveSettings();
                    g_menuScreen = MENU_MAIN;
                }
                g_display.renderRecipeEdit(s_editRecipe, s_recipeField);
                break;
            case MENU_BAKE_SETUP:
                if (g_buttons.isPressed(PIN_BTN_F1_UP)) {
                    if (s_bakeField == 0) {
                        s_editBake.temperature = (s_editBake.temperature < 280) ? s_editBake.temperature + 5 : 280;
                    } else {
                        s_editBake.durationMin = (s_editBake.durationMin < g_settings.maxBakeMin) ? s_editBake.durationMin + 1 : g_settings.maxBakeMin;
                    }
                    g_buttons.clearPress(PIN_BTN_F1_UP);
                }
                if (g_buttons.isPressed(PIN_BTN_F2_DOWN)) {
                    if (s_bakeField == 0) {
                        s_editBake.temperature = (s_editBake.temperature > 25) ? s_editBake.temperature - 5 : 25;
                    } else {
                        s_editBake.durationMin = (s_editBake.durationMin > 1) ? s_editBake.durationMin - 1 : 1;
                    }
                    g_buttons.clearPress(PIN_BTN_F2_DOWN);
                }
                if (g_buttons.isPressed(PIN_BTN_F3_LEFT)) {
                    g_menuScreen = MENU_MAIN;
                    g_buttons.clearPress(PIN_BTN_F3_LEFT);
                }
                if (g_buttons.isPressed(PIN_BTN_F4_RIGHT)) {
                    s_bakeField = (s_bakeField == 0) ? 1 : 0;
                    g_buttons.clearPress(PIN_BTN_F4_RIGHT);
                }
                if (g_buttons.isPressed(PIN_BTN_S_SELECT)) {
                    g_buttons.clearPress(PIN_BTN_S_SELECT);
                    ReflowRecipe bakeRecipe = ReflowRecipe();
                    snprintf(bakeRecipe.name, MAX_RECIPE_NAME_LEN, "Bake");
                    float temp = (float)s_editBake.temperature;
                    bakeRecipe.preheatTemp = temp;
                    bakeRecipe.soakTemp = temp;
                    bakeRecipe.peakTemp = temp;
                    bakeRecipe.rampTimeSec = 0;
                    bakeRecipe.soakTimeSec = 0;
                    bakeRecipe.reflowTimeSec = s_editBake.durationMin * 60;
                    bakeRecipe.holdTimeSec = 0;
                    bakeRecipe.fineTuneEnabled = true;
                    bakeRecipe.tuneSeq = g_calibSeq;
                    for (int z = 0; z < PID_ZONES; z++) {
                        bakeRecipe.heaterGains[z].kp = 2.0f;
                        bakeRecipe.heaterGains[z].ki = 0.1f;
                        bakeRecipe.heaterGains[z].kd = 0.5f;
                        bakeRecipe.coolingGains[z].kp = 1.0f;
                        bakeRecipe.coolingGains[z].ki = 0.05f;
                        bakeRecipe.coolingGains[z].kd = 0.2f;
                    }
                    ensureRecipeGainsFromCalib(bakeRecipe);
                    g_profile.loadRecipe(bakeRecipe);
                    g_pid.reset();
                    g_tempReader.resetFilters();
                    g_tempHistoryCount = 0;
                    g_prevStage = STAGE_IDLE;
                    g_prevZone = -1;
                    g_tuner.resetMetrics(g_zoneMetrics, 0, 0);
                    g_systemState = STATE_BAKING;
                }
                g_display.renderBakeSetup(s_editBake, s_bakeField);
                break;
            case MENU_SETTINGS:
                if (g_buttons.isPressed(PIN_BTN_F1_UP)) {
                    if (s_settingsField == 1) {
                        g_settings.maxBakeMin = (g_settings.maxBakeMin < 999) ? g_settings.maxBakeMin + 5 : 999;
                    }
                    g_buttons.clearPress(PIN_BTN_F1_UP);
                }
                if (g_buttons.isPressed(PIN_BTN_F2_DOWN)) {
                    if (s_settingsField == 1) {
                        g_settings.maxBakeMin = (g_settings.maxBakeMin > 5) ? g_settings.maxBakeMin - 5 : 5;
                    }
                    g_buttons.clearPress(PIN_BTN_F2_DOWN);
                }
                if (g_buttons.isPressed(PIN_BTN_F3_LEFT)) {
                    saveSettings();
                    g_menuScreen = MENU_MAIN;
                    g_buttons.clearPress(PIN_BTN_F3_LEFT);
                }
                if (g_buttons.isPressed(PIN_BTN_F4_RIGHT) || g_buttons.isPressed(PIN_BTN_S_SELECT)) {
                    if (s_settingsField == 0) {
                        g_settings.useFahrenheit = !g_settings.useFahrenheit;
                    }
                    g_buttons.clearPress(PIN_BTN_F4_RIGHT);
                    g_buttons.clearPress(PIN_BTN_S_SELECT);
                }
                g_display.renderSettings(g_settings, s_settingsField);
                break;
            case MENU_CALIB_SUBMENU:
                if (g_buttons.isPressed(PIN_BTN_F1_UP)) {
                    s_calibMenuCursor = (s_calibMenuCursor > 0) ? s_calibMenuCursor - 1 : 2;
                    g_buttons.clearPress(PIN_BTN_F1_UP);
                }
                if (g_buttons.isPressed(PIN_BTN_F2_DOWN)) {
                    s_calibMenuCursor = (s_calibMenuCursor < 2) ? s_calibMenuCursor + 1 : 0;
                    g_buttons.clearPress(PIN_BTN_F2_DOWN);
                }
                if (g_buttons.isPressed(PIN_BTN_F3_LEFT)) {
                    g_menuScreen = MENU_MAIN;
                    g_buttons.clearPress(PIN_BTN_F3_LEFT);
                }
                if (g_buttons.isPressed(PIN_BTN_S_SELECT)) {
                    g_buttons.clearPress(PIN_BTN_S_SELECT);
                    if (s_calibMenuCursor == 0) {
                        g_menuScreen = MENU_CALIBRATION;
                    } else if (s_calibMenuCursor == 1) {
                        if (g_recipeCount > 0) {
                            g_menuScreen = MENU_CALIB_RECIPE_LIST;
                            s_recipeCursor = 0;
                        }
                    } else {
                        s_calibInfoRecipeIdx = 0;
                        s_calibInfoPage = 0;
                        g_menuScreen = MENU_CALIB_INFO;
                    }
                }
                g_display.renderCalibSubmenu(s_calibMenuCursor);
                break;

            case MENU_CALIBRATION:
                if (g_buttons.anyPressed()) {
                    g_menuScreen = MENU_CALIB_SUBMENU;
                    g_buttons.clearPress(PIN_BTN_F1_UP);
                    g_buttons.clearPress(PIN_BTN_F2_DOWN);
                    g_buttons.clearPress(PIN_BTN_F3_LEFT);
                    g_buttons.clearPress(PIN_BTN_F4_RIGHT);
                    g_buttons.clearPress(PIN_BTN_S_SELECT);
                }
                g_display.renderCalibration();
                break;

            case MENU_CALIB_RECIPE_LIST:
                if (g_buttons.isPressed(PIN_BTN_F1_UP)) {
                    s_recipeCursor = (s_recipeCursor > 0) ? s_recipeCursor - 1 : g_recipeCount - 1;
                    g_buttons.clearPress(PIN_BTN_F1_UP);
                }
                if (g_buttons.isPressed(PIN_BTN_F2_DOWN)) {
                    s_recipeCursor = (s_recipeCursor < g_recipeCount - 1) ? s_recipeCursor + 1 : 0;
                    g_buttons.clearPress(PIN_BTN_F2_DOWN);
                }
                if (g_buttons.isPressed(PIN_BTN_F3_LEFT)) {
                    g_menuScreen = MENU_CALIB_SUBMENU;
                    g_buttons.clearPress(PIN_BTN_F3_LEFT);
                }
                if (g_buttons.isPressed(PIN_BTN_S_SELECT)) {
                    g_buttons.clearPress(PIN_BTN_S_SELECT);
                    if (g_recipeCount > 0 && s_recipeCursor < g_recipeCount) {
                        s_calibRecipe = g_recipes[s_recipeCursor];
                        s_calibRecipeIdx = s_recipeCursor;
                        s_calibPhase = 0;
                        for (int z = 0; z < PID_ZONES; z++) {
                            s_calibMaxHeatRates[z] = 0.0f;
                            s_calibMaxCoolRates[z] = 0.0f;
                            s_calibZoneVisited[z] = false;
                        }
                        s_calibCurrentRate = 0.0f;
                        s_calibPrevTemp = 0.0f;
                        s_calibZone = 0;
                        s_calibElapsedSec = 0;
                        s_calibPhaseStartMs = 0;
                        s_calibHeatDeadTimeMs = 0;
                        s_calibCoolDeadTimeMs = 0;
                        s_calibDeadTimeMeasured = false;
                        s_calibDone = false;
                        s_calibOldRamp = s_calibRecipe.rampTimeSec;
                        s_calibNewRamp = s_calibRecipe.rampTimeSec;
                        s_calibOldReflow = s_calibRecipe.reflowTimeSec;
                        s_calibNewReflow = s_calibRecipe.reflowTimeSec;
                        g_pid.reset();
                        g_tempReader.resetFilters();
                        g_tempHistoryCount = 0;
                        g_prevStage = STAGE_IDLE;
                        g_prevZone = -1;
                        g_systemState = STATE_CALIB_TEST;
                    }
                }
                g_display.renderRecipeList(g_recipes, g_recipeCount, s_recipeCursor);
                break;

            case MENU_CALIB_RESULTS:
                g_display.renderCalibTestResults(
                    s_calibOldRamp, s_calibNewRamp,
                    s_calibOldReflow, s_calibNewReflow,
                    s_calibMaxHeatRates, s_calibMaxCoolRates,
                    s_calibHeatDeadTimeMs, s_calibCoolDeadTimeMs,
                    g_recipeCount);
                if (g_buttons.isPressed(PIN_BTN_S_SELECT)) {
                    g_buttons.clearPress(PIN_BTN_S_SELECT);
                    g_calibSeq++;
                    applyCalibToAllRecipes(s_calibMaxHeatRates);
                    saveSettings();
                    saveCalibData(s_calibMaxHeatRates, s_calibMaxCoolRates,
                                  s_calibHeatDeadTimeMs, s_calibCoolDeadTimeMs);
                    float hDt = s_calibHeatDeadTimeMs / 1000.0f;
                    float cDt = s_calibCoolDeadTimeMs / 1000.0f;
                    g_tuner.setDeadTime(hDt, cDt);
                    ESP_LOGI(TAG, "Calib seq %lu saved & applied to all recipes, dead time H:%lums C:%lums",
                             (unsigned long)g_calibSeq,
                             (unsigned long)s_calibHeatDeadTimeMs, (unsigned long)s_calibCoolDeadTimeMs);
                    g_menuScreen = MENU_CALIB_SUBMENU;
                }
                if (g_buttons.isPressed(PIN_BTN_F3_LEFT)) {
                    g_buttons.clearPress(PIN_BTN_F3_LEFT);
                    g_menuScreen = MENU_CALIB_SUBMENU;
                }
                break;

            case MENU_CALIB_INFO:
                if (g_buttons.isPressed(PIN_BTN_F1_UP)) {
                    g_buttons.clearPress(PIN_BTN_F1_UP);
                    if (s_calibInfoPage != 1 && g_recipeCount > 0) {
                        s_calibInfoRecipeIdx = (s_calibInfoRecipeIdx > 0) ? s_calibInfoRecipeIdx - 1 : g_recipeCount - 1;
                    }
                }
                if (g_buttons.isPressed(PIN_BTN_F2_DOWN)) {
                    g_buttons.clearPress(PIN_BTN_F2_DOWN);
                    if (s_calibInfoPage != 1 && g_recipeCount > 0) {
                        s_calibInfoRecipeIdx = (s_calibInfoRecipeIdx < g_recipeCount - 1) ? s_calibInfoRecipeIdx + 1 : 0;
                    }
                }
                if (g_buttons.isPressed(PIN_BTN_F3_LEFT)) {
                    g_buttons.clearPress(PIN_BTN_F3_LEFT);
                    g_menuScreen = MENU_CALIB_SUBMENU;
                }
                if (g_buttons.isPressed(PIN_BTN_S_SELECT)) {
                    g_buttons.clearPress(PIN_BTN_S_SELECT);
                    s_calibInfoPage = (s_calibInfoPage + 1) % 3;
                }
                g_display.renderCalibInfo(s_calibInfoRecipeIdx, g_recipeCount, g_recipes,
                                           s_calibMaxHeatRates, s_calibMaxCoolRates,
                                           s_calibHeatDeadTimeMs, s_calibCoolDeadTimeMs,
                                           s_calibInfoPage);
                break;

            default:
                g_display.renderMainMenu(0);
                break;
            }
            break;

        case STATE_RUNNING:
        case STATE_BAKING:
            if (g_buttons.isPressed(PIN_BTN_F3_LEFT) || g_buttons.isPressed(PIN_BTN_F4_RIGHT)) {
                s_showPlot = !s_showPlot;
                g_buttons.clearPress(PIN_BTN_F3_LEFT);
                g_buttons.clearPress(PIN_BTN_F4_RIGHT);
            }
            if (s_showPlot) {
                g_display.renderLivePlot(g_tempHistory, g_tempHistoryCount, localTelemetry.setpoint);
            } else {
                g_display.renderRunning(localTelemetry);
            }
            break;

        case STATE_CALIB_TEST:
            if (g_buttons.isPressed(PIN_BTN_S_SELECT)) {
                g_buttons.clearPress(PIN_BTN_S_SELECT);
                g_pid.setRawHeaterPower(0.0f);
                g_pid.setRawCoolingPower(0.0f);
                g_pid.reset();
                s_calibDone = true;
                g_systemState = STATE_MENU;
                g_menuScreen = MENU_CALIB_SUBMENU;
            }
            {
                bool isCooling = (s_calibPhase == 1);
                float maxRate = isCooling ? s_calibMaxCoolRates[s_calibZone] : s_calibMaxHeatRates[s_calibZone];
                g_display.renderCalibTestRunning(
                    isCooling,
                    s_calibZone + 1,
                    localTelemetry.actualTemp,
                    maxRate,
                    s_calibCurrentRate,
                    s_calibElapsedSec,
                    g_tempHistory,
                    g_tempHistoryCount
                );
            }
            break;

        case STATE_ESTOP:
            g_display.renderEstop();
            if (g_buttons.isPressed(PIN_BTN_S_SELECT)) {
                g_pid.reset();
                g_systemState = STATE_MENU;
                g_menuScreen = MENU_MAIN;
                g_buttons.clearPress(PIN_BTN_S_SELECT);
            }
            break;

        default:
            break;
        }

        vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(30));
    }
}

extern "C" void app_main() {
    ESP_LOGI(TAG, "OvenController v%d.%d.%d starting...",
             FIRMWARE_VERSION_MAJOR, FIRMWARE_VERSION_MINOR, FIRMWARE_VERSION_PATCH);

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    i2c_master_bus_handle_t bus_handle;
    i2c_master_bus_config_t i2c_cfg = {};
    i2c_cfg.i2c_port = I2C_NUM_0;
    i2c_cfg.sda_io_num = PIN_I2C_SDA;
    i2c_cfg.scl_io_num = PIN_I2C_SCL;
    i2c_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
    i2c_cfg.glitch_ignore_cnt = 7;
    i2c_cfg.flags.enable_internal_pullup = true;
    i2c_new_master_bus(&i2c_cfg, &bus_handle);

    g_tempReader.init(bus_handle);

    g_pid.init();
    g_buttons.init();
    g_buzzer.init();

    ledc_timer_config_t timer_cfg = {};
    timer_cfg.speed_mode = LEDC_LOW_SPEED_MODE;
    timer_cfg.duty_resolution = LEDC_TIMER_8_BIT;
    timer_cfg.timer_num = LEDC_TIMER_0;
    timer_cfg.freq_hz = SYS_FAN_PWM_FREQ;
    timer_cfg.clk_cfg = LEDC_AUTO_CLK;
    ledc_timer_config(&timer_cfg);
    ledc_channel_config_t chan_cfg = {};
    chan_cfg.gpio_num = PIN_SYS_FAN_PWM;
    chan_cfg.speed_mode = LEDC_LOW_SPEED_MODE;
    chan_cfg.channel = LEDC_CHANNEL_0;
    chan_cfg.timer_sel = LEDC_TIMER_0;
    chan_cfg.duty = 0;
    chan_cfg.hpoint = 0;
    ledc_channel_config(&chan_cfg);

    loadSettings();
    g_tempReader.setCalibrationOffset(0, g_settings.tc1Offset);
    g_tempReader.setCalibrationOffset(1, g_settings.tc2Offset);
    {
        uint32_t hDt = 0, cDt = 0;
        loadCalibData(s_calibMaxHeatRates, s_calibMaxCoolRates, hDt, cDt);
        s_calibHeatDeadTimeMs = hDt;
        s_calibCoolDeadTimeMs = cDt;
        g_tuner.setDeadTime(hDt / 1000.0f, cDt / 1000.0f);
        ESP_LOGI(TAG, "Calib data loaded, dead time H:%lums C:%lums, calibSeq=%lu",
                 (unsigned long)hDt, (unsigned long)cDt, (unsigned long)g_calibSeq);
    }

    g_pid.measureLineFrequency();
    {
        nvs_handle_t nvs;
        if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs) == ESP_OK) {
            uint32_t stored = 0;
            esp_err_t err = nvs_get_u32(nvs, "zcHalfCycUs", &stored);
            uint32_t measured = g_pid.getHalfCycleUs();
            int32_t diff = (int32_t)(measured - stored);
            if (err != ESP_OK || diff < -50 || diff > 50) {
                nvs_set_u32(nvs, "zcHalfCycUs", measured);
                nvs_commit(nvs);
                ESP_LOGI(TAG, "Saved ZC half-cycle %" PRIu32 " us to NVS", measured);
            }
            nvs_close(nvs);
        }
    }

    g_telemetryQueue = xQueueCreate(1, sizeof(ThermalTelemetry));
    g_i2cMutex = xSemaphoreCreateMutex();

    xTaskCreatePinnedToCore(controlTask, "control", 4096, NULL, 3, NULL, 0);
    xTaskCreatePinnedToCore(uiTask, "ui", 8192, NULL, 1, NULL, 1);

    g_display.init();
    {
        char verBuf[24];
        snprintf(verBuf, sizeof(verBuf), "%d.%d.%d",
                 FIRMWARE_VERSION_MAJOR, FIRMWARE_VERSION_MINOR, FIRMWARE_VERSION_PATCH);
        float lf = g_pid.getLineFrequency();
        g_display.splash(verBuf, lf);
        ESP_LOGI(TAG, "Splash: v%s, %.1f Hz", verBuf, lf);
    }

    ESP_LOGI(TAG, "Startup complete, calibSeq=%lu", (unsigned long)g_calibSeq);

    vTaskDelete(NULL);
}
