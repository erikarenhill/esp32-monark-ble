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

    // Web calibration state (order: 0kp -> 6kp -> 4kp -> 2kp)
    enum CalibState { CAL_IDLE, CAL_0KP, CAL_6KP, CAL_4KP, CAL_2KP, CAL_DONE };
    CalibState _calState = CAL_IDLE;
    int _calValues[4] = {0, 0, 0, 0};  // 0kp, 2kp, 4kp, 6kp

    float readAdcQuick();  // Quick read (8 samples)
    float readAdcSmoothed();  // Smoothed read for calibration display (~1s)
    float readAdcAvg();    // 5-second smoothed read for calibration capture

    // ADC smoothing buffer for calibration display (~1 second at 200ms polling)
    float _calAdcBuffer[20] = {0};
    uint8_t _calAdcHead = 0;
    uint8_t _calAdcCount = 0;

    bool tryConnectWiFi();
    void startAPMode();
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
