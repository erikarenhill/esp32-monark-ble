#pragma once
#include <Arduino.h>
#include "IDisplay.h"
#include "SettingsManager.h"
#include "Calibration.h"

class CalibrationProcess {
public:
    CalibrationProcess(uint8_t btnPin, uint8_t adcPin, IDisplay* lcd, SettingsManager* settings, MonarkCalibration* calObj);
    
    void begin();
    void update(); // Call in loop
    bool isCalibrating() const;
    void startCalibration(); // Manually start calibration

private:
    enum State {
        IDLE,
        WAIT_0KP,
        WAIT_6KP,
        WAIT_4KP,
        WAIT_2KP,
        DONE
    };

    uint8_t _btnPin;
    uint8_t _adcPin;
    IDisplay* _lcd;
    SettingsManager* _settings;
    MonarkCalibration* _calObj;

    State _state = IDLE;
    int _tempAdc[4]; // 0, 2, 4, 6

    // Button debounce
    bool _lastBtnState = HIGH;
    uint32_t _lastDebounceTime = 0;
    const uint32_t DEBOUNCE_DELAY = 50;

    // Display update
    uint32_t _lastDisplayTime = 0;

    // Done timer
    uint32_t _doneStartTime = 0;

    void handleButtonPress();
    float readAdcAvg();  // Returns float for precision, round when storing
    void showState();
    void saveAndApply();
};
