#include "PowerWebServer.h"
#include <Update.h>
#include <esp_wifi.h>
#include <ESPmDNS.h>

PowerWebServer::PowerWebServer(SettingsManager* settings, MonarkCalibration* calibration, uint8_t adcPin)
    : _server(80), _settings(settings), _calibration(calibration), _adcPin(adcPin) {
    memset(&_lastSample, 0, sizeof(_lastSample));
}

void PowerWebServer::begin(const char* apPassword) {
    _deviceName = _settings->loadDeviceName("MonarkPower");
    _apPassword = apPassword;

    // Always come up in AP mode FIRST so the web UI is reachable within a
    // second of boot — STA association is finicky on the C6 (often takes
    // 30-60 s of repeated auth handshakes, sometimes never succeeds on the
    // first try). The poll() loop will attempt STA in the background a few
    // seconds later, once the radio + BLE stack have settled. On success
    // poll() promotes us to AP+STA so existing AP clients aren't dropped.
    startAPMode();
    setupRoutes();
    _server.begin();
    Serial.println("Web server started on port 80");

    // Schedule the first STA retry ~5 s out — gives the radio time to settle
    // after AP-up + BLE-up, when the C6 has been observed to associate more
    // reliably than during a busy boot.
    _lastStaRetryMs = millis() - STA_RETRY_INTERVAL_MS + 5000;
}

bool PowerWebServer::tryConnectWiFi() {
    String ssid, password;
    if (!_settings->loadWiFi(ssid, password)) {
        Serial.println("No WiFi credentials saved");
        return false;
    }
    // Defensive trim — the saved value might have been written before we
    // started trimming on save. Stray whitespace causes silent reason=2.
    ssid.trim();
    password.trim();
    if (ssid.length() == 0) {
        Serial.println("WiFi credentials empty after trim");
        return false;
    }

    // Print first 3 + last 3 chars of password so the user can verify the saved
    // value matches what they typed/pasted, without leaking the full secret.
    String passPreview;
    if (password.length() <= 6) {
        passPreview = String("***");
    } else {
        passPreview = password.substring(0, 3) + String("...") + password.substring(password.length() - 3);
    }
    Serial.printf("Connecting to WiFi: %s (pass %d chars: %s)\n",
                  ssid.c_str(), password.length(), passPreview.c_str());
    WiFi.persistent(false);
    // Don't let the WiFi stack auto-retry on AUTH_FAIL. The C6 retries every
    // ~500ms with the same wrong (or temporarily-rejected) creds, hammering
    // the AP and triggering its rate-limit / blacklist for our MAC. We do
    // exactly one deliberate attempt, wait, give up if it doesn't take.
    WiFi.setAutoReconnect(false);
    WiFi.mode(WIFI_STA);
    delay(100);
    // Disconnect AFTER mode is set so it's not a "STA not started" error at fresh boot.
    WiFi.disconnect(false, true);
    delay(100);

    // Log the AP's exact disassociation reason. Common codes:
    //   2  = AUTH_EXPIRE    | 6  = NOT_AUTHED
    //   8  = ASSOC_LEAVE    | 15 = 4WAY_HANDSHAKE_TIMEOUT (typical wrong-password)
    //   200= BEACON_TIMEOUT | 201= NO_AP_FOUND | 202= AUTH_FAIL
    //   203= ASSOC_FAIL     | 204= HANDSHAKE_TIMEOUT       (also wrong-password)
    WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
        Serial.printf("[wifi-event] STA_DISCONNECTED reason=%d\n",
                      info.wifi_sta_disconnected.reason);
    }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

    // Pre-scan so we have concrete diagnostics (RSSI / channel / auth) regardless of
    // what happens next. Cheap (~3s) and the result usually tells us why a connect
    // fails more clearly than the WL_* status code does.
    Serial.println("Scanning for target SSID...");
    int n = WiFi.scanNetworks();
    int targetIdx = -1;
    for (int i = 0; i < n; i++) {
        if (WiFi.SSID(i) == ssid) { targetIdx = i; break; }
    }
    if (targetIdx >= 0) {
        const char* authNames[] = {"OPEN","WEP","WPA-PSK","WPA2-PSK","WPA/WPA2","WPA2-ENT","WPA3-PSK","WPA2/WPA3","WAPI","OWE","WPA3-ENT-192"};
        int auth = (int)WiFi.encryptionType(targetIdx);
        const char* authName = (auth >= 0 && auth < (int)(sizeof(authNames)/sizeof(authNames[0]))) ? authNames[auth] : "?";
        Serial.printf("  visible: RSSI=%ddBm channel=%d auth=%s (raw=%d)\n",
                      WiFi.RSSI(targetIdx), WiFi.channel(targetIdx), authName, auth);
    } else {
        Serial.printf("  '%s' NOT visible in scan (%d networks seen). Will still try begin().\n",
                      ssid.c_str(), n);
    }
    WiFi.scanDelete();

    // Power-save can cause silent disconnects on weak/marginal links — turn it off.
    WiFi.setSleep(false);
    // Allow WPA-PSK so APs running mixed-mode security still work.
    WiFi.setMinSecurity(WIFI_AUTH_WPA_PSK);

    // CRITICAL for ESP32-C6 + WPA2/WPA3 mixed-mode routers (and some plain WPA2
    // APs with PMF Required): disable 802.11ax (WiFi 6 / HE) on the STA radio.
    // The C6 advertises HE capabilities by default and some APs reject the
    // association silently, producing repeated reason=2 (AUTH_EXPIRE) loops.
    // Restricting to b/g/n forces a legacy capability set everyone speaks.
    esp_wifi_set_protocol(WIFI_IF_STA,
        WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);

    // Build the STA config manually so we can force PMF off — ESP32-C6 advertises
    // PMF-capable by default and some WPA2-only routers drop those association
    // requests.
    wifi_config_t wcfg = {};
    strncpy((char*)wcfg.sta.ssid, ssid.c_str(), sizeof(wcfg.sta.ssid) - 1);
    strncpy((char*)wcfg.sta.password, password.c_str(), sizeof(wcfg.sta.password) - 1);
    wcfg.sta.pmf_cfg.capable = false;
    wcfg.sta.pmf_cfg.required = false;
    wcfg.sta.threshold.authmode = WIFI_AUTH_WPA_PSK;
    esp_wifi_set_config(WIFI_IF_STA, &wcfg);
    esp_wifi_connect();

    // Wait up to 60 seconds for connection — the C6 + WPA3-mixed combo can
    // take 30-45 s on a "cold" first try while the AP and the chip negotiate
    // capabilities. 60 s gives margin without making boot feel hung.
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 120) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        _isAPMode = false;
        Serial.print("Connected to WiFi. IP: ");
        Serial.println(WiFi.localIP());
        // Advertise via mDNS so the user can reach the device by name from any
        // host on the same LAN: http://MonarkPower.local/  (lowercase, no underscores).
        if (MDNS.begin(_deviceName.c_str())) {
            MDNS.addService("http", "tcp", 80);
            Serial.printf("mDNS: http://%s.local/\n", _deviceName.c_str());
        } else {
            Serial.println("mDNS start failed");
        }
        return true;
    }

    // Print the specific status so we can tell the failure mode at a glance:
    //   1 = WL_NO_SSID_AVAIL (SSID not found / out of range)
    //   4 = WL_CONNECT_FAILED (auth rejected — wrong password)
    //   6 = WL_DISCONNECTED   (no AP / DHCP timeout)
    Serial.printf("WiFi connection failed (status=%d)\n", (int)WiFi.status());
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
        // mDNS works in soft-AP mode too, so a client joined to MonarkPower can
        // reach the device by name instead of memorising 192.168.4.1.
        if (MDNS.begin(_deviceName.c_str())) {
            MDNS.addService("http", "tcp", 80);
            Serial.printf("mDNS: http://%s.local/\n", _deviceName.c_str());
        }
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

void PowerWebServer::poll() {
    // Background STA retry while stuck in AP. Cheap — bails fast unless the
    // timer has elapsed or the user manually requested a retry.
    if (!_isAPMode) return;
    uint32_t now = millis();
    bool due = _forceStaRetry || (_lastStaRetryMs != 0 && (now - _lastStaRetryMs >= STA_RETRY_INTERVAL_MS));
    if (_lastStaRetryMs == 0) _lastStaRetryMs = now; // first call after boot baselines the timer
    if (!due) return;
    _forceStaRetry = false;
    _lastStaRetryMs = now;
    retryStaIfNeeded();
}

void PowerWebServer::retryStaIfNeeded() {
    String ssid, password;
    if (!_settings->loadWiFi(ssid, password)) return;
    if (ssid.length() == 0) return;

    Serial.println("[wifi-retry] attempting STA reconnect from AP mode");
    // Stay in AP+STA mode so AP clients (phone on MonarkPower) don't get
    // dropped while STA negotiates. tryConnectWiFi() will set WIFI_STA mode
    // internally — we override after it returns.
    WiFi.mode(WIFI_AP_STA);
    bool ok = tryConnectWiFi();
    if (ok) {
        // Keep both AP+STA up so existing AP clients survive.
        WiFi.mode(WIFI_AP_STA);
        _isAPMode = false; // STA is our preferred address now
        Serial.println("[wifi-retry] STA up — running AP+STA");
    } else {
        // tryConnectWiFi switched to WIFI_STA on entry; restore AP-only since
        // STA couldn't associate. Without this, AP would stay down.
        Serial.println("[wifi-retry] STA still failing — restoring AP");
        WiFi.disconnect(true /*wifioff*/, false);
        delay(50);
        startAPMode();
    }
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

    // GET /api/wifi/scan - poll scan state. Auto-kicks an async scan if none
    // is in flight; returns "started" / "running" / "done" with results.
    // Works from both STA and AP modes (we briefly enable STA scan capability
    // without dropping the AP).
    _server.on("/api/wifi/scan", HTTP_GET, [this](AsyncWebServerRequest* request) {
        int n = WiFi.scanComplete();
        JsonDocument doc;
        if (n == WIFI_SCAN_FAILED || n == -2) {
            // Make sure STA mode is at least available alongside the AP.
            if (_isAPMode) WiFi.mode(WIFI_AP_STA);
            WiFi.scanNetworks(true /*async*/);
            doc["status"] = "started";
        } else if (n == WIFI_SCAN_RUNNING || n == -1) {
            doc["status"] = "running";
        } else {
            doc["status"] = "done";
            doc["count"] = n;
            JsonArray arr = doc["results"].to<JsonArray>();
            for (int i = 0; i < n && i < 24; i++) {
                JsonObject o = arr.add<JsonObject>();
                o["ssid"] = WiFi.SSID(i);
                o["rssi"] = WiFi.RSSI(i);
                o["channel"] = WiFi.channel(i);
                o["secure"] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
            }
        }
        String json;
        serializeJson(doc, json);
        request->send(200, "application/json", json);
    });

    // POST /api/wifi/reconnect — manual STA retry without rebooting. Returns
    // immediately (the actual attempt blocks for up to 60s — fire and forget).
    _server.on("/api/wifi/reconnect", HTTP_POST, [this](AsyncWebServerRequest* request) {
        request->send(200, "application/json", "{\"ok\":true,\"note\":\"STA retry kicked off — check /api/wifi in a minute\"}");
        _forceStaRetry = true;
    });

    // POST /api/wifi/scan - force a fresh scan (deletes previous results).
    _server.on("/api/wifi/scan", HTTP_POST, [this](AsyncWebServerRequest* request) {
        WiFi.scanDelete();
        if (_isAPMode) WiFi.mode(WIFI_AP_STA);
        WiFi.scanNetworks(true);
        request->send(200, "application/json", "{\"status\":\"started\"}");
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
        const char* calStates[] = {"idle", "0kp", "2kp", "4kp", "6kp", "done"};
        doc["cal"]["state"] = calStates[_calState];
        doc["cal"]["step"] = (int)_calState;
        doc["cal"]["adc"] = _lastSample.adc_raw;
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

    // OTA Update
    _server.on("/update", HTTP_POST, [&](AsyncWebServerRequest *request){
        bool shouldReboot = !Update.hasError();
        AsyncWebServerResponse *response = request->beginResponse(200, "text/plain", shouldReboot ? "OK" : "FAIL");
        response->addHeader("Connection", "close");
        request->send(response);
        if(shouldReboot){
            delay(100);
            ESP.restart();
        }
    }, [&](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final){
        handleUpdate(request, filename, index, data, len, final);
    });

    // Cinder mobile SPA. The HTML/CSS/JS lives in flash (PROGMEM) — we used to
    // build a 28 KB String per request which competed for fragmented heap and
    // could fail under load on the C6. The PROGMEM literal + `send_P` zero-
    // copies straight from flash so the AP-mode UI is reliable even after
    // long uptimes.
    static const char INDEX_HTML[] PROGMEM = R"rawhtml(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<title>monark · cycle computer</title>
<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">
<style>
:root {
  --bg:#0e0c0a; --surface:#171411; --surface2:#1f1b16;
  --ink:#f3ece0; --dim:#8a7f6e; --rule:#2a251e;
  --accent:#ff8a14; --accent2:#ffb84d; --ok:#a3c46a; --warn:#e85a3c;
  --fd:-apple-system,system-ui,"Segoe UI",Roboto,sans-serif;
  --fm:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;
}
*{box-sizing:border-box}
html,body{margin:0;padding:0;background:var(--bg);color:var(--ink);font-family:var(--fd);-webkit-font-smoothing:antialiased}
body{min-height:100vh;padding-bottom:96px;padding-top:env(safe-area-inset-top)}
@media (min-width:600px){body{max-width:480px;margin:0 auto;border-left:1px solid var(--rule);border-right:1px solid var(--rule)}}
.toptitle{padding:14px 18px 0;display:flex;justify-content:space-between;align-items:flex-start}
.toptitle h1{font-size:22px;font-weight:700;margin:0;letter-spacing:-.02em;text-transform:lowercase}
.toptitle .sub{font-family:var(--fm);font-size:9px;letter-spacing:.22em;color:var(--dim);text-transform:uppercase}
.pill{display:inline-flex;gap:6px;align-items:center}
.pill .dot{width:7px;height:7px;border-radius:50%;background:var(--accent);box-shadow:0 0 6px var(--accent)}
.pill .text{font-family:var(--fm);font-size:10px;letter-spacing:.18em;color:var(--accent);text-transform:uppercase}
.card{margin:14px;padding:14px 16px 12px;background:var(--surface);border:1px solid var(--rule);position:relative}
.card .head{display:flex;justify-content:space-between;font-family:var(--fm);font-size:9px;letter-spacing:.2em;color:var(--dim);text-transform:uppercase}
.card .head .accent{color:var(--accent)}
.big-num{font-size:72px;font-weight:700;letter-spacing:-.045em;line-height:.9;font-variant-numeric:tabular-nums;margin-top:4px;display:flex;align-items:baseline;gap:6px}
.big-num small{font-family:var(--fm);font-size:14px;color:var(--dim);font-weight:400}
.spark{margin:14px -16px 4px;display:block}
.foot-line{display:flex;justify-content:space-between;font-family:var(--fm);font-size:9px;color:var(--dim);margin-top:4px}
.foot-line span b{color:var(--ink);font-weight:500}
.grid-3{margin:0 14px;display:grid;grid-template-columns:1fr 1fr 1fr;gap:8px}
.tile{padding:10px;background:var(--surface);border:1px solid var(--rule)}
.tile .label{font-family:var(--fm);font-size:8px;letter-spacing:.2em;color:var(--dim);text-transform:uppercase}
.tile .val{font-size:24px;font-weight:600;letter-spacing:-.03em;margin-top:4px;font-variant-numeric:tabular-nums}
.tile .val small{font-family:var(--fm);font-size:9px;color:var(--dim);margin-left:3px;font-weight:400}
.zones{margin:14px}
.zones .head{display:flex;justify-content:space-between;font-family:var(--fm);font-size:9px;letter-spacing:.2em;color:var(--dim);text-transform:uppercase;margin-bottom:6px}
.zones .bars{display:grid;grid-template-columns:repeat(7,1fr);gap:3px;align-items:end;height:50px}
.zones .col{display:flex;flex-direction:column;align-items:center;gap:3px;height:100%}
.zones .b{width:100%;min-height:2px}
.zones span{font-family:var(--fm);font-size:8px;color:var(--dim)}
.ip-strip{position:fixed;bottom:60px;left:0;right:0;padding:8px 14px;display:flex;justify-content:space-between;font-family:var(--fm);font-size:10px;color:var(--dim);background:var(--bg);border-top:1px solid var(--rule)}
.ip-strip .ip{color:var(--accent)}
@media (min-width:600px){.ip-strip{max-width:480px;left:50%;transform:translateX(-50%)}}
nav.tabs{position:fixed;bottom:0;left:0;right:0;height:60px;background:var(--surface);border-top:1px solid var(--rule);display:grid;grid-template-columns:repeat(4,1fr);padding-bottom:env(safe-area-inset-bottom)}
@media (min-width:600px){nav.tabs{max-width:480px;left:50%;transform:translateX(-50%)}}
nav.tabs button{background:none;border:0;color:var(--dim);font-family:var(--fm);font-size:10px;letter-spacing:.18em;text-transform:uppercase;cursor:pointer;padding:0}
nav.tabs button.active{color:var(--accent)}
input[type=text],input[type=password],input[type=number]{width:calc(100% - 28px);box-sizing:border-box;padding:12px 14px;background:var(--surface);border:1px solid var(--rule);color:var(--ink);font-family:var(--fd);font-size:15px;outline:none;margin:0 14px 10px}
input:focus{border-color:var(--accent)}
label{display:block;font-family:var(--fm);font-size:9px;letter-spacing:.2em;color:var(--dim);margin:14px 14px 6px;text-transform:uppercase}
button.btn{padding:13px;background:transparent;border:1px solid var(--rule);color:var(--ink);font-family:var(--fm);font-size:11px;letter-spacing:.18em;text-transform:uppercase;cursor:pointer;flex:1}
button.btn.primary{background:var(--accent);border-color:var(--accent);color:var(--bg);font-weight:600}
button.btn.danger{color:var(--warn);border-color:var(--warn)}
button.btn:disabled{opacity:.5;cursor:wait}
.btn-row{margin:14px;display:flex;gap:8px}
.wifi-list{margin:0 14px;background:var(--surface);border:1px solid var(--rule)}
.wifi-row{padding:12px 14px;border-bottom:1px solid var(--rule);display:flex;justify-content:space-between;align-items:center;cursor:pointer}
.wifi-row:last-child{border-bottom:0}
.wifi-row.active{background:rgba(255,138,20,.08)}
.wifi-row .ssid{font-size:14px;font-weight:500}
.wifi-row .meta{font-family:var(--fm);font-size:9px;color:var(--dim);margin-top:2px;letter-spacing:.1em;text-transform:uppercase}
.wifi-row .right{display:flex;gap:8px;align-items:center}
.bars{display:flex;gap:1px;align-items:end}
.bars i{width:2px;background:var(--rule)}
.bars i.on{background:var(--ink)}
.bars i:nth-child(1){height:3px}.bars i:nth-child(2){height:6px}.bars i:nth-child(3){height:9px}.bars i:nth-child(4){height:12px}
.badge{font-family:var(--fm);font-size:8px;letter-spacing:.18em;color:var(--accent);text-transform:uppercase}
.steps{display:flex;gap:6px;margin:0 14px 14px}
.steps .seg{flex:1;height:3px;background:var(--rule)}
.steps .seg.on{background:var(--accent)}
.cal-text{margin:0 18px 14px;font-size:16px;font-weight:500;line-height:1.3;color:var(--ink)}
.cal-text small{display:block;font-family:var(--fm);font-size:11px;color:var(--dim);margin-top:8px;line-height:1.5;letter-spacing:.06em}
.row{padding:14px;margin:0 14px;border-bottom:1px solid var(--rule);display:flex;justify-content:space-between;align-items:center}
.row .lbl{font-family:var(--fm);font-size:9px;letter-spacing:.2em;color:var(--dim);text-transform:uppercase}
.row .val{font-size:16px;font-weight:500;margin-top:2px}
.row .arr{color:var(--dim);font-family:var(--fm)}
.toggle{width:36px;height:20px;position:relative;cursor:pointer;display:inline-block}
.toggle .knob{position:absolute;top:2px;bottom:2px;width:14px;transition:left 100ms ease}
.toggle.on{background:var(--accent)} .toggle.off{background:var(--surface2)}
.toggle.on .knob{left:calc(100% - 16px);background:var(--bg)}
.toggle.off .knob{left:2px;background:var(--dim)}
.hidden{display:none !important}
.toast{position:fixed;bottom:124px;left:14px;right:14px;padding:10px 14px;background:var(--surface);border:1px solid var(--accent);font-family:var(--fm);font-size:11px;letter-spacing:.08em;color:var(--accent);z-index:100;text-transform:uppercase}
.toast.error{border-color:var(--warn);color:var(--warn)}
@media (min-width:600px){.toast{max-width:452px;left:50%;transform:translateX(-50%)}}
.fwfoot{margin:14px;padding:10px 14px;background:var(--surface);border:1px solid var(--rule);font-family:var(--fm);font-size:10px;color:var(--dim);letter-spacing:.1em;text-align:center;text-transform:uppercase}
.adc-bar{height:4px;background:var(--rule);margin-top:8px;position:relative}
.adc-bar .fill{position:absolute;left:0;top:0;bottom:0;background:var(--accent);transition:width 200ms ease}
.kp-presets{display:flex;gap:6px;margin-top:14px}
.kp-presets div{flex:1;padding:8px 0;text-align:center;font-family:var(--fm);font-size:11px;letter-spacing:.1em;color:var(--ink);border:1px solid var(--rule);cursor:pointer;text-transform:uppercase}
.kp-presets div.on{background:var(--accent);border-color:var(--accent);color:var(--bg);font-weight:600}
.scan-status{font-family:var(--fm);font-size:10px;color:var(--dim);text-transform:uppercase;letter-spacing:.18em;padding:0 18px 8px}
</style>
</head>
<body>

<!-- ─── DASHBOARD ─── -->
<section id="screen-dash">
  <div class="toptitle">
    <div>
      <div class="sub">live · <span id="dashSession">--:--</span></div>
      <h1 id="dashName">monark</h1>
    </div>
    <div class="pill"><span class="dot"></span><span class="text">live</span></div>
  </div>
  <div class="card">
    <div class="head"><span>Power · W</span><span class="accent" id="powerSub">—</span></div>
    <div class="big-num"><span id="powerBig">—</span><small>watts</small></div>
    <svg class="spark" id="powerSpark" viewBox="0 0 328 64" preserveAspectRatio="none" width="100%" height="64"></svg>
    <div class="foot-line"><span>avg <b id="powerAvg">—</b></span><span>max <b id="powerMax">—</b></span></div>
  </div>
  <div class="grid-3">
    <div class="tile"><div class="label">Cadence</div><div class="val"><span id="rpmTile">—</span><small>rpm</small></div></div>
    <div class="tile"><div class="label">Kilopond</div><div class="val"><span id="kpTile">—</span><small>kp</small></div></div>
    <div class="tile"><div class="label">ADC</div><div class="val"><span id="adcTile">—</span></div></div>
  </div>
  <div class="zones">
    <div class="head"><span>Zones · current</span><span id="zoneLabel">—</span></div>
    <div class="bars" id="zoneBars"></div>
  </div>
</section>

<!-- ─── WIFI ─── -->
<section id="screen-wifi" class="hidden">
  <div class="toptitle"><div><div class="sub">network</div><h1>wifi</h1></div></div>
  <div class="card" id="wifiCurrent">
    <div class="head"><span id="wifiCurrentLabel">disconnected</span><span class="accent" id="wifiCurrentIp"></span></div>
    <div style="font-size:18px;font-weight:600;margin-top:4px" id="wifiCurrentSsid">—</div>
    <div class="foot-line"><span>mode <b id="wifiMode">ap</b></span><span id="wifiAux"></span></div>
  </div>
  <div class="scan-status" id="scanStatus">tap scan to look for networks</div>
  <div class="wifi-list" id="wifiList"></div>
  <div class="btn-row">
    <button class="btn" onclick="startScan()" id="scanBtn">Scan</button>
    <button class="btn" onclick="reconnectSta()" id="reconnectBtn">Reconnect STA</button>
    <button class="btn primary" onclick="showWifiManual()">+ Add</button>
  </div>
  <div id="wifiManual" class="hidden">
    <label>SSID</label>
    <input type="text" id="wifiSsid" maxlength="32">
    <label>Password (blank = keep saved)</label>
    <input type="password" id="wifiPass" maxlength="63">
    <div class="btn-row">
      <button class="btn" onclick="hideWifiManual()">Cancel</button>
      <button class="btn primary" onclick="saveWifi()">Save · Reboot</button>
    </div>
    <div class="btn-row"><button class="btn danger" onclick="clearWifi()">Forget current</button></div>
  </div>
</section>

<!-- ─── CALIBRATION ─── -->
<section id="screen-cal" class="hidden">
  <div class="toptitle"><div><div class="sub">step · <span id="calStepNum">0/4</span></div><h1>calibrate</h1></div></div>
  <div class="steps" id="calSteps">
    <div class="seg"></div><div class="seg"></div><div class="seg"></div><div class="seg"></div>
  </div>
  <div class="cal-text" id="calMsg">
    Click <b>Start</b> to begin calibration. You'll set the pendulum to 0 → 2 → 4 → 6 kp in order.
    <small>The capture reads the live smoothed ADC. Hold the pendulum still; the chrome below shows current load.</small>
  </div>
  <div class="card">
    <div class="head"><span>Live load</span><span class="accent" id="calAdcRaw">—</span></div>
    <div class="big-num"><span id="calAdcLive">—</span><small>adc</small></div>
    <div class="adc-bar"><div class="fill" id="calAdcFill" style="width:0%"></div></div>
    <div class="foot-line">
      <span>0kp <b id="cal0">—</b></span>
      <span>2kp <b id="cal2">—</b></span>
      <span>4kp <b id="cal4">—</b></span>
      <span>6kp <b id="cal6">—</b></span>
    </div>
  </div>
  <div class="btn-row" id="calBtns">
    <button class="btn" id="calCancelBtn" onclick="cancelCal()" disabled>Cancel</button>
    <button class="btn primary" id="calNextBtn" onclick="nextCal()">Start</button>
  </div>
  <div class="cal-text" style="margin-top:0">
    <small>Tip: the pendulum rest position drifts. Re-cal if zero load reads more than ~10 ADC counts above the saved 0kp.</small>
  </div>
</section>

<!-- ─── SETTINGS ─── -->
<section id="screen-settings" class="hidden">
  <div class="toptitle"><div><div class="sub">configuration</div><h1>settings</h1></div></div>
  <label>Device name (BLE / WiFi AP)</label>
  <input type="text" id="setDevName" maxlength="32">
  <div class="btn-row" style="margin-top:0"><button class="btn primary" onclick="saveDevName()">Save name</button></div>

  <div class="row">
    <div><div class="lbl">Cycle constant</div><div class="val" id="setCycCurrent">1.05</div></div>
    <span class="arr">stored in nvs</span>
  </div>
  <div class="row">
    <div><div class="lbl">Power simulator</div><div class="val" id="setSimVal">off</div></div>
    <div class="toggle off" id="setSimToggle" onclick="toggleSim()"><div class="knob"></div></div>
  </div>
  <div class="row">
    <div><div class="lbl">Cadence target</div><div class="val">80–100 rpm</div></div>
    <span class="arr">device chrome</span>
  </div>

  <div class="btn-row">
    <button class="btn" onclick="rebootDevice()">Reboot</button>
    <button class="btn danger" onclick="clearWifi()">Forget WiFi</button>
  </div>

  <label>OTA firmware update</label>
  <input type="file" id="otaFile" accept=".bin" style="margin:0 14px 10px;color:var(--dim);font-family:var(--fm);font-size:11px">
  <div class="btn-row" style="margin-top:0"><button class="btn primary" onclick="otaUpload()" id="otaBtn">Upload &amp; flash</button></div>

  <div class="fwfoot" id="fwFoot">monark · esp32-c6 · fw build</div>
</section>

<div id="ipStrip" class="ip-strip">
  <span id="dotName">● <span id="devShort">monark</span></span>
  <span class="ip" id="ipAddr">—</span>
</div>
<nav class="tabs">
  <button data-tab="dash" class="active" onclick="setTab('dash')">Dash</button>
  <button data-tab="wifi" onclick="setTab('wifi')">WiFi</button>
  <button data-tab="cal" onclick="setTab('cal')">Calib</button>
  <button data-tab="settings" onclick="setTab('settings')">Set</button>
</nav>
<div id="toast" class="toast hidden"></div>

<script>
// ───── Tab management ─────
function setTab(name){
  ['dash','wifi','cal','settings'].forEach(n=>{
    document.getElementById('screen-'+n).classList.toggle('hidden', n!==name);
  });
  document.querySelectorAll('nav.tabs button').forEach(b=>{
    b.classList.toggle('active', b.dataset.tab===name);
  });
  if (name==='wifi') refreshWifi();
  if (name==='cal')  refreshCal();
  if (name==='settings') refreshSettings();
}

function toast(msg, isError){
  const el = document.getElementById('toast');
  el.textContent = msg; el.classList.toggle('error', !!isError); el.classList.remove('hidden');
  clearTimeout(window._t);
  window._t = setTimeout(()=>el.classList.add('hidden'), 3500);
}

// ───── Dash polling + sparkline ─────
const POW_HIST_N = 80;
const powHist = [];
let sessionStart = Date.now();
let powerMax = 0, powerSum = 0, powerN = 0;

function fmtClock(ms){ const s=Math.floor(ms/1000); const m=Math.floor(s/60); const h=Math.floor(m/60); return (h>0?String(h)+':':'')+String(m%60).padStart(2,'0')+':'+String(s%60).padStart(2,'0'); }
function fmtMaybe(v, dp){ if(v==null) return '—'; return Number(v).toFixed(dp||0); }

function drawSpark(){
  const svg = document.getElementById('powerSpark');
  const W=328, H=64;
  if (powHist.length < 2){ svg.innerHTML=''; return; }
  const max = Math.max(...powHist, 1);
  const pts = powHist.map((v,i)=>[i*(W/(POW_HIST_N-1)), H - (v/max)*H]);
  const d = pts.map((p,i)=>(i?'L':'M')+p[0].toFixed(1)+','+p[1].toFixed(1)).join(' ');
  svg.innerHTML = '<path d="'+d+' L'+W+','+H+' L0,'+H+' Z" fill="rgba(255,138,20,.10)"/>'+
                  '<path d="'+d+'" fill="none" stroke="#ff8a14" stroke-width="1.5"/>';
}

function powerZone(p){
  // Rough zones for an FTP of ~250W (Monark ergometer rider). Bin 1..7.
  const ftp = 250;
  if (p<.55*ftp) return 1; if (p<.75*ftp) return 2; if (p<.9*ftp) return 3;
  if (p<1.05*ftp) return 4; if (p<1.2*ftp) return 5; if (p<1.5*ftp) return 6; return 7;
}

function buildZoneBars(active){
  const colors=['#7a6f5e','#a3c46a','#ffb84d','#ff8a14','#e85a3c','#c63a1f','#7a1f12'];
  const heights=[24,40,55,38,28,18,12]; // visual rhythm
  const wrap = document.getElementById('zoneBars'); wrap.innerHTML='';
  for(let i=0;i<7;i++){
    const col=document.createElement('div'); col.className='col';
    const b=document.createElement('div'); b.className='b';
    const isActive = (i+1)===active;
    b.style.height = (isActive?heights[i]+8:heights[i])+'px';
    b.style.background = colors[i];
    b.style.opacity = isActive ? 1 : .55;
    const lab=document.createElement('span'); lab.textContent='Z'+(i+1);
    if(isActive) lab.style.color='var(--accent)';
    col.appendChild(b); col.appendChild(lab); wrap.appendChild(col);
  }
}

async function pollDash(){
  try{
    const res = await fetch('/api/status');
    const d = await res.json();
    const p = d.power || 0, r = d.rpm || 0, kp = d.kp || 0, adc = d.adc || 0;
    document.getElementById('powerBig').textContent = Math.round(p);
    document.getElementById('powerSub').textContent = '3s · '+Math.round(p);
    document.getElementById('rpmTile').textContent = Math.round(r);
    document.getElementById('kpTile').textContent = kp.toFixed(2);
    document.getElementById('adcTile').textContent = Math.round(adc);
    if(p>0){ powerSum+=p; powerN++; if(p>powerMax) powerMax=p; }
    document.getElementById('powerAvg').textContent = powerN? Math.round(powerSum/powerN) : '—';
    document.getElementById('powerMax').textContent = powerMax? Math.round(powerMax) : '—';
    document.getElementById('dashSession').textContent = fmtClock(Date.now()-sessionStart);
    powHist.push(p); while(powHist.length>POW_HIST_N) powHist.shift();
    drawSpark();
    const z = p>10? powerZone(p) : 0;
    buildZoneBars(z);
    document.getElementById('zoneLabel').textContent = z? ('Z'+z) : '—';
  } catch(e){ /* ignore transient errors */ }
}
buildZoneBars(0);
setInterval(pollDash, 1000);
pollDash();

// ───── WiFi ─────
let scanTimer = null;
async function refreshWifi(){
  try {
    const res = await fetch('/api/wifi');
    const d = await res.json();
    const ip = d.ip || '';
    const isAP = !!d.isAPMode;
    const connected = !!d.connected;
    document.getElementById('wifiCurrentLabel').textContent = isAP ? 'access point' : (connected ? 'connected' : 'disconnected');
    document.getElementById('wifiCurrentIp').textContent = ip;
    document.getElementById('wifiCurrentSsid').textContent = d.ssid || (isAP ? 'monark-ap' : '—');
    document.getElementById('wifiMode').textContent = isAP ? 'ap' : 'sta';
    document.getElementById('wifiAux').textContent = d.configured ? 'sta saved' : 'no sta credentials';
    document.getElementById('ipAddr').textContent = ip || '—';
  } catch(e) { document.getElementById('wifiCurrentLabel').textContent = 'fetch error'; }
}

async function startScan(){
  const btn = document.getElementById('scanBtn'); btn.disabled = true;
  document.getElementById('scanStatus').textContent = 'scanning...';
  document.getElementById('wifiList').innerHTML = '';
  await fetch('/api/wifi/scan', {method:'POST'});
  if (scanTimer) clearInterval(scanTimer);
  scanTimer = setInterval(pollScan, 1500);
  pollScan();
}
async function pollScan(){
  const res = await fetch('/api/wifi/scan');
  const d = await res.json();
  if (d.status === 'done') {
    clearInterval(scanTimer); scanTimer=null;
    document.getElementById('scanBtn').disabled = false;
    renderScan(d.results || []);
  } else {
    document.getElementById('scanStatus').textContent = d.status;
  }
}
function rssiToBars(rssi){ if(rssi>=-55)return 4; if(rssi>=-66)return 3; if(rssi>=-77)return 2; return 1; }
function renderScan(list){
  const wrap = document.getElementById('wifiList'); wrap.innerHTML = '';
  if (!list.length) { document.getElementById('scanStatus').textContent = 'no networks visible'; return; }
  document.getElementById('scanStatus').textContent = list.length+' networks';
  list.sort((a,b)=>b.rssi-a.rssi).forEach(n=>{
    const row = document.createElement('div'); row.className='wifi-row';
    const bars = rssiToBars(n.rssi);
    row.innerHTML = '<div><div class="ssid">'+escapeHtml(n.ssid||'(hidden)')+'</div>'+
      '<div class="meta">'+(n.secure?'WPA2':'OPEN')+' · ch'+n.channel+' · '+n.rssi+'dBm</div></div>'+
      '<div class="right"><div class="bars">'+
      [1,2,3,4].map(i=>'<i'+(i<=bars?' class="on"':'')+'></i>').join('')+
      '</div></div>';
    row.onclick = ()=>{
      document.getElementById('wifiSsid').value = n.ssid;
      document.getElementById('wifiPass').value = '';
      showWifiManual();
    };
    wrap.appendChild(row);
  });
}
function showWifiManual(){ document.getElementById('wifiManual').classList.remove('hidden'); }
function hideWifiManual(){ document.getElementById('wifiManual').classList.add('hidden'); }
async function saveWifi(){
  const ssid=document.getElementById('wifiSsid').value.trim();
  const pass=document.getElementById('wifiPass').value;
  if(!ssid){ toast('SSID required', true); return; }
  const body = pass ? {ssid, password:pass} : {ssid};
  const res = await fetch('/api/wifi', {method:'POST', headers:{'Content-Type':'application/json'}, body: JSON.stringify(body)});
  const d = await res.json();
  toast(d.success ? 'saved · reboot to apply' : (d.error||'error'), !d.success);
}
async function reconnectSta(){
  const btn = document.getElementById('reconnectBtn');
  btn.disabled = true; const old = btn.textContent; btn.textContent = 'Reconnecting…';
  try {
    await fetch('/api/wifi/reconnect', {method:'POST'});
    toast('STA retry kicked off — wait ~60s and refresh');
  } catch(e){ toast('error: '+e, true); }
  setTimeout(()=>{ btn.disabled=false; btn.textContent=old; refreshWifi(); }, 5000);
}
async function clearWifi(){
  if(!confirm('Forget saved WiFi credentials?')) return;
  await fetch('/api/wifi', {method:'DELETE'});
  toast('cleared · reboot to apply');
}

// ───── Calibration ─────
const CAL_LABELS = ['ready','set 0kp','set 2kp','set 4kp','set 6kp','done'];
const CAL_KP = [null, 0, 2, 4, 6];

async function refreshCal(){
  const res = await fetch('/api/status');
  const d = await res.json();
  const step = d.cal && d.cal.step != null ? d.cal.step : 0;
  document.getElementById('calStepNum').textContent = step+'/4';
  ['cal0','cal2','cal4','cal6'].forEach((id,i)=>{
    const v = d.cal && d.cal.values ? d.cal.values['adc'+[0,2,4,6][i]] : 0;
    document.getElementById(id).textContent = v||0;
  });
  // step indicator
  const segs = document.querySelectorAll('#calSteps .seg');
  segs.forEach((s,i)=>s.classList.toggle('on', i < step));
  // live ADC
  const adc = d.cal ? d.cal.adc : (d.adc||0);
  document.getElementById('calAdcLive').textContent = Math.round(adc);
  document.getElementById('calAdcRaw').textContent = adc.toFixed ? adc.toFixed(1) : adc;
  // map 0..1023 to 0..100% width
  const pct = Math.min(100, Math.max(0, (adc/1023)*100));
  document.getElementById('calAdcFill').style.width = pct.toFixed(1)+'%';

  const btnNext = document.getElementById('calNextBtn');
  const btnCancel = document.getElementById('calCancelBtn');
  if (step === 0) {
    btnNext.textContent = 'Start';
    btnCancel.disabled = true;
    document.getElementById('calMsg').innerHTML = "Click <b>Start</b> to begin calibration. You'll set the pendulum to 0 → 2 → 4 → 6 kp in order.<small>The capture reads the live smoothed ADC. Hold the pendulum still; the chrome above shows current load.</small>";
  } else if (step >= 1 && step <= 4) {
    const kp = CAL_KP[step];
    btnNext.textContent = (step === 4 ? 'Save & Finish' : 'Capture & Next →');
    btnCancel.disabled = false;
    document.getElementById('calMsg').innerHTML = "Set pendulum to <b>"+kp+" kp</b> and hold steady. When the live ADC settles (the bar above stops moving), tap <b>Capture</b>.";
  } else if (step === 5) {
    btnNext.textContent = 'Done';
    btnCancel.disabled = true;
    document.getElementById('calMsg').innerHTML = "Calibration <b>saved</b>. New ADC quartet active. <small>Tap Done to clear, or revisit any time.</small>";
  }
}
async function nextCal(){
  const res = await fetch('/api/calibrate/next', {method:'POST'});
  const d = await res.json();
  if(d.success === false) toast(d.error||'error', true);
  refreshCal();
}
async function cancelCal(){
  await fetch('/api/calibrate/cancel', {method:'POST'});
  refreshCal();
}
setInterval(()=>{ if(!document.getElementById('screen-cal').classList.contains('hidden')) refreshCal(); }, 500);

// ───── Settings ─────
async function refreshSettings(){
  try {
    const dev = await (await fetch('/api/device')).json();
    document.getElementById('setDevName').value = dev.name || 'monark';
    document.getElementById('devShort').textContent = dev.name || 'monark';
    document.getElementById('dashName').textContent = (dev.name || 'monark').toLowerCase();
    const cal = await (await fetch('/api/calibration')).json();
    document.getElementById('setCycCurrent').textContent = (cal.cycleConstant!=null? cal.cycleConstant.toFixed(3) : '1.05');
    const sim = await (await fetch('/api/simulator')).json();
    setSimUi(sim.enabled);
  } catch(e){}
}
function setSimUi(on){
  const t = document.getElementById('setSimToggle');
  t.classList.toggle('on', !!on); t.classList.toggle('off', !on);
  document.getElementById('setSimVal').textContent = on ? 'on' : 'off';
}
async function toggleSim(){
  const cur = document.getElementById('setSimToggle').classList.contains('on');
  await fetch('/api/simulator', {method:'POST', headers:{'Content-Type':'application/json'}, body: JSON.stringify({enabled: !cur})});
  setSimUi(!cur);
  toast('simulator '+(!cur?'on':'off')+' · reboot to apply');
}
async function saveDevName(){
  const name = document.getElementById('setDevName').value.trim();
  if(!name){ toast('name required', true); return; }
  await fetch('/api/device', {method:'POST', headers:{'Content-Type':'application/json'}, body: JSON.stringify({name})});
  toast('saved · reboot to apply');
  document.getElementById('devShort').textContent = name;
  document.getElementById('dashName').textContent = name.toLowerCase();
}
async function rebootDevice(){
  if(!confirm('Reboot device?')) return;
  await fetch('/api/reboot', {method:'POST'});
  toast('rebooting…');
}
async function otaUpload(){
  const f = document.getElementById('otaFile').files[0];
  if(!f){ toast('pick a .bin file', true); return; }
  const btn = document.getElementById('otaBtn'); btn.disabled = true; btn.textContent='Uploading…';
  const fd = new FormData(); fd.append('firmware', f);
  try {
    const res = await fetch('/update', {method:'POST', body: fd});
    const txt = await res.text();
    toast(txt === 'OK' ? 'flashed · rebooting' : 'flash failed', txt !== 'OK');
  } catch(e){ toast('upload error', true); }
  btn.disabled = false; btn.textContent='Upload & flash';
}

function escapeHtml(s){return (s||'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));}

// initial load
refreshWifi();
refreshSettings();
</script>
</body>
</html>
)rawhtml";
    _server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
        // Chunked PROGMEM response — no String copy, no heap pressure.
        request->send_P(200, "text/html", reinterpret_cast<const uint8_t*>(INDEX_HTML),
                        sizeof(INDEX_HTML) - 1);
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
    // Trim leading / trailing whitespace — copy-paste from a password manager
    // often sneaks in stray spaces that cause silent reason=2 (auth) failures.
    ssid.trim();
    if (ssid.length() == 0 || ssid.length() > 32) {
        request->send(400, "application/json", "{\"success\":false,\"error\":\"SSID must be 1-32 characters\"}");
        return;
    }

    // If the client omits "password", keep the currently-saved password.
    // (The form's password field is always blank on page load — sending "" would
    // wipe a working password just because the user pressed Save without retyping.)
    String password;
    bool passwordProvided = doc.containsKey("password");
    if (passwordProvided) {
        password = doc["password"].as<String>();
        // Same whitespace-trim as SSID — protects against accidental
        // leading/trailing spaces in pasted PSKs.
        password.trim();
        if (password.length() > 63) {
            request->send(400, "application/json", "{\"success\":false,\"error\":\"Password too long\"}");
            return;
        }
    } else {
        String oldSsid;
        _settings->loadWiFi(oldSsid, password); // keep the existing password
    }

    _settings->saveWiFi(ssid.c_str(), password.c_str());
    Serial.printf("WiFi credentials saved: %s (pass %d chars, %s)\n",
                  ssid.c_str(), password.length(),
                  passwordProvided ? "provided" : "kept-existing");
    // Hex dump of first 4 + last 4 bytes — lets us spot copy-paste artefacts
    // (smart quotes, look-alike unicode, control chars) that look fine on
    // screen but cause silent reason=2 auth failures.
    if (passwordProvided && password.length() > 0) {
        size_t n = password.length();
        const char* p = password.c_str();
        Serial.print("  password bytes:");
        for (size_t i = 0; i < n; i++) {
            if (i == 4 && n > 8) { Serial.print(" .."); i = n - 4; }
            Serial.printf(" %02X", (uint8_t)p[i]);
        }
        Serial.println();
    }

    request->send(200, "application/json", "{\"success\":true,\"message\":\"Restart to connect to WiFi\"}");
}

void PowerWebServer::handleClearWiFi(AsyncWebServerRequest* request) {
    _settings->clearWiFi();
    Serial.println("WiFi credentials cleared");
    request->send(200, "application/json", "{\"success\":true,\"message\":\"WiFi cleared. Restart to use AP mode\"}");
}

void PowerWebServer::handleCalibrationStatus(AsyncWebServerRequest* request) {
    JsonDocument doc;

    const char* stateNames[] = {"idle", "0kp", "2kp", "4kp", "6kp", "done"};
    doc["state"] = stateNames[_calState];
    doc["step"] = (int)_calState;
    doc["adc"] = _lastSample.adc_raw;
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

    // Capture from PowerReal's already-smoothed sample. No blocking ADC reads on
    // the AsyncTCP request thread — that was the source of the lag.
    int adcValue = (int)roundf(_lastSample.adc_raw);
    Serial.printf("Captured ADC: %d for state %d\n", adcValue, (int)_calState);

    // Order: 0kp -> 2kp -> 4kp -> 6kp (stock firmware order).
    switch (_calState) {
        case CAL_0KP:
            _calValues[0] = adcValue;
            _calState = CAL_2KP;
            request->send(200, "application/json", "{\"success\":true,\"message\":\"Set pendulum to 2 kp and click Next\"}");
            break;
        case CAL_2KP:
            _calValues[1] = adcValue;
            _calState = CAL_4KP;
            request->send(200, "application/json", "{\"success\":true,\"message\":\"Set pendulum to 4 kp and click Next\"}");
            break;
        case CAL_4KP:
            _calValues[2] = adcValue;
            _calState = CAL_6KP;
            request->send(200, "application/json", "{\"success\":true,\"message\":\"Set pendulum to 6 kp and click Next\"}");
            break;
        case CAL_6KP:
            _calValues[3] = adcValue;
            _settings->saveCalibration(_calValues[0], _calValues[1], _calValues[2], _calValues[3]);
            _calibration->updateValues(_calValues[0], _calValues[1], _calValues[2], _calValues[3]);
            Serial.printf("Calibration saved: 0kp=%d 2kp=%d 4kp=%d 6kp=%d\n",
                          _calValues[0], _calValues[1], _calValues[2], _calValues[3]);
            _calState = CAL_DONE;
            request->send(200, "application/json", "{\"success\":true,\"message\":\"Calibration complete and saved!\"}");
            break;
        case CAL_DONE:
            _calState = CAL_IDLE;
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

void PowerWebServer::handleUpdate(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
    if (!index) {
        Serial.printf("Update Start: %s\n", filename.c_str());
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
            Update.printError(Serial);
        }
    }
    if (!Update.hasError()) {
        if (Update.write(data, len) != len) {
            Update.printError(Serial);
        }
    }
    if (final) {
        if (Update.end(true)) {
            Serial.printf("Update Success: %uB\n", index + len);
        } else {
            Update.printError(Serial);
        }
    }
}

