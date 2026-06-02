#include "ButtonDebouncer.h"
#include "Config.h"
#include "esp_timer.h"
#include "driver/gpio.h"

ButtonDebouncer::ButtonDebouncer() {
    gpio_num_t pins[NUM_BUTTONS] = {
        PIN_BTN_F1_UP, PIN_BTN_F2_DOWN, PIN_BTN_F3_LEFT,
        PIN_BTN_F4_RIGHT, PIN_BTN_S_SELECT, PIN_ESTOP
    };
    for (int i = 0; i < NUM_BUTTONS; i++) {
        _buttons[i].pin = pins[i];
        _buttons[i].lastState = true;
        _buttons[i].currentState = true;
        _buttons[i].lastDebounceTime = 0;
        _buttons[i].pressed = false;
    }
}

void ButtonDebouncer::init() {
    for (int i = 0; i < NUM_BUTTONS; i++) {
        gpio_set_direction(_buttons[i].pin, GPIO_MODE_INPUT);
        gpio_set_pull_mode(_buttons[i].pin, GPIO_PULLUP_ONLY);
    }
}

void ButtonDebouncer::update() {
    uint32_t now = getMillis();
    for (int i = 0; i < NUM_BUTTONS; i++) {
        bool reading = gpio_get_level(_buttons[i].pin) == 0;
        if (reading != _buttons[i].lastState) {
            _buttons[i].lastDebounceTime = now;
        }
        if ((now - _buttons[i].lastDebounceTime) > DEBOUNCE_DELAY_MS) {
            if (reading && !_buttons[i].currentState) {
                _buttons[i].pressed = true;
            }
            _buttons[i].currentState = reading;
        }
        _buttons[i].lastState = reading;
    }
}

bool ButtonDebouncer::isPressed(gpio_num_t pin) const {
    for (int i = 0; i < NUM_BUTTONS; i++) {
        if (_buttons[i].pin == pin) return _buttons[i].pressed;
    }
    return false;
}

bool ButtonDebouncer::anyPressed() const {
    for (int i = 0; i < NUM_BUTTONS; i++) {
        if (_buttons[i].pressed) return true;
    }
    return false;
}

void ButtonDebouncer::clearPress(gpio_num_t pin) {
    for (int i = 0; i < NUM_BUTTONS; i++) {
        if (_buttons[i].pin == pin) {
            _buttons[i].pressed = false;
            return;
        }
    }
}

uint32_t ButtonDebouncer::getMillis() const {
    return (uint32_t)(esp_timer_get_time() / 1000);
}
