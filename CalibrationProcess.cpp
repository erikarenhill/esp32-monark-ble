#include "CalibrationProcess.h"

CalibrationProcess::CalibrationProcess(uint8_t btnPin, uint8_t adcPin, IDisplay* lcd, SettingsManager* settings,
                                       MonarkCalibration* calObj, uint8_t tareBtnPin, uint8_t ledPin,
                                       PowerSource* power)
    : _btnPin(btnPin), _adcPin(adcPin), _lcd(lcd), _settings(settings), _calObj(calObj),
      _tareBtnPin(tareBtnPin), _ledPin(ledPin), _power(power) {}

void CalibrationProcess::begin() {
    if (_btnPin != 255) pinMode(_btnPin, INPUT_PULLUP);
    if (_tareBtnPin != 255) pinMode(_tareBtnPin, INPUT_PULLUP);
    if (_ledPin != 255) {
        pinMode(_ledPin, OUTPUT);
        digitalWrite(_ledPin, LOW);
    }
    Serial.printf("Buttons: cal=%d tare=%d led=%d\n", _btnPin, _tareBtnPin, _ledPin);
    Serial.println("Short press = zero/tare, hold 3s = full calibration");
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

// ------------------ Tare ------------------

TareResult CalibrationProcess::performTare() {
    if (!_calObj || !_settings) return TareResult::NoCalibration;

    // Zeroing only means anything at a standstill: a bumped button mid-ride
    // must not quietly rewrite the zero point.
    if (_power && _power->getCurrentRpm() > MAX_TARE_RPM) {
        Serial.printf("Tare refused: still pedalling (%.0f rpm)\n", _power->getCurrentRpm());
        return TareResult::Moving;
    }

    float maxOff = _calObj->maxZeroOffset();
    if (maxOff <= 0.0f) {
        Serial.println("Tare: no valid calibration span, run full calibration first");
        return TareResult::NoCalibration;
    }

    // Average over ~500 ms rather than one burst: the pendulum settles slowly
    // and a single 8-sample read is all taken inside one millisecond.
    float sum = 0.0f;
    const int N = 30;
    for (int i = 0; i < N; i++) {
        sum += readAdcAvg();
        delay(10);
    }
    float adc = sum / (float)N;

    float offset = _calObj->offsetForZeroAt(adc);
    float perKp = (float)(_calObj->getAdc2() - _calObj->getAdc0()) / 2.0f;

    if (fabsf(offset) > maxOff) {
        Serial.printf("Tare REJECTED: %.1f mV (%.2f kp) exceeds limit %.1f mV. Brake loaded or bike moving?\n",
                      offset, perKp > 0.0f ? offset / perKp : 0.0f, maxOff);
        return TareResult::Rejected;
    }

    _calObj->setZeroOffset(offset);
    _settings->saveZeroOffset(offset);
    Serial.printf("Tare OK: adc=%.1f mV -> offset %+.1f mV (%+.2f kp)\n",
                  adc, offset, perKp > 0.0f ? offset / perKp : 0.0f);
    return TareResult::Ok;
}

void CalibrationProcess::blink(uint8_t times, uint16_t onMs, uint16_t offMs) {
    if (_ledPin == 255) return;
    for (uint8_t i = 0; i < times; i++) {
        digitalWrite(_ledPin, HIGH);
        delay(onMs);
        digitalWrite(_ledPin, LOW);
        delay(offMs);
    }
}

void CalibrationProcess::announceTare(TareResult result) {
    const char* line1 = "Zero / Tare";
    const char* line2 = "";
    switch (result) {
        case TareResult::Ok:
            line2 = "Zeroed OK";
            blink(2, 150, 150);
            break;
        case TareResult::Rejected:
            line2 = "Rejected!";
            blink(5, 60, 60);
            break;
        case TareResult::NoCalibration:
            line2 = "Calibrate 1st";
            blink(5, 60, 60);
            break;
        case TareResult::Moving:
            line2 = "Stop pedalling";
            blink(5, 60, 60);
            break;
    }
    if (_lcd) {
        _lcd->showMessage(line1, line2);
        _tareMsgUntil = millis() + 2000;
    }
}

// ------------------ Buttons ------------------

bool CalibrationProcess::debounceEdge(uint8_t pin, int& lastState, int& stableState, uint32_t& lastTime) {
    int reading = digitalRead(pin);
    uint32_t now = millis();
    if (reading != lastState) lastTime = now;
    lastState = reading;
    if ((now - lastTime) > DEBOUNCE_DELAY && reading != stableState) {
        stableState = reading;
        return (stableState == LOW);  // falling edge = pressed
    }
    return false;
}

void CalibrationProcess::handleShortPress() {
    if (_state == IDLE) {
        announceTare(performTare());
    } else {
        handleButtonPress();
    }
}

void CalibrationProcess::handleLongPress() {
    if (_state == IDLE) {
        Serial.println("Long press: starting full calibration");
        startCalibration();
    } else {
        Serial.println("Long press: calibration cancelled");
        _state = IDLE;
        if (_lcd) _lcd->showMessage("Calibration     ", "Cancelled       ");
        blink(3, 80, 80);
    }
}

void CalibrationProcess::saveAndApply() {
    // Save to NVS
    _settings->saveCalibration(_tempAdc[0], _tempAdc[1], _tempAdc[2], _tempAdc[3]);

    // A fresh 4-point calibration supersedes any tare offset
    _settings->saveZeroOffset(0.0f);

    // Update live object
    _calObj->updateValues(_tempAdc[0], _tempAdc[1], _tempAdc[2], _tempAdc[3]);
    _calObj->setZeroOffset(0.0f);
}

void CalibrationProcess::handleButtonPress() {
    switch (_state) {
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

        case IDLE:
        case DONE:
            // IDLE is handled as a tare; DONE auto-exits
            break;
    }
}

void CalibrationProcess::showState() {
    if (!_lcd) return;  // No display, skip
    if (millis() < _tareMsgUntil) return;  // let tare feedback linger

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

        int currentAdc = (int)roundf(readAdcAvg());
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

    uint32_t now = millis();

    // Calibration button: short press = tare, hold = full calibration
    if (_btnPin != 255) {
        int reading = digitalRead(_btnPin);
        if (reading != _lastBtnState) _lastDebounceTime = now;
        _lastBtnState = reading;

        if ((now - _lastDebounceTime) > DEBOUNCE_DELAY && reading != _stableBtnState) {
            _stableBtnState = reading;
            if (_stableBtnState == LOW) {
                _btnDown = true;
                _pressStartMs = now;
                _longFired = false;
            } else {
                if (_btnDown && !_longFired) handleShortPress();
                _btnDown = false;
            }
        }

        // Fire the long press while the button is still held, so the user
        // gets feedback and can simply let go.
        if (_btnDown && !_longFired && (now - _pressStartMs) >= LONG_PRESS_MS) {
            _longFired = true;
            handleLongPress();
        }
    }

    // Optional dedicated tare button: single press, no gesture needed
    if (_tareBtnPin != 255) {
        if (debounceEdge(_tareBtnPin, _lastTareBtnState, _stableTareBtnState, _lastTareDebounceTime)) {
            if (_state == IDLE) {
                announceTare(performTare());
            } else {
                handleButtonPress();
            }
        }
    }

    // Touch input counts as a short press
    if (_lcd && _lcd->isActionRequested()) {
        handleShortPress();
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
