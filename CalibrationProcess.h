#pragma once
#include <Arduino.h>
#include "IDisplay.h"
#include "SettingsManager.h"
#include "Calibration.h"
#include "PowerSource.h"

// Result of a tare attempt
enum class TareResult { Ok, Rejected, NoCalibration, Moving };

class CalibrationProcess {
public:
    CalibrationProcess(uint8_t btnPin, uint8_t adcPin, IDisplay* lcd, SettingsManager* settings,
                       MonarkCalibration* calObj, uint8_t tareBtnPin = 255, uint8_t ledPin = 255,
                       PowerSource* power = nullptr);

    void begin();
    void update(); // Call in loop
    bool isCalibrating() const;
    void startCalibration(); // Manually start calibration

    // Zero the brake reading at the current ADC value. The bike must be at
    // rest with the brake at its lowest setting. Shifts the calibration
    // curve instead of rewriting the anchors, so the span is preserved.
    TareResult performTare();

    // Offset applied by the last successful tare, in millivolts
    float getTareOffset() const { return _calObj ? _calObj->getZeroOffset() : 0.0f; }

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
    uint8_t _tareBtnPin;
    uint8_t _ledPin;
    PowerSource* _power;

    State _state = IDLE;
    int _tempAdc[4]; // 0, 2, 4, 6

    // Button debounce / gesture tracking for the calibration button
    int _lastBtnState = HIGH;
    int _stableBtnState = HIGH;
    uint32_t _lastDebounceTime = 0;
    bool _btnDown = false;
    uint32_t _pressStartMs = 0;
    bool _longFired = false;
    static const uint32_t DEBOUNCE_DELAY = 50;
    static const uint32_t LONG_PRESS_MS  = 3000;  // hold this long for full calibration
    static constexpr float MAX_TARE_RPM  = 5.0f;  // must be stopped to zero

    // Dedicated tare button (optional)
    int _lastTareBtnState = HIGH;
    int _stableTareBtnState = HIGH;
    uint32_t _lastTareDebounceTime = 0;

    // Display update
    uint32_t _lastDisplayTime = 0;

    // Done timer
    uint32_t _doneStartTime = 0;

    // Tare feedback message timer
    uint32_t _tareMsgUntil = 0;

    void handleShortPress();
    void handleLongPress();
    void handleButtonPress();
    float readAdcAvg();  // Returns float for precision, round when storing
    void showState();
    void saveAndApply();
    void blink(uint8_t times, uint16_t onMs, uint16_t offMs);
    void announceTare(TareResult result);
    bool debounceEdge(uint8_t pin, int& lastState, int& stableState, uint32_t& lastTime);
};
