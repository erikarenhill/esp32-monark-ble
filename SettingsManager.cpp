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

void SettingsManager::saveDeviceName(const char* name) {
    preferences.begin(NAMESPACE, false);
    preferences.putString("devname", name);
    preferences.end();
}

String SettingsManager::loadDeviceName(const char* defaultName) {
    preferences.begin(NAMESPACE, true);
    String name = preferences.getString("devname", defaultName);
    preferences.end();
    return name;
}

void SettingsManager::saveCycleConstant(float value) {
    preferences.begin(NAMESPACE, false);
    preferences.putFloat("cyclec", value);
    preferences.end();
}

float SettingsManager::loadCycleConstant(float defaultValue) {
    preferences.begin(NAMESPACE, true);
    float value = preferences.getFloat("cyclec", defaultValue);
    preferences.end();
    return value;
}

void SettingsManager::saveWiFi(const char* ssid, const char* password) {
    preferences.begin(NAMESPACE, false);
    preferences.putString("wifi_ssid", ssid);
    preferences.putString("wifi_pass", password);
    preferences.end();
}

bool SettingsManager::loadWiFi(String& ssid, String& password) {
    preferences.begin(NAMESPACE, true);
    if (!preferences.isKey("wifi_ssid")) {
        preferences.end();
        return false;
    }
    ssid = preferences.getString("wifi_ssid", "");
    password = preferences.getString("wifi_pass", "");
    preferences.end();
    return ssid.length() > 0;
}

void SettingsManager::clearWiFi() {
    preferences.begin(NAMESPACE, false);
    preferences.remove("wifi_ssid");
    preferences.remove("wifi_pass");
    preferences.end();
}

void SettingsManager::saveSimulatorMode(bool enabled) {
    preferences.begin(NAMESPACE, false);
    preferences.putBool("simulator", enabled);
    preferences.end();
}

bool SettingsManager::loadSimulatorMode(bool defaultValue) {
    preferences.begin(NAMESPACE, true);
    bool value = preferences.getBool("simulator", defaultValue);
    preferences.end();
    return value;
}
