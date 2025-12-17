#include "PowerWebServer.h"

PowerWebServer::PowerWebServer(SettingsManager* settings, MonarkCalibration* calibration, uint8_t adcPin)
    : _server(80), _settings(settings), _calibration(calibration), _adcPin(adcPin) {
    memset(&_lastSample, 0, sizeof(_lastSample));
}

float PowerWebServer::readAdcQuick() {
    // Quick ADC read (8 samples)
    float sum = 0.0f;
    for (int i = 0; i < 8; i++) {
        sum += (float)analogRead(_adcPin);
    }
    yield();  // Let other tasks run
    return sum / 8.0f;
}

float PowerWebServer::readAdcSmoothed() {
    // Add new quick reading to ring buffer
    float newReading = readAdcQuick();
    _calAdcBuffer[_calAdcHead] = newReading;
    _calAdcHead = (_calAdcHead + 1) % 20;
    if (_calAdcCount < 20) _calAdcCount++;

    // Return average of buffer (~1 second of readings)
    float sum = 0.0f;
    for (uint8_t i = 0; i < _calAdcCount; i++) {
        sum += _calAdcBuffer[i];
    }
    return sum / (float)_calAdcCount;
}

float PowerWebServer::readAdcAvg() {
    // Use the already-smoothed buffer (~1 second of readings)
    // The RC filter + smoothing buffer provides stable readings
    // No blocking needed - just return current smoothed value
    return readAdcSmoothed();
}

void PowerWebServer::begin(const char* apPassword) {
    _deviceName = _settings->loadDeviceName("MonarkPower");
    _apPassword = apPassword;

    // Try to connect to saved WiFi first
    if (!tryConnectWiFi()) {
        // Fall back to AP mode
        startAPMode();
    }

    setupRoutes();
    _server.begin();
    Serial.println("Web server started on port 80");
}

bool PowerWebServer::tryConnectWiFi() {
    String ssid, password;
    if (!_settings->loadWiFi(ssid, password)) {
        Serial.println("No WiFi credentials saved");
        return false;
    }

    Serial.printf("Connecting to WiFi: %s\n", ssid.c_str());
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), password.c_str());

    // Wait up to 10 seconds for connection
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        _isAPMode = false;
        Serial.print("Connected to WiFi. IP: ");
        Serial.println(WiFi.localIP());
        return true;
    }

    Serial.println("WiFi connection failed");
    WiFi.disconnect();
    return false;
}

void PowerWebServer::startAPMode() {
    _isAPMode = true;

    // Disconnect any previous connection
    WiFi.disconnect(true);
    delay(100);

    // Set AP mode
    WiFi.mode(WIFI_AP);
    delay(100);

    // Start soft AP
    bool result = WiFi.softAP(_deviceName.c_str(), _apPassword.c_str());

    if (result) {
        Serial.println("WiFi AP started successfully");
        Serial.print("SSID: ");
        Serial.println(_deviceName);
        Serial.print("Password: ");
        Serial.println(_apPassword);
        Serial.print("IP Address: ");
        Serial.println(WiFi.softAPIP());
    } else {
        Serial.println("ERROR: Failed to start WiFi AP!");
    }
}

String PowerWebServer::getIPAddress() const {
    if (_isAPMode) {
        return WiFi.softAPIP().toString();
    }
    return WiFi.localIP().toString();
}

bool PowerWebServer::isConnected() const {
    if (_isAPMode) {
        return WiFi.softAPgetStationNum() > 0;
    }
    return WiFi.status() == WL_CONNECTED;
}

void PowerWebServer::updatePowerData(const PowerSample& sample) {
    _lastSample = sample;
    _lastSampleTime = millis();
}

void PowerWebServer::setupRoutes() {
    // GET /api/power - returns current power data
    _server.on("/api/power", HTTP_GET, [this](AsyncWebServerRequest* request) {
        handleGetPower(request);
    });

    // GET /api/calibration - returns calibration values
    _server.on("/api/calibration", HTTP_GET, [this](AsyncWebServerRequest* request) {
        handleGetCalibration(request);
    });

    // POST /api/calibration - saves calibration values
    _server.on("/api/calibration", HTTP_POST,
        [](AsyncWebServerRequest* request) {},
        nullptr,
        [this](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            handleSetCalibration(request, data, len);
        }
    );

    // GET /api/device - returns device name
    _server.on("/api/device", HTTP_GET, [this](AsyncWebServerRequest* request) {
        handleGetDeviceName(request);
    });

    // POST /api/device - saves device name
    _server.on("/api/device", HTTP_POST,
        [](AsyncWebServerRequest* request) {},
        nullptr,
        [this](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            handleSetDeviceName(request, data, len);
        }
    );

    // GET /api/wifi - returns WiFi status and saved SSID
    _server.on("/api/wifi", HTTP_GET, [this](AsyncWebServerRequest* request) {
        handleGetWiFi(request);
    });

    // POST /api/wifi - saves WiFi credentials
    _server.on("/api/wifi", HTTP_POST,
        [](AsyncWebServerRequest* request) {},
        nullptr,
        [this](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            handleSetWiFi(request, data, len);
        }
    );

    // DELETE /api/wifi - clears WiFi credentials
    _server.on("/api/wifi", HTTP_DELETE, [this](AsyncWebServerRequest* request) {
        handleClearWiFi(request);
    });

    // Reboot endpoint
    _server.on("/api/reboot", HTTP_POST, [this](AsyncWebServerRequest* request) {
        request->send(200, "application/json", "{\"success\":true,\"message\":\"Rebooting...\"}");
        delay(500);
        ESP.restart();
    });

    // Simulator mode endpoints
    _server.on("/api/simulator", HTTP_GET, [this](AsyncWebServerRequest* request) {
        JsonDocument doc;
        doc["enabled"] = _settings->loadSimulatorMode(false);
        String json;
        serializeJson(doc, json);
        request->send(200, "application/json", json);
    });

    _server.on("/api/simulator", HTTP_POST,
        [](AsyncWebServerRequest* request) {},
        nullptr,
        [this](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            JsonDocument doc;
            DeserializationError error = deserializeJson(doc, data, len);
            if (error) {
                request->send(400, "application/json", "{\"success\":false,\"error\":\"Invalid JSON\"}");
                return;
            }
            bool enabled = doc["enabled"] | false;
            _settings->saveSimulatorMode(enabled);
            Serial.printf("Simulator mode set to: %s\n", enabled ? "ON" : "OFF");
            request->send(200, "application/json", "{\"success\":true,\"message\":\"Restart required\"}");
        }
    );

    // Combined status endpoint (power + calibration) - poll this at 1Hz
    _server.on("/api/status", HTTP_GET, [this](AsyncWebServerRequest* request) {
        JsonDocument doc;

        // Power data
        doc["power"] = _lastSample.power_w;
        doc["rpm"] = _lastSample.rpm;
        doc["kp"] = _lastSample.kp;
        doc["adc"] = _lastSample.adc_raw;

        // Calibration state
        const char* calStates[] = {"idle", "0kp", "6kp", "4kp", "2kp", "done"};
        doc["cal"]["state"] = calStates[_calState];
        doc["cal"]["step"] = (int)_calState;
        doc["cal"]["adc"] = readAdcSmoothed();
        doc["cal"]["values"]["adc0"] = _calValues[0];
        doc["cal"]["values"]["adc2"] = _calValues[1];
        doc["cal"]["values"]["adc4"] = _calValues[2];
        doc["cal"]["values"]["adc6"] = _calValues[3];

        String json;
        serializeJson(doc, json);
        request->send(200, "application/json", json);
    });

    // Calibration wizard endpoints
    _server.on("/api/calibrate/start", HTTP_POST, [this](AsyncWebServerRequest* request) {
        handleCalibrationStart(request);
    });
    _server.on("/api/calibrate/next", HTTP_POST, [this](AsyncWebServerRequest* request) {
        handleCalibrationNext(request);
    });
    _server.on("/api/calibrate/cancel", HTTP_POST, [this](AsyncWebServerRequest* request) {
        handleCalibrationCancel(request);
    });

    // Simple web page
    _server.on("/", HTTP_GET, [this](AsyncWebServerRequest* request) {
        String html = R"rawhtml(
<!DOCTYPE html>
<html>
<head>
    <title>Monark Power</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body { font-family: Arial, sans-serif; margin: 20px; background: #1a1a2e; color: #eee; }
        .card { background: #16213e; padding: 20px; border-radius: 10px; margin: 10px 0; }
        .value { font-size: 48px; font-weight: bold; color: #4ecca3; }
        .label { font-size: 14px; color: #888; }
        .row { display: flex; gap: 20px; flex-wrap: wrap; }
        .col { flex: 1; min-width: 120px; }
        input { padding: 10px; margin: 5px 0; width: 100%; box-sizing: border-box; background: #0f3460; border: 1px solid #4ecca3; color: #eee; border-radius: 5px; }
        button { padding: 15px 30px; background: #4ecca3; border: none; border-radius: 5px; cursor: pointer; font-size: 16px; margin-top: 10px; }
        button:hover { background: #3db892; }
        h2 { color: #4ecca3; margin-top: 0; }
        .status { margin-left: 10px; }
        .success { color: #4ecca3; }
        .error { color: #e94560; }
    </style>
</head>
<body>
    <h1 id="title">Monark Power Meter</h1>

    <div class="card">
        <div class="row">
            <div class="col">
                <div class="label">Power</div>
                <div class="value" id="power">--</div>
                <div class="label">watts</div>
            </div>
            <div class="col">
                <div class="label">Cadence</div>
                <div class="value" id="rpm">--</div>
                <div class="label">rpm</div>
            </div>
            <div class="col">
                <div class="label">Resistance</div>
                <div class="value" id="kp">--</div>
                <div class="label">kp</div>
            </div>
            <div class="col">
                <div class="label">ADC Raw</div>
                <div class="value" id="adc" style="font-size:32px;">--</div>
                <div class="label">&nbsp;</div>
            </div>
        </div>
    </div>

    <div class="card">
        <h2>Device Settings</h2>
        <label>Device Name (BLE & WiFi AP)<br>
            <input type="text" id="deviceName" maxlength="20" placeholder="MonarkPower">
        </label>
        <button onclick="saveDeviceName()">Save Name</button>
        <span id="nameStatus" class="status"></span>
        <p style="font-size:12px;color:#888;">Restart required after changing name</p>

        <div style="margin-top:20px;padding-top:20px;border-top:1px solid #0f3460;">
            <label style="display:flex;align-items:center;cursor:pointer;">
                <input type="checkbox" id="simulatorMode" onchange="saveSimulatorMode()" style="width:auto;margin-right:10px;">
                <span>Simulator Mode</span>
            </label>
            <span id="simStatus" class="status"></span>
            <p style="font-size:12px;color:#888;">Uses simulated power data instead of real sensors. Restart required.</p>
        </div>
    </div>

    <div class="card">
        <h2>WiFi Connection</h2>
        <div id="wifiStatus" style="margin-bottom:15px;padding:10px;background:#0f3460;border-radius:5px;">
            <span id="wifiMode">Loading...</span><br>
            <span style="font-size:12px;color:#888;">IP: <span id="wifiIP">--</span></span>
        </div>
        <label>Network SSID<br>
            <input type="text" id="wifiSSID" maxlength="32" placeholder="Your WiFi network">
        </label>
        <label>Password<br>
            <input type="password" id="wifiPass" maxlength="63" placeholder="WiFi password">
        </label>
        <button onclick="saveWiFi()">Connect to WiFi</button>
        <button onclick="clearWiFi()" style="background:#e94560;margin-left:10px;">Use AP Mode</button>
        <span id="wifiSaveStatus" class="status"></span>
        <p style="font-size:12px;color:#888;">Restart required after changing WiFi settings</p>
    </div>

    <div class="card">
        <h2>Calibration Wizard</h2>
        <div id="calWizard">
            <div id="calInstructions" style="padding:15px;background:#0f3460;border-radius:5px;margin-bottom:15px;">
                <strong id="calStep">Ready to calibrate</strong><br>
                <span id="calMessage">Click Start to begin calibration process</span>
            </div>
            <div id="calAdcRow" style="margin-bottom:15px;display:none;">
                <span style="color:#888;">Current ADC: </span>
                <span id="calAdc" style="font-size:24px;color:#4ecca3;">--</span>
            </div>
            <button id="calStartBtn" onclick="startCalibration()">Start Calibration</button>
            <button id="calNextBtn" onclick="nextCalibration()" style="display:none;">Next Step</button>
            <button id="calCancelBtn" onclick="cancelCalibration()" style="background:#e94560;display:none;margin-left:10px;">Cancel</button>
        </div>
    </div>

    <div class="card">
        <h2>Manual Calibration</h2>
        <div class="row">
            <div class="col"><label>0 kp ADC<br><input type="number" id="adc0"></label></div>
            <div class="col"><label>2 kp ADC<br><input type="number" id="adc2"></label></div>
            <div class="col"><label>4 kp ADC<br><input type="number" id="adc4"></label></div>
            <div class="col"><label>6 kp ADC<br><input type="number" id="adc6"></label></div>
        </div>
        <div class="row" style="margin-top:15px;">
            <div class="col"><label>Cycle Constant<br><input type="number" id="cycleConstant" step="0.01" min="0.5" max="2.0"></label></div>
            <div class="col"></div>
            <div class="col"></div>
            <div class="col"></div>
        </div>
        <button onclick="saveCalibration()">Save Calibration</button>
        <span id="calStatus" class="status"></span>
        <p style="font-size:12px;color:#888;">Restart required for cycle constant change</p>
    </div>

    <div class="card" style="text-align:center;">
        <h2>Device Control</h2>
        <button onclick="rebootDevice()" style="background:#e94560;padding:20px 40px;font-size:18px;">Reboot Device</button>
        <span id="rebootStatus" class="status"></span>
        <p style="font-size:12px;color:#888;margin-top:15px;">Required after changing device name or WiFi settings</p>
    </div>

    <script>
        // Single unified status fetch at 1Hz
        async function fetchStatus() {
            try {
                const res = await fetch('/api/status');
                const data = await res.json();

                // Power display
                document.getElementById('power').textContent = Math.round(data.power);
                document.getElementById('rpm').textContent = Math.round(data.rpm);
                document.getElementById('kp').textContent = data.kp.toFixed(2);
                document.getElementById('adc').textContent = data.adc.toFixed(2);

                // Calibration wizard
                updateCalibrationUI(data.cal);
            } catch (e) {}
        }

        function updateCalibrationUI(cal) {
            const stepNum = cal.step;
            const kpOrder = [0, 0, 6, 4, 2, 0];

            // Only show live ADC when actively calibrating
            if (stepNum >= 1 && stepNum <= 4) {
                document.getElementById('calAdcRow').style.display = 'block';
                document.getElementById('calAdc').textContent = cal.adc.toFixed(2);
            } else {
                document.getElementById('calAdcRow').style.display = 'none';
            }

            if (stepNum === 0) {
                document.getElementById('calStep').textContent = 'Ready to calibrate';
                document.getElementById('calMessage').textContent = 'Click Start to begin (0 -> 6 -> 4 -> 2 kp)';
                document.getElementById('calStartBtn').style.display = 'inline-block';
                document.getElementById('calNextBtn').style.display = 'none';
                document.getElementById('calCancelBtn').style.display = 'none';
            } else if (stepNum >= 1 && stepNum <= 4) {
                const kpVal = kpOrder[stepNum];
                document.getElementById('calStep').textContent = 'Step ' + stepNum + '/4: Set ' + kpVal + ' kp';
                document.getElementById('calMessage').textContent = 'Position pendulum at ' + kpVal + ' kp, click Next';
                document.getElementById('calStartBtn').style.display = 'none';
                document.getElementById('calNextBtn').style.display = 'inline-block';
                document.getElementById('calCancelBtn').style.display = 'inline-block';
            } else if (stepNum === 5) {
                document.getElementById('calStep').textContent = 'Calibration Complete!';
                document.getElementById('calMessage').textContent = '0kp=' + cal.values.adc0 + ' 2kp=' + cal.values.adc2 + ' 4kp=' + cal.values.adc4 + ' 6kp=' + cal.values.adc6;
                document.getElementById('calStartBtn').style.display = 'inline-block';
                document.getElementById('calNextBtn').style.display = 'none';
                document.getElementById('calCancelBtn').style.display = 'none';
                fetchCalibration();
            }
        }

        async function fetchCalibration() {
            try {
                const res = await fetch('/api/calibration');
                const data = await res.json();
                document.getElementById('adc0').value = data.adc0;
                document.getElementById('adc2').value = data.adc2;
                document.getElementById('adc4').value = data.adc4;
                document.getElementById('adc6').value = data.adc6;
                document.getElementById('cycleConstant').value = data.cycleConstant;
            } catch (e) {}
        }

        async function fetchDeviceName() {
            try {
                const res = await fetch('/api/device');
                const data = await res.json();
                document.getElementById('deviceName').value = data.name;
                document.getElementById('title').textContent = data.name;
            } catch (e) {}
        }

        async function fetchSimulatorMode() {
            try {
                const res = await fetch('/api/simulator');
                const data = await res.json();
                document.getElementById('simulatorMode').checked = data.enabled;
            } catch (e) {}
        }

        async function saveSimulatorMode() {
            const status = document.getElementById('simStatus');
            const enabled = document.getElementById('simulatorMode').checked;
            try {
                const res = await fetch('/api/simulator', {
                    method: 'POST',
                    headers: {'Content-Type': 'application/json'},
                    body: JSON.stringify({ enabled: enabled })
                });
                const result = await res.json();
                status.textContent = result.success ? 'Saved! Restart required.' : (result.error || 'Error');
                status.className = 'status ' + (result.success ? 'success' : 'error');
                setTimeout(function() { status.textContent = ""; }, 3000);
            } catch (e) {
                status.textContent = 'Network error';
                status.className = 'status error';
            }
        }

        async function saveCalibration() {
            const status = document.getElementById('calStatus');
            const data = {
                adc0: parseInt(document.getElementById('adc0').value),
                adc2: parseInt(document.getElementById('adc2').value),
                adc4: parseInt(document.getElementById('adc4').value),
                adc6: parseInt(document.getElementById('adc6').value),
                cycleConstant: parseFloat(document.getElementById('cycleConstant').value)
            };
            try {
                const res = await fetch('/api/calibration', {
                    method: 'POST',
                    headers: {'Content-Type': 'application/json'},
                    body: JSON.stringify(data)
                });
                const result = await res.json();
                status.textContent = result.success ? 'Saved!' : (result.error || 'Error');
                status.className = 'status ' + (result.success ? 'success' : 'error');
                setTimeout(function() { status.textContent = ""; }, 3000);
            } catch (e) {
                status.textContent = 'Error';
                status.className = 'status error';
            }
        }

        async function saveDeviceName() {
            const status = document.getElementById('nameStatus');
            const name = document.getElementById('deviceName').value.trim();
            if (!name || name.length > 20) {
                status.textContent = 'Name must be 1-20 characters';
                status.className = 'status error';
                return;
            }
            try {
                const res = await fetch('/api/device', {
                    method: 'POST',
                    headers: {'Content-Type': 'application/json'},
                    body: JSON.stringify({ name: name })
                });
                const result = await res.json();
                status.textContent = result.success ? 'Saved! Restart device.' : (result.error || 'Error');
                status.className = 'status ' + (result.success ? 'success' : 'error');
                if (result.success) {
                    document.getElementById('title').textContent = name;
                }
                setTimeout(function() { status.textContent = ""; }, 3000);
            } catch (e) {
                status.textContent = 'Network error';
                status.className = 'status error';
            }
        }

        async function fetchWiFi() {
            try {
                const res = await fetch('/api/wifi');
                const data = await res.json();
                document.getElementById('wifiSSID').value = data.ssid || "";
                document.getElementById('wifiIP').textContent = data.ip;
                if (data.isAPMode) {
                    document.getElementById('wifiMode').innerHTML = '<span style="color:#e94560;">AP Mode</span> (Direct connection)';
                } else if (data.connected) {
                    document.getElementById('wifiMode').innerHTML = '<span style="color:#4ecca3;">Connected</span> to ' + data.ssid;
                } else {
                    document.getElementById('wifiMode').innerHTML = '<span style="color:#e94560;">Disconnected</span>';
                }
            } catch (e) {}
        }

        async function saveWiFi() {
            const status = document.getElementById('wifiSaveStatus');
            const ssid = document.getElementById('wifiSSID').value.trim();
            const password = document.getElementById('wifiPass').value;
            if (!ssid) {
                status.textContent = 'SSID required';
                status.className = 'status error';
                return;
            }
            try {
                const res = await fetch('/api/wifi', {
                    method: 'POST',
                    headers: {'Content-Type': 'application/json'},
                    body: JSON.stringify({ ssid: ssid, password: password })
                });
                const result = await res.json();
                status.textContent = result.success ? 'Saved! Restart device.' : (result.error || 'Error');
                status.className = 'status ' + (result.success ? 'success' : 'error');
            } catch (e) {
                status.textContent = 'Error';
                status.className = 'status error';
            }
        }

        async function clearWiFi() {
            const status = document.getElementById('wifiSaveStatus');
            try {
                const res = await fetch('/api/wifi', { method: 'DELETE' });
                const result = await res.json();
                status.textContent = result.success ? 'Cleared! Restart for AP mode.' : 'Error';
                status.className = 'status ' + (result.success ? 'success' : 'error');
                document.getElementById('wifiSSID').value = "";
                document.getElementById('wifiPass').value = "";
            } catch (e) {
                status.textContent = 'Error';
                status.className = 'status error';
            }
        }

        // Calibration Wizard
        async function startCalibration() {
            try {
                const res = await fetch('/api/calibrate/start', { method: 'POST' });
                const result = await res.json();
            } catch (e) {}
        }

        async function nextCalibration() {
            const btn = document.getElementById('calNextBtn');
            btn.disabled = true;
            btn.textContent = 'Capturing...';
            try {
                const res = await fetch('/api/calibrate/next', { method: 'POST' });
                const result = await res.json();
            } catch (e) {}
            btn.disabled = false;
            btn.textContent = 'Next Step';
        }

        async function cancelCalibration() {
            try {
                const res = await fetch('/api/calibrate/cancel', { method: 'POST' });
            } catch (e) {}
        }

        async function rebootDevice() {
            const status = document.getElementById('rebootStatus');
            if (!confirm('Reboot the device now?')) return;
            try {
                status.textContent = 'Rebooting...';
                status.className = 'status success';
                await fetch('/api/reboot', { method: 'POST' });
            } catch (e) {}
            startRebootCountdown();
        }

        function startRebootCountdown() {
            const status = document.getElementById('rebootStatus');
            let seconds = 10;
            status.textContent = 'Reloading in ' + seconds + 's...';
            status.className = 'status success';
            const interval = setInterval(function() {
                seconds--;
                if (seconds <= 0) {
                    clearInterval(interval);
                    status.textContent = 'Reloading...';
                    location.reload();
                } else {
                    status.textContent = 'Reloading in ' + seconds + 's...';
                }
            }, 1000);
        }

        // Initial fetches (one-time)
        fetchDeviceName();
        fetchSimulatorMode();
        fetchCalibration();
        fetchWiFi();

        // Single 1Hz polling for power + calibration status
        fetchStatus();
        setInterval(fetchStatus, 1000);
    </script>
</body>
</html>
)rawhtml";
        request->send(200, "text/html", html);
    });
}

void PowerWebServer::handleGetPower(AsyncWebServerRequest* request) {
    JsonDocument doc;
    doc["power"] = _lastSample.power_w;
    doc["rpm"] = _lastSample.rpm;
    doc["kp"] = _lastSample.kp;
    doc["adc_raw"] = _lastSample.adc_raw;
    doc["crank_revs"] = _lastSample.crank_revs;
    doc["timestamp"] = _lastSampleTime;

    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
}

void PowerWebServer::handleGetCalibration(AsyncWebServerRequest* request) {
    JsonDocument doc;
    doc["adc0"] = _calibration->getAdc0();
    doc["adc2"] = _calibration->getAdc2();
    doc["adc4"] = _calibration->getAdc4();
    doc["adc6"] = _calibration->getAdc6();
    doc["cycleConstant"] = _settings->loadCycleConstant(1.05f);

    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
}

void PowerWebServer::handleSetCalibration(AsyncWebServerRequest* request, uint8_t* data, size_t len) {
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, data, len);

    if (error) {
        request->send(400, "application/json", "{\"success\":false,\"error\":\"Invalid JSON\"}");
        return;
    }

    int adc0 = doc["adc0"] | -1;
    int adc2 = doc["adc2"] | -1;
    int adc4 = doc["adc4"] | -1;
    int adc6 = doc["adc6"] | -1;
    float cycleConstant = doc["cycleConstant"] | -1.0f;

    if (adc0 < 0 || adc2 < 0 || adc4 < 0 || adc6 < 0) {
        request->send(400, "application/json", "{\"success\":false,\"error\":\"Missing calibration values\"}");
        return;
    }

    if (cycleConstant < 0.5f || cycleConstant > 2.0f) {
        request->send(400, "application/json", "{\"success\":false,\"error\":\"Cycle constant must be 0.5-2.0\"}");
        return;
    }

    // Save to NVS
    _settings->saveCalibration(adc0, adc2, adc4, adc6);
    _settings->saveCycleConstant(cycleConstant);

    // Update live calibration
    _calibration->updateValues(adc0, adc2, adc4, adc6);

    Serial.printf("Calibration saved via web: %d %d %d %d, cycle=%.2f\n", adc0, adc2, adc4, adc6, cycleConstant);

    request->send(200, "application/json", "{\"success\":true,\"message\":\"Restart for cycle constant to take effect\"}");
}

void PowerWebServer::handleGetDeviceName(AsyncWebServerRequest* request) {
    JsonDocument doc;
    doc["name"] = _deviceName;

    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
}

void PowerWebServer::handleSetDeviceName(AsyncWebServerRequest* request, uint8_t* data, size_t len) {
    Serial.printf("handleSetDeviceName: received %d bytes\n", len);

    // Debug: print raw data
    String rawBody;
    for (size_t i = 0; i < len && i < 100; i++) {
        rawBody += (char)data[i];
    }
    Serial.printf("Raw body: %s\n", rawBody.c_str());

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, data, len);

    if (error) {
        Serial.printf("JSON parse error: %s\n", error.c_str());
        request->send(400, "application/json", "{\"success\":false,\"error\":\"Invalid JSON\"}");
        return;
    }

    // Debug: check what keys exist
    Serial.print("JSON keys: ");
    for (JsonPair kv : doc.as<JsonObject>()) {
        Serial.printf("%s ", kv.key().c_str());
    }
    Serial.println();

    if (!doc.containsKey("name")) {
        Serial.println("Name field missing from JSON");
        request->send(400, "application/json", "{\"success\":false,\"error\":\"Name field required\"}");
        return;
    }

    String name = doc["name"].as<String>();
    Serial.printf("Parsed name: '%s'\n", name.c_str());

    if (name.length() == 0 || name.length() > 20) {
        Serial.printf("Name length invalid: %d\n", name.length());
        request->send(400, "application/json", "{\"success\":false,\"error\":\"Name must be 1-20 characters\"}");
        return;
    }

    // Save to NVS
    _settings->saveDeviceName(name.c_str());
    _deviceName = name;

    Serial.printf("Device name saved via web: %s\n", name.c_str());

    request->send(200, "application/json", "{\"success\":true,\"message\":\"Restart required for WiFi/BLE name change\"}");
}

void PowerWebServer::handleGetWiFi(AsyncWebServerRequest* request) {
    JsonDocument doc;
    String ssid, password;
    bool hasWiFi = _settings->loadWiFi(ssid, password);

    doc["configured"] = hasWiFi;
    doc["ssid"] = hasWiFi ? ssid : "";
    doc["isAPMode"] = _isAPMode;
    doc["connected"] = !_isAPMode && WiFi.status() == WL_CONNECTED;
    doc["ip"] = getIPAddress();

    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
}

void PowerWebServer::handleSetWiFi(AsyncWebServerRequest* request, uint8_t* data, size_t len) {
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, data, len);

    if (error) {
        request->send(400, "application/json", "{\"success\":false,\"error\":\"Invalid JSON\"}");
        return;
    }

    if (!doc.containsKey("ssid")) {
        request->send(400, "application/json", "{\"success\":false,\"error\":\"SSID field required\"}");
        return;
    }

    String ssid = doc["ssid"].as<String>();
    String password = doc.containsKey("password") ? doc["password"].as<String>() : "";

    if (ssid.length() == 0 || ssid.length() > 32) {
        request->send(400, "application/json", "{\"success\":false,\"error\":\"SSID must be 1-32 characters\"}");
        return;
    }

    if (password.length() > 63) {
        request->send(400, "application/json", "{\"success\":false,\"error\":\"Password too long\"}");
        return;
    }

    _settings->saveWiFi(ssid.c_str(), password.c_str());
    Serial.printf("WiFi credentials saved: %s\n", ssid.c_str());

    request->send(200, "application/json", "{\"success\":true,\"message\":\"Restart to connect to WiFi\"}");
}

void PowerWebServer::handleClearWiFi(AsyncWebServerRequest* request) {
    _settings->clearWiFi();
    Serial.println("WiFi credentials cleared");
    request->send(200, "application/json", "{\"success\":true,\"message\":\"WiFi cleared. Restart to use AP mode\"}");
}

void PowerWebServer::handleCalibrationStatus(AsyncWebServerRequest* request) {
    JsonDocument doc;

    const char* stateNames[] = {"idle", "0kp", "6kp", "4kp", "2kp", "done"};
    doc["state"] = stateNames[_calState];
    doc["step"] = (int)_calState;
    doc["adc"] = readAdcSmoothed();
    doc["values"]["adc0"] = _calValues[0];
    doc["values"]["adc2"] = _calValues[1];
    doc["values"]["adc4"] = _calValues[2];
    doc["values"]["adc6"] = _calValues[3];

    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
}

void PowerWebServer::handleCalibrationStart(AsyncWebServerRequest* request) {
    _calState = CAL_0KP;
    _calValues[0] = _calValues[1] = _calValues[2] = _calValues[3] = 0;
    Serial.println("Web calibration started");
    request->send(200, "application/json", "{\"success\":true}");
}

void PowerWebServer::handleCalibrationNext(AsyncWebServerRequest* request) {
    if (_calState == CAL_IDLE) {
        request->send(400, "application/json", "{\"success\":false,\"error\":\"Calibration not started\"}");
        return;
    }

    // Read current smoothed ADC value
    float adcFloat = readAdcAvg();
    int adcValue = (int)roundf(adcFloat);
    Serial.printf("Captured ADC: %.2f (rounded: %d) for state %d\n", adcFloat, adcValue, (int)_calState);

    // Order: 0kp -> 6kp -> 4kp -> 2kp (easier to remove weights)
    switch (_calState) {
        case CAL_0KP:
            _calValues[0] = adcValue;
            _calState = CAL_6KP;
            Serial.printf("0kp ADC: %d, moving to state %d (CAL_6KP)\n", adcValue, (int)_calState);
            request->send(200, "application/json", "{\"success\":true,\"message\":\"Set pendulum to 6 kp and click Next\"}");
            break;
        case CAL_6KP:
            _calValues[3] = adcValue;
            _calState = CAL_4KP;
            Serial.printf("6kp ADC: %d, moving to state %d (CAL_4KP)\n", adcValue, (int)_calState);
            request->send(200, "application/json", "{\"success\":true,\"message\":\"Set pendulum to 4 kp and click Next\"}");
            break;
        case CAL_4KP:
            _calValues[2] = adcValue;
            _calState = CAL_2KP;
            Serial.printf("4kp ADC: %d, moving to state %d (CAL_2KP)\n", adcValue, (int)_calState);
            request->send(200, "application/json", "{\"success\":true,\"message\":\"Set pendulum to 2 kp and click Next\"}");
            break;
        case CAL_2KP:
            _calValues[1] = adcValue;
            Serial.printf("2kp ADC: %d\n", adcValue);

            // Save calibration
            _settings->saveCalibration(_calValues[0], _calValues[1], _calValues[2], _calValues[3]);
            _calibration->updateValues(_calValues[0], _calValues[1], _calValues[2], _calValues[3]);

            Serial.printf("Calibration saved: 0kp=%d 2kp=%d 4kp=%d 6kp=%d\n", _calValues[0], _calValues[1], _calValues[2], _calValues[3]);
            _calState = CAL_DONE;
            Serial.printf("Moving to state %d (CAL_DONE)\n", (int)_calState);
            request->send(200, "application/json", "{\"success\":true,\"message\":\"Calibration complete and saved!\"}");
            break;
        case CAL_DONE:
            _calState = CAL_IDLE;
            Serial.printf("Moving to state %d (CAL_IDLE)\n", (int)_calState);
            request->send(200, "application/json", "{\"success\":true,\"message\":\"Calibration finished\"}");
            break;
        default:
            Serial.printf("ERROR: Unknown state %d in switch!\n", (int)_calState);
            request->send(400, "application/json", "{\"success\":false,\"error\":\"Invalid state\"}");
            break;
    }
}

void PowerWebServer::handleCalibrationCancel(AsyncWebServerRequest* request) {
    _calState = CAL_IDLE;
    Serial.println("Web calibration cancelled");
    request->send(200, "application/json", "{\"success\":true,\"message\":\"Calibration cancelled\"}");
}
