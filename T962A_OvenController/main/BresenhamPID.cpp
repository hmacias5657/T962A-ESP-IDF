#include "BresenhamPID.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cmath>
#include <inttypes.h>

static const char* TAG = "BresenhamPID";

static const float D_FILTER_ALPHA = 0.3f;

volatile int BresenhamPID::_zcCount = 0;
volatile int BresenhamPID::_heaterOnCycles = 0;
volatile int BresenhamPID::_coolingOnCycles = 0;
volatile bool BresenhamPID::_zcFlag = false;
volatile bool BresenhamPID::_measureDone = false;
volatile uint32_t BresenhamPID::_measuredHalfCycleUs = ZC_HALF_CYCLE_US_DEFAULT;
volatile int BresenhamPID::_measureCount = 0;
volatile int64_t BresenhamPID::_measureFirstTime = 0;

BresenhamPID::BresenhamPID()
    : _kp(0.0f), _ki(0.0f), _kd(0.0f)
    , _ckp(0.0f), _cki(0.0f), _ckd(0.0f)
    , _setpoint(0.0f), _integral(0.0f), _prevError(0.0f)
    , _heaterOutput(0.0f), _coolingOutput(0.0f)
    , _feedforward(0.0f), _dFiltered(0.0f)
    , _estopped(false) {}

void BresenhamPID::init() {
    gpio_config_t ssr_io = {
        .pin_bit_mask = (1ULL << PIN_SSR_GATE) | (1ULL << PIN_COOLING_FAN_SSR),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&ssr_io);
    gpio_set_level(PIN_SSR_GATE, 0);
    gpio_set_level(PIN_COOLING_FAN_SSR, 0);

    gpio_config_t zc_io = {
        .pin_bit_mask = (1ULL << PIN_ZC_INTERRUPT),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_POSEDGE,
    };
    gpio_config(&zc_io);
    gpio_install_isr_service(0);
    gpio_isr_handler_add(PIN_ZC_INTERRUPT, handleZeroCrossing, NULL);
}

void BresenhamPID::setGains(const PidGains& gains) {
    _kp = gains.kp;
    _ki = gains.ki;
    _kd = gains.kd;
}

void BresenhamPID::setCoolingGains(const PidGains& gains) {
    _ckp = gains.kp;
    _cki = gains.ki;
    _ckd = gains.kd;
}

void BresenhamPID::setSetpoint(float setpoint) {
    _setpoint = setpoint;
}

float BresenhamPID::compute(float input, float dt) {
    if (_estopped) return 0.0f;

    float error = _setpoint - input;

    _integral += error * dt;
    _integral = fmaxf(-100.0f, fminf(100.0f, _integral));

    float rawD = (error - _prevError) / dt;
    _dFiltered = D_FILTER_ALPHA * rawD + (1.0f - D_FILTER_ALPHA) * _dFiltered;
    _prevError = error;

    _heaterOutput = _kp * error + _ki * _integral + _kd * _dFiltered;
    _heaterOutput = fmaxf(0.0f, fminf(1.0f, _heaterOutput));

    _coolingOutput = _ckp * error + _cki * _integral + _ckd * _dFiltered;
    _coolingOutput = fmaxf(0.0f, fminf(1.0f, _coolingOutput));

    if (error < 0) {
        _heaterOutput = 0.0f;
    }

    float totalHeater = _heaterOutput + _feedforward;
    if (totalHeater > 1.0f) totalHeater = 1.0f;
    if (totalHeater < 0.0f) totalHeater = 0.0f;

    bresenhamPower(totalHeater);
    bresenhamCooling(_coolingOutput);

    return totalHeater;
}

void BresenhamPID::setFeedforward(float ff) {
    _feedforward = ff;
    if (_feedforward < 0.0f) _feedforward = 0.0f;
    if (_feedforward > FF_CAP_PCT) _feedforward = FF_CAP_PCT;
}

void BresenhamPID::setRawHeaterPower(float power) {
    if (power < 0.0f) power = 0.0f;
    if (power > 1.0f) power = 1.0f;
    _heaterOutput = power;
    _heaterOnCycles = (int)(power * BRESENHAM_CYCLES);
    if (_heaterOnCycles > BRESENHAM_CYCLES) _heaterOnCycles = BRESENHAM_CYCLES;
    if (_heaterOnCycles < 0) _heaterOnCycles = 0;
    _coolingOutput = 0.0f;
    _coolingOnCycles = 0;
}

void BresenhamPID::setRawCoolingPower(float power) {
    if (power < 0.0f) power = 0.0f;
    if (power > 1.0f) power = 1.0f;
    _heaterOutput = 0.0f;
    _heaterOnCycles = 0;
    _coolingOutput = power;
    _coolingOnCycles = (int)(power * BRESENHAM_CYCLES);
    if (_coolingOnCycles > BRESENHAM_CYCLES) _coolingOnCycles = BRESENHAM_CYCLES;
    if (_coolingOnCycles < 0) _coolingOnCycles = 0;
}

float BresenhamPID::getOutput() const { return _heaterOutput; }
float BresenhamPID::getCoolingOutput() const { return _coolingOutput; }

void BresenhamPID::emergencyStop() {
    _estopped = true;
    gpio_set_level(PIN_SSR_GATE, 0);
    gpio_set_level(PIN_COOLING_FAN_SSR, 0);
    ESP_LOGW(TAG, "EMERGENCY STOP ACTIVATED");
}

bool BresenhamPID::isEstopped() const { return _estopped; }

void BresenhamPID::reset() {
    _integral = 0.0f;
    _prevError = 0.0f;
    _dFiltered = 0.0f;
    _heaterOutput = 0.0f;
    _coolingOutput = 0.0f;
    _feedforward = 0.0f;
    _estopped = false;
}

void BresenhamPID::resetIntegral() {
    _integral = 0.0f;
}

float BresenhamPID::getIntegral() const {
    return _integral;
}

void IRAM_ATTR BresenhamPID::handleZeroCrossing(void* arg) {
    (void)arg;
    if (!_measureDone) {
        int64_t now = esp_timer_get_time();
        if (_measureCount == 0) {
            _measureFirstTime = now;
        }
        int mc = _measureCount + 1;
        _measureCount = mc;
        if (mc >= ZC_MEASURE_SAMPLES) {
            int64_t elapsed = now - _measureFirstTime;
            _measuredHalfCycleUs = (uint32_t)(elapsed / _measureCount);
            _measureDone = true;
        }
    }
    int zc = _zcCount + 1;
    _zcCount = zc;
    if (_zcCount >= BRESENHAM_CYCLES) {
        _zcCount = 0;
        _zcFlag = true;
    }
    if (_zcCount < _heaterOnCycles) {
        gpio_set_level(PIN_SSR_GATE, 1);
    } else {
        gpio_set_level(PIN_SSR_GATE, 0);
    }
    if (_zcCount < _coolingOnCycles) {
        gpio_set_level(PIN_COOLING_FAN_SSR, 1);
    } else {
        gpio_set_level(PIN_COOLING_FAN_SSR, 0);
    }
}

void BresenhamPID::bresenhamPower(float power) {
    float bounded = fmaxf(0.0f, fminf(1.0f, power));
    _heaterOnCycles = (int)(bounded * BRESENHAM_CYCLES);
    if (_heaterOnCycles > BRESENHAM_CYCLES) _heaterOnCycles = BRESENHAM_CYCLES;
    if (_heaterOnCycles < 0) _heaterOnCycles = 0;
}

void BresenhamPID::bresenhamCooling(float power) {
    _coolingOnCycles = (int)(power * BRESENHAM_CYCLES);
    if (_coolingOnCycles > BRESENHAM_CYCLES) _coolingOnCycles = BRESENHAM_CYCLES;
    if (_coolingOnCycles < 0) _coolingOnCycles = 0;
}

void BresenhamPID::measureLineFrequency() {
    _measureCount = 0;
    _measureFirstTime = 0;
    _measureDone = false;
    TickType_t start = xTaskGetTickCount();
    while (!_measureDone) {
        if ((xTaskGetTickCount() - start) > pdMS_TO_TICKS(2000)) {
            ESP_LOGW(TAG, "ZC freq measurement timed out, using default %" PRIu32 " us",
                     (uint32_t)ZC_HALF_CYCLE_US_DEFAULT);
            _measuredHalfCycleUs = ZC_HALF_CYCLE_US_DEFAULT;
            _measureDone = true;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    ESP_LOGI(TAG, "Line: %.1f Hz (half-cycle: %" PRIu32 " us)",
             getLineFrequency(), (uint32_t)_measuredHalfCycleUs);
}

uint32_t BresenhamPID::getHalfCycleUs() {
    if (!_measureDone) return ZC_HALF_CYCLE_US_DEFAULT;
    return _measuredHalfCycleUs;
}

float BresenhamPID::getLineFrequency() {
    uint32_t halfUs = getHalfCycleUs();
    return 1000000.0f / (2.0f * halfUs);
}

uint32_t BresenhamPID::getPidIntervalMs() {
    return (getHalfCycleUs() * BRESENHAM_CYCLES) / 1000;
}
