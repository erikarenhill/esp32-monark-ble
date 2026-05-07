#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include "PowerSource.h"
#include "SettingsManager.h"
#include "Calibration.h"

class PowerWebServer {
public:
    PowerWebServer(SettingsManager* settings, MonarkCalibration* calibration, uint8_t adcPin);

    void begin(const char* apPassword = "monark123");
    void updatePowerData(const PowerSample& sample);

    // Periodic housekeeping. Call from the main loop. While in AP mode with
    // saved STA credentials, this retries STA every few minutes so a flaky
    // first-boot association eventually succeeds without a reboot.
    void poll();

    String getIPAddress() const;
    String getDeviceName() const { return _deviceName; }
    bool isAPMode() const { return _isAPMode; }
    bool isConnected() const;

private:
    AsyncWebServer _server;
    SettingsManager* _settings;
    MonarkCalibration* _calibration;
    String _deviceName;
    String _apPassword;
    bool _isAPMode = true;
    uint8_t _adcPin;

    // Current power data
    PowerSample _lastSample;
    uint32_t _lastSampleTime = 0;

    // Web calibration state (order: 0kp -> 2kp -> 4kp -> 6kp, stock firmware order)
    enum CalibState { CAL_IDLE, CAL_0KP, CAL_2KP, CAL_4KP, CAL_6KP, CAL_DONE };
    CalibState _calState = CAL_IDLE;
    int _calValues[4] = {0, 0, 0, 0};  // 0kp, 2kp, 4kp, 6kp

    bool tryConnectWiFi();
    void startAPMode();

    // Background STA retry state — populated when we fall to AP at boot,
    // checked from poll() to attempt STA again periodically.
    uint32_t _lastStaRetryMs = 0;
    bool _forceStaRetry = false;
    static constexpr uint32_t STA_RETRY_INTERVAL_MS = 5UL * 60UL * 1000UL; // 5 min
    void retryStaIfNeeded();
    void setupRoutes();
    void handleGetPower(AsyncWebServerRequest* request);
    void handleGetCalibration(AsyncWebServerRequest* request);
    void handleSetCalibration(AsyncWebServerRequest* request, uint8_t* data, size_t len);
    void handleGetDeviceName(AsyncWebServerRequest* request);
    void handleSetDeviceName(AsyncWebServerRequest* request, uint8_t* data, size_t len);
    void handleGetWiFi(AsyncWebServerRequest* request);
    void handleSetWiFi(AsyncWebServerRequest* request, uint8_t* data, size_t len);
    void handleClearWiFi(AsyncWebServerRequest* request);

    // Calibration wizard
    void handleCalibrationStatus(AsyncWebServerRequest* request);
    void handleCalibrationStart(AsyncWebServerRequest* request);
    void handleCalibrationNext(AsyncWebServerRequest* request);
    void handleCalibrationCancel(AsyncWebServerRequest* request);

    // OTA Update
    void handleUpdate(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final);
};
