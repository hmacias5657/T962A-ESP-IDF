#pragma once

#include <stdint.h>

enum BuzzerPattern : uint8_t {
    BUZZER_NONE = 0,
    BUZZER_STAGE_CHANGE,
    BUZZER_CYCLE_COMPLETE,
    BUZZER_ERROR,
    BUZZER_ESTOP_ALARM,
    BUZZER_BUTTON_CLICK
};

class Buzzer {
public:
    Buzzer();
    void init();
    void play(BuzzerPattern pattern);
    void update();
    void stop();
    bool isPlaying() const;
private:
    BuzzerPattern _activePattern;
    uint32_t _patternStart;
    uint32_t _lastToggle;
    bool _state;
    int _step;
    uint32_t getMillis() const;
    void setOutput(bool on);
};
