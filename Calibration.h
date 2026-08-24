#pragma once
#include <stdint.h>

class ICalibration {
public:
    virtual ~ICalibration() {}
    virtual float adcToKp(float adc) = 0;
};

class MonarkCalibration : public ICalibration {
public:
    MonarkCalibration(int adc0, int adc2, int adc4, int adc6);
    float adcToKp(float adc) override;

    void updateValues(int adc0, int adc2, int adc4, int adc6) {
        _adc0 = adc0; _adc2 = adc2; _adc4 = adc4; _adc6 = adc6;
    }

    // Getters for calibration values
    int getAdc0() const { return _adc0; }
    int getAdc2() const { return _adc2; }
    int getAdc4() const { return _adc4; }
    int getAdc6() const { return _adc6; }

    // Zero offset (tare), in the same millivolt units as the anchors.
    // Subtracted from every reading, so it shifts the curve without
    // touching the span. Set by taring at 0 kp before a ride.
    void setZeroOffset(float mv) { _zeroOffset = mv; }
    float getZeroOffset() const { return _zeroOffset; }

    // Largest tare we accept: 1 kp worth of drift. Anything beyond that
    // means the tare was taken with load on the brake, not at rest.
    float maxZeroOffset() const {
        float perKp = (float)(_adc2 - _adc0) / 2.0f;
        return perKp > 0.0f ? perKp : 0.0f;
    }

    // Offset that would make `adc` read exactly 0 kp
    float offsetForZeroAt(float adc) const { return adc - (float)_adc0; }

private:
    int _adc0, _adc2, _adc4, _adc6;
    float _zeroOffset = 0.0f;

    float lerp(float x, float x0, float y0, float x1, float y1);
};
