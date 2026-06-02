#pragma once

#include <stdint.h>
#include "driver/gpio.h"

struct Button {
    gpio_num_t pin;
    bool lastState;
    bool currentState;
    uint32_t lastDebounceTime;
    bool pressed;
};

class ButtonDebouncer {
public:
    ButtonDebouncer();
    void init();
    void update();
    bool isPressed(gpio_num_t pin) const;
    bool anyPressed() const;
    void clearPress(gpio_num_t pin);
private:
    static const int NUM_BUTTONS = 6;
    Button _buttons[NUM_BUTTONS];
    static const uint32_t DEBOUNCE_DELAY_MS = 50;
    uint32_t getMillis() const;
};
