#pragma once

#include <stdint.h>
#include "Config.h"

// Reflow stage IDs
enum ReflowStage : uint8_t {
    STAGE_IDLE = 0,
    STAGE_PREHEAT,
    STAGE_SOAK,
    STAGE_REFLOW,
    STAGE_COOLDOWN,
    STAGE_COMPLETE,
    STAGE_FAULT
};

// System state
enum SystemState : uint8_t {
    STATE_MENU = 0,
    STATE_RUNNING,
    STATE_BAKING,
    STATE_CALIBRATION,
    STATE_EDIT_RECIPE,
    STATE_SETTINGS,
    STATE_ESTOP,
    STATE_CALIB_TEST
};

// Menu screen IDs
enum MenuScreen : uint8_t {
    MENU_MAIN = 0,
    MENU_RECIPE_LIST,
    MENU_RECIPE_EDIT,
    MENU_RUN_DISPLAY,
    MENU_BAKE_SETUP,
    MENU_SETTINGS,
    MENU_CALIBRATION,
    MENU_PLOT,
    MENU_CALIB_SUBMENU,
    MENU_CALIB_RECIPE_LIST,
    MENU_CALIB_RESULTS,
    MENU_CALIB_INFO
};

struct PidGains {
    float kp;
    float ki;
    float kd;
};

struct ZoneMetrics {
    float peakTemp;
    float setpoint;
    float steadyError;
    float oscillationAmp;
    float riseTime90;
    float settlingTime;
    float maxRate;
    int sampleCount;
    float errorSum;
    float errorAbsSum;
    float tempMin;
    float tempMax;
    float lastTemp;
    float prevSlope;
    int zeroCrossCount;
    uint32_t entryTimeSec;
    bool settled;
};

struct ReflowRecipe {
    char name[MAX_RECIPE_NAME_LEN];
    float preheatTemp;
    float soakTemp;
    float peakTemp;
    uint16_t rampTimeSec;
    uint16_t soakTimeSec;
    uint16_t reflowTimeSec;
    uint16_t holdTimeSec;
    PidGains heaterGains[PID_ZONES];
    PidGains coolingGains[PID_ZONES];
    bool fineTuneEnabled;
    uint32_t tuneSeq;
};

struct TemperatureData {
    float tc1;
    float tc2;
    float avg;
    float delta;
    bool fault;
};

struct ThermalTelemetry {
    float setpoint;
    float actualTemp;
    float heaterPower;
    float coolingPower;
    float fanPWM;
    uint32_t elapsedSec;
    ReflowStage stage;
    TemperatureData temps;
};

struct BakeSettings {
    uint16_t temperature;
    uint16_t durationMin;
};

struct SystemSettings {
    bool useFahrenheit;
    float tc1Offset;
    float tc2Offset;
    uint16_t maxBakeMin;
};
