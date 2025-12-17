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

private:
    int _adc0, _adc2, _adc4, _adc6;
    
    float lerp(float x, float x0, float y0, float x1, float y1);
};
