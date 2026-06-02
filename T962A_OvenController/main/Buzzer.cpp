#include "Buzzer.h"
#include "Config.h"
#include "esp_timer.h"
#include "driver/gpio.h"

Buzzer::Buzzer()
    : _activePattern(BUZZER_NONE)
    , _patternStart(0)
    , _lastToggle(0)
    , _state(false)
    , _step(0) {}

void Buzzer::init() {
    gpio_set_direction(PIN_BUZZER, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_BUZZER, 0);
}

void Buzzer::play(BuzzerPattern pattern) {
    _activePattern = pattern;
    _patternStart = getMillis();
    _lastToggle = _patternStart;
    _state = false;
    _step = 0;
}

void Buzzer::update() {
    if (_activePattern == BUZZER_NONE) return;

    uint32_t now = getMillis();
    uint32_t elapsed = now - _patternStart;

    switch (_activePattern) {
    case BUZZER_BUTTON_CLICK:
        if (elapsed < 50) setOutput(true);
        else if (elapsed < 100) setOutput(false);
        else _activePattern = BUZZER_NONE;
        break;
    case BUZZER_STAGE_CHANGE:
        if (elapsed < 200) setOutput(true);
        else if (elapsed < 400) setOutput(false);
        else _activePattern = BUZZER_NONE;
        break;
    case BUZZER_CYCLE_COMPLETE:
        if ((elapsed / 200) % 2 == 0) setOutput(true);
        else setOutput(false);
        if (elapsed > 2000) _activePattern = BUZZER_NONE;
        break;
    case BUZZER_ERROR:
        if ((elapsed / 300) % 2 == 0) setOutput(true);
        else setOutput(false);
        if (elapsed > 3000) _activePattern = BUZZER_NONE;
        break;
    case BUZZER_ESTOP_ALARM:
        if ((elapsed / 100) % 2 == 0) setOutput(true);
        else setOutput(false);
        break;
    default:
        break;
    }
}

void Buzzer::stop() {
    _activePattern = BUZZER_NONE;
    setOutput(false);
}

bool Buzzer::isPlaying() const {
    return _activePattern != BUZZER_NONE;
}

void Buzzer::setOutput(bool on) {
    gpio_set_level(PIN_BUZZER, on ? 1 : 0);
}

uint32_t Buzzer::getMillis() const {
    return (uint32_t)(esp_timer_get_time() / 1000);
}
