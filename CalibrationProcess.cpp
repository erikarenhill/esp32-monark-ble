#include "CalibrationProcess.h"

CalibrationProcess::CalibrationProcess(uint8_t btnPin, uint8_t adcPin, IDisplay* lcd, SettingsManager* settings, MonarkCalibration* calObj)
    : _btnPin(btnPin), _adcPin(adcPin), _lcd(lcd), _settings(settings), _calObj(calObj) {}

void CalibrationProcess::begin() {
    pinMode(_btnPin, INPUT_PULLUP);
}

void CalibrationProcess::startCalibration() {
    _state = WAIT_0KP;
    if (_lcd) {
        _lcd->showMessage("Calibration Req.", "Starting...     ");
        delay(1000);
    }
}

bool CalibrationProcess::isCalibrating() const {
    return _state != IDLE;
}

float CalibrationProcess::readAdcAvg() {
    // Quick 8-sample average - RC filter handles smoothing, returns calibrated millivolts
    // Discard first 2 readings to avoid spikes from ADC settling
    analogReadMilliVolts(_adcPin);
    analogReadMilliVolts(_adcPin);

    float sum = 0.0f;
    for (int i = 0; i < 8; i++) {
        sum += (float)analogReadMilliVolts(_adcPin);
    }
    yield();
    return sum / 8.0f;
}

void CalibrationProcess::saveAndApply() {
    // Save to NVS
    _settings->saveCalibration(_tempAdc[0], _tempAdc[1], _tempAdc[2], _tempAdc[3]);
    
    // Update live object (we need to add a setter to MonarkCalibration or just re-instantiate, 
    // but re-instantiating is hard because PowerReal holds the pointer.
    // Let's assume we can update it or we just rely on the next reboot. 
    // Ideally MonarkCalibration should have a updateValues method.
    // For now, we will cast or assume the user reboots, OR we add updateValues to MonarkCalibration.
    
    // Let's update the object directly if we can.
    _calObj->updateValues(_tempAdc[0], _tempAdc[1], _tempAdc[2], _tempAdc[3]);
}

void CalibrationProcess::handleButtonPress() {
    switch (_state) {
        case IDLE:
            _state = WAIT_0KP;
            _lcd->showMessage("Calibration Mode", "Release Button  ");
            delay(1000); // Simple debounce/wait for release
            break;

        // Order: 0kp -> 6kp -> 4kp -> 2kp (easier to remove weights)
        case WAIT_0KP:
            _tempAdc[0] = (int)roundf(readAdcAvg());
            _state = WAIT_6KP;
            break;

        case WAIT_6KP:
            _tempAdc[3] = (int)roundf(readAdcAvg());
            _state = WAIT_4KP;
            break;

        case WAIT_4KP:
            _tempAdc[2] = (int)roundf(readAdcAvg());
            _state = WAIT_2KP;
            break;

        case WAIT_2KP:
            _tempAdc[1] = (int)roundf(readAdcAvg());
            saveAndApply();
            _state = DONE;
            _doneStartTime = millis();
            break;

        case DONE:
            // Should not happen via button, it auto-exits
            break;
    }
}

void CalibrationProcess::showState() {
    if (!_lcd) return;  // No display, skip

    static State lastShownState = IDLE;

    // Show header only on state change
    if (_state != lastShownState) {
        lastShownState = _state;
        const char* header = nullptr;
        switch (_state) {
            case WAIT_0KP: header = "Set 0kp & Click"; break;
            case WAIT_2KP: header = "Set 2kp & Click"; break;
            case WAIT_4KP: header = "Set 4kp & Click"; break;
            case WAIT_6KP: header = "Set 6kp & Click"; break;
            case DONE:     header = "Calibration Done"; break;
            default: break;
        }
        if (header) {
            _lcd->showMessage(header, _state == DONE ? "Saved!" : "");
        }
    }

    // Update ADC value periodically (but not in DONE state)
    if (_state != DONE && _state != IDLE) {
        if (millis() - _lastDisplayTime < 300) return;
        _lastDisplayTime = millis();

        int currentAdc = readAdcAvg();
        char line2[17];
        snprintf(line2, sizeof(line2), "ADC: %d", currentAdc);
        _lcd->showMessage(nullptr, line2);
    }
}

void CalibrationProcess::update() {
    // Update display input (if display exists)
    if (_lcd) {
        _lcd->update();
    }

    // Button handling
    bool physicalPressed = false;
    if (_btnPin != 255) { // Assuming 255 is invalid/none
        int reading = digitalRead(_btnPin);
        if (reading != _lastBtnState) {
            _lastDebounceTime = millis();
        }
        if ((millis() - _lastDebounceTime) > DEBOUNCE_DELAY) {
            static int stableState = HIGH;
            if (reading != stableState) {
                stableState = reading;
                if (stableState == LOW) {
                    physicalPressed = true;
                }
            }
        }
        _lastBtnState = reading;
    }

    // Check touch input
    bool touchPressed = _lcd ? _lcd->isActionRequested() : false;

    if (physicalPressed || touchPressed) {
        handleButtonPress();
    }

    // Logic
    if (_state != IDLE) {
        if (_state == DONE) {
            showState();
            if (millis() - _doneStartTime > 3000) {
                _state = IDLE;
                if (_lcd) _lcd->showMessage("                ", "                "); // Clear
            }
        } else {
            showState();
        }
    }
}