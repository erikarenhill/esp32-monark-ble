#include "Calibration.h"

MonarkCalibration::MonarkCalibration(int adc0, int adc2, int adc4, int adc6)
    : _adc0(adc0), _adc2(adc2), _adc4(adc4), _adc6(adc6) {}

float MonarkCalibration::lerp(float x, float x0, float y0, float x1, float y1) {
    if (x1 == x0) return y0;
    float t = (x - x0) / (x1 - x0);
    return y0 + t * (y1 - y0);
}

float MonarkCalibration::adcToKp(float adc) {
    // Below 0kp calibration point - clamp to 0
    if (adc <= (float)_adc0) return 0.0f;

    // Interpolate between calibration points
    if (adc <= (float)_adc2)
        return lerp(adc, (float)_adc0, 0.0f, (float)_adc2, 2.0f);
    else if (adc <= (float)_adc4)
        return lerp(adc, (float)_adc2, 2.0f, (float)_adc4, 4.0f);
    else
        // For adc > _adc4, extrapolate using the 4-6kp slope
        // This allows readings beyond 6kp
        return lerp(adc, (float)_adc4, 4.0f, (float)_adc6, 6.0f);
}
