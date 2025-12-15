#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include "Calibration.h"

class SettingsManager {
public:
    SettingsManager();
    void begin();
    
    // Save calibration values
    void saveCalibration(int adc0, int adc2, int adc4, int adc6);
    
    // Load calibration values (returns true if loaded, false if defaults used)
    bool loadCalibration(int& adc0, int& adc2, int& adc4, int& adc6);

private:
    Preferences preferences;
    const char* NAMESPACE = "monark";
};
