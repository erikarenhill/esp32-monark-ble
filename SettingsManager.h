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

    // Device name (used for BLE and WiFi)
    void saveDeviceName(const char* name);
    String loadDeviceName(const char* defaultName = "MonarkPower");

    // Cycle constant
    void saveCycleConstant(float value);
    float loadCycleConstant(float defaultValue = 1.05f);

    // WiFi configuration
    void saveWiFi(const char* ssid, const char* password);
    bool loadWiFi(String& ssid, String& password);
    void clearWiFi();

    // Simulator mode
    void saveSimulatorMode(bool enabled);
    bool loadSimulatorMode(bool defaultValue = false);

private:
    Preferences preferences;
    const char* NAMESPACE = "monark";
};
