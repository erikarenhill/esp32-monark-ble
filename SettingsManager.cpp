#include "SettingsManager.h"

SettingsManager::SettingsManager() {}

void SettingsManager::begin() {
    // Preferences library handles NVS initialization automatically on ESP32
}

void SettingsManager::saveCalibration(int adc0, int adc2, int adc4, int adc6) {
    preferences.begin(NAMESPACE, false); // false = read/write
    preferences.putInt("adc0", adc0);
    preferences.putInt("adc2", adc2);
    preferences.putInt("adc4", adc4);
    preferences.putInt("adc6", adc6);
    preferences.end();
}

bool SettingsManager::loadCalibration(int& adc0, int& adc2, int& adc4, int& adc6) {
    preferences.begin(NAMESPACE, true); // true = read-only
    
    // Check if keys exist (simple check using one key)
    if (!preferences.isKey("adc0")) {
        preferences.end();
        return false;
    }

    adc0 = preferences.getInt("adc0", 78);
    adc2 = preferences.getInt("adc2", 125);
    adc4 = preferences.getInt("adc4", 177);
    adc6 = preferences.getInt("adc6", 226);
    
    preferences.end();
    return true;
}
