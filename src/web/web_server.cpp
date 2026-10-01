#include "web_server.h"
#include "web_ui.h"
#include "test/test_runner.h"
#include "motor/dshot_motor.h"
#include "telemetry/esc_telemetry.h"
#include "loadcell/loadcell.h"
#include "storage/test_results.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include <esp_log.h>

static const char* TAG = "WebServer";

TestStandWebServer webServer(HTTP_SERVER_PORT);

TestStandWebServer::TestStandWebServer(uint16_t port)
    : _server(port), _port(port) {
}

TestStandWebServer::~TestStandWebServer() {
    _server.stop();
}

bool TestStandWebServer::begin() {
    ESP_LOGI(TAG, "Starting HTTP server on port %u...", _port);
    _setupRoutes();
    _server.begin();
    ESP_LOGI(TAG, "HTTP Server started.");
    return true;
}

void TestStandWebServer::update() {
    _server.handleClient();
}

void TestStandWebServer::_setupRoutes() {
    _server.on("/", HTTP_GET, [this]() { _handleRoot(); });
    _server.on("/api/status", HTTP_GET, [this]() { _handleStatus(); });
    _server.on("/api/start", HTTP_POST, [this]() { _handleStart(); });
    _server.on("/api/abort", HTTP_POST, [this]() { _handleAbort(); });
    _server.on("/api/tare", HTTP_POST, [this]() { _handleTare(); });
    _server.on("/api/results", HTTP_GET, [this]() { _handleResults(); });
    _server.on("/api/export.json", HTTP_GET, [this]() { _handleExportJson(); });
    _server.on("/api/loadcell/debug", HTTP_GET, [this]() { _handleLoadCellDebug(); });
    _server.on("/api/loadcell/config", HTTP_POST, [this]() { _handleLoadCellConfig(); });
    _server.on("/api/loadcell/calibrate", HTTP_POST, [this]() { _handleLoadCellCalibrate(); });
    _server.on("/api/esc/debug", HTTP_GET, [this]() { _handleEscDebug(); });
    _server.on("/api/esc/config", HTTP_POST, [this]() { _handleEscConfig(); });
    _server.on("/api/motor/throttle", HTTP_POST, [this]() { _handleMotorThrottle(); });
    _server.on("/api/diag/pins", HTTP_GET, [this]() { _handlePinDiag(); });
    _server.on("/api/esc/sniff", HTTP_GET, [this]() { _handleEscSniff(); });
    _server.onNotFound([this]() { _handleNotFound(); });
}

void TestStandWebServer::_handleRoot() {
    _server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    _server.send_P(200, "text/html", INDEX_HTML);
}

void TestStandWebServer::_handleStatus() {
    LiveTestStatus st = testRunner.getLiveStatus();
    EscTelemetryData esc = escTelemetry.getSnapshot();

    JsonDocument doc;
    doc["state_str"] = st.stateString;
    doc["throttle"] = st.commandedThrottle;
    doc["step_index"] = st.currentStep;
    doc["total_steps"] = st.totalSteps;
    doc["progress_pct"] = st.progressPercent;
    doc["elapsed_ms"] = st.testElapsedMs;
    doc["step_rem_ms"] = st.stepRemainingMs;
    doc["last_error"] = st.lastError;

    doc["load_g"] = st.currentLoad;
    doc["rpm"] = st.currentRpm;
    doc["erpm"] = st.currentErpm;
    doc["current_a"] = st.currentAmps;
    doc["voltage_v"] = st.currentVolts;
    doc["temp_c"] = st.currentTemp;
    doc["consumption_mah"] = esc.consumption_mah;

    doc["wifi_ip"] = WiFi.isConnected() ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
    doc["wifi_rssi"] = WiFi.isConnected() ? WiFi.RSSI() : 0;

    uint32_t tlmBytes = 0, tlmPkts = 0, tlmCrc = 0;
    String tlmHex;
    escTelemetry.getDebugInfo(tlmBytes, tlmPkts, tlmCrc, tlmHex);
    doc["tlm_healthy"] = esc.is_healthy;
    doc["tlm_packets"] = esc.total_packets;
    doc["tlm_bytes"] = tlmBytes;
    doc["tlm_crc_err"] = esc.crc_errors;

    uint32_t lcBytes = 0;
    String lcHex, lcAscii;
    LoadCellSample lcSample;
    if (activeLoadCell) {
        activeLoadCell->getDebugInfo(lcBytes, lcHex, lcAscii);
        activeLoadCell->getLatestSample(lcSample);
    }
    doc["lc_healthy"] = activeLoadCell ? activeLoadCell->isHealthy() : false;
    doc["lc_bytes"] = lcBytes;
    doc["lc_adc"] = lcSample.rawAdc;
    doc["motor_poles"] = escTelemetry.getMotorPoles();
    doc["dshot_mode"] = "DSHOT600";

    String jsonResponse;
    serializeJson(doc, jsonResponse);

    _server.sendHeader("Access-Control-Allow-Origin", "*");
    _server.send(200, "application/json", jsonResponse);
}

void TestStandWebServer::_handleStart() {
    if (!_server.hasArg("plain")) {
        // Malformed request safety requirement: default motor to STOP
        motorDriver.stop();
        _server.send(400, "application/json", "{\"success\":false,\"error\":\"Missing request payload\"}");
        return;
    }

    String body = _server.arg("plain");
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);

    if (err) {
        // Malformed request: force motor stop
        motorDriver.stop();
        _server.send(400, "application/json", "{\"success\":false,\"error\":\"Malformed JSON payload\"}");
        return;
    }

    TestConfig cfg;
    cfg.initialThrottle = doc["initialThrottle"] | DEFAULT_INIT_THROTTLE;
    cfg.endThrottle = doc["endThrottle"] | DEFAULT_END_THROTTLE;
    cfg.throttleStep = doc["throttleStep"] | DEFAULT_THROTTLE_STEP;
    cfg.stabilizationMs = doc["stabilizationMs"] | DEFAULT_STABILIZATION_MS;
    cfg.measurementMs = doc["measurementMs"] | DEFAULT_MEASUREMENT_MS;
    cfg.motorPoles = doc["motorPoles"] | DEFAULT_MOTOR_POLES;

    String validationErr;
    if (!cfg.validate(validationErr)) {
        // Invalid configuration safety requirement: default to STOP
        motorDriver.stop();
        String resp = "{\"success\":false,\"error\":\"" + validationErr + "\"}";
        _server.send(422, "application/json", resp);
        return;
    }

    String startErr;
    if (!testRunner.startTest(cfg, startErr)) {
        motorDriver.stop();
        String resp = "{\"success\":false,\"error\":\"" + startErr + "\"}";
        _server.send(409, "application/json", resp);
        return;
    }

    _server.send(200, "application/json", "{\"success\":true,\"message\":\"Test sequence started\"}");
}

void TestStandWebServer::_handleAbort() {
    // Immediate thread-safe motor stop and test termination
    testRunner.abortTest("User pressed ABORT button in Web UI");
    _server.send(200, "application/json", "{\"success\":true,\"message\":\"Motor STOP and test abort commanded\"}");
}

void TestStandWebServer::_handleTare() {
    if (activeLoadCell) {
        activeLoadCell->tare();
        _server.send(200, "application/json", "{\"success\":true,\"message\":\"Load cell tared successfully\"}");
    } else {
        _server.send(500, "application/json", "{\"success\":false,\"error\":\"No active load cell driver\"}");
    }
}

void TestStandWebServer::_handleResults() {
    String json = testResults.summaryJson();
    _server.sendHeader("Access-Control-Allow-Origin", "*");
    _server.send(200, "application/json", json);
}

void TestStandWebServer::_handleExportJson() {
    String json = testResults.exportJson(true);
    _server.sendHeader("Content-Disposition", "attachment; filename=\"propeller_test_dataset.json\"");
    _server.send(200, "application/json", json);
}

void TestStandWebServer::_handleLoadCellDebug() {
    uint32_t byteCount = 0;
    String hexDump, asciiDump;
    LoadCellSample sample;
    
    if (activeLoadCell) {
        activeLoadCell->getDebugInfo(byteCount, hexDump, asciiDump);
        activeLoadCell->getLatestSample(sample);
    }

    JsonDocument doc;
    doc["healthy"] = activeLoadCell ? activeLoadCell->isHealthy() : false;
    doc["byte_count"] = byteCount;
    doc["raw_adc"] = sample.rawAdc;
    doc["load_g"] = sample.loadGrams;
    doc["hex_dump"] = hexDump;
    doc["ascii_dump"] = asciiDump;
    doc["baud"] = activeLoadCell ? activeLoadCell->getBaud() : LOADCELL_BAUD;
    doc["rx_pin"] = PIN_LOADCELL_RX;
    doc["tx_pin"] = PIN_LOADCELL_TX;
    doc["zero_offset"] = activeLoadCell ? activeLoadCell->getZeroOffset() : 0;
    doc["cal_factor"] = activeLoadCell ? activeLoadCell->getCalibrationFactor() : 1.0f;

    String resp;
    serializeJson(doc, resp);
    _server.sendHeader("Access-Control-Allow-Origin", "*");
    _server.send(200, "application/json", resp);
}

void TestStandWebServer::_handleLoadCellConfig() {
    if (!_server.hasArg("plain")) {
        _server.send(400, "application/json", "{\"error\":\"Missing JSON payload\"}");
        return;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, _server.arg("plain"));
    if (err) {
        _server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        return;
    }

    if (doc["baud"].is<uint32_t>() && activeLoadCell) {
        activeLoadCell->setBaud(doc["baud"].as<uint32_t>());
    }
    if (doc["cal_factor"].is<float>() && activeLoadCell) {
        activeLoadCell->setCalibrationFactor(doc["cal_factor"].as<float>());
    }
    if (doc["send_str"].is<const char*>() && activeLoadCell) {
        String s = doc["send_str"].as<String>();
        activeLoadCell->sendBytes((const uint8_t*)s.c_str(), s.length());
    }

    _server.send(200, "application/json", "{\"success\":true}");
}

void TestStandWebServer::_handleLoadCellCalibrate() {
    if (!_server.hasArg("plain")) {
        _server.send(400, "application/json", "{\"error\":\"Missing payload\"}");
        return;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, _server.arg("plain"));
    if (err) {
        _server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        return;
    }

    float knownG = doc["known_weight_g"] | 0.0f;
    if (knownG == 0.0f || !activeLoadCell) {
        _server.send(400, "application/json", "{\"error\":\"Invalid known weight\"}");
        return;
    }

    LoadCellSample sample;
    activeLoadCell->getLatestSample(sample);
    int32_t deltaAdc = sample.rawAdc - activeLoadCell->getZeroOffset();
    if (deltaAdc == 0) {
        _server.send(400, "application/json", "{\"error\":\"Scale reading matches zero offset. Place weight on stand before calibrating.\"}");
        return;
    }

    float newFactor = knownG / (float)deltaAdc;
    activeLoadCell->setCalibrationFactor(newFactor);

    JsonDocument respDoc;
    respDoc["success"] = true;
    respDoc["cal_factor"] = newFactor;
    respDoc["delta_adc"] = deltaAdc;
    respDoc["known_weight_g"] = knownG;
    String resp;
    serializeJson(respDoc, resp);
    _server.send(200, "application/json", resp);
}

void TestStandWebServer::_handleEscDebug() {
    uint32_t tlmBytes = 0, tlmPkts = 0, tlmCrc = 0;
    String tlmHex;
    escTelemetry.getDebugInfo(tlmBytes, tlmPkts, tlmCrc, tlmHex);
    EscTelemetryData snap = escTelemetry.getSnapshot();

    JsonDocument doc;
    doc["healthy"] = snap.is_healthy;
    doc["total_bytes"] = tlmBytes;
    doc["total_packets"] = snap.total_packets;
    doc["crc_errors"] = snap.crc_errors;
    doc["voltage_v"] = snap.voltage_v;
    doc["current_a"] = snap.current_a;
    doc["temperature_c"] = snap.temperature_c;
    doc["erpm"] = snap.erpm;
    doc["rpm"] = snap.rpm;
    doc["consumption_mah"] = snap.consumption_mah;
    doc["hex_dump"] = tlmHex;
    doc["rx_pin"] = PIN_ESC_TELEMETRY_RX;
    doc["pin_level"] = gpio_get_level((gpio_num_t)PIN_ESC_TELEMETRY_RX);
    doc["pullup"] = escTelemetry.isPullup();
    doc["dshot_pin"] = PIN_DSHOT_SIGNAL;
    doc["dshot_mode"] = dshot_mode_name[motorDriver.getDShotMode()];
    doc["dshot_bidirectional"] = motorDriver.isBidirectional();

    uint32_t dshotFrames = 0, dshotFails = 0;
    uint16_t dshotLastErr = 0, dshotLastRaw = 0;
    motorDriver.getTxStats(dshotFrames, dshotFails, dshotLastErr, dshotLastRaw);
    doc["dshot_frames"] = dshotFrames;
    doc["dshot_fails"] = dshotFails;
    doc["dshot_last_err"] = dshotLastErr;
    doc["dshot_last_raw"] = dshotLastRaw;

    doc["baud"] = escTelemetry.getBaud();
    doc["inverted"] = escTelemetry.isInverted();

    String resp;
    serializeJson(doc, resp);
    _server.sendHeader("Access-Control-Allow-Origin", "*");
    _server.send(200, "application/json", resp);
}

void TestStandWebServer::_handleMotorThrottle() {
    if (!_server.hasArg("plain")) {
        _server.send(400, "application/json", "{\"error\":\"Missing payload\"}");
        return;
    }
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, _server.arg("plain"));
    if (err) {
        _server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        return;
    }

    uint16_t throttle = doc["throttle"] | 0;
    if (throttle > 0 && throttle < DSHOT_THROTTLE_MIN_RUN) {
        throttle = DSHOT_THROTTLE_MIN_RUN;
    }
    if (throttle > 2047) throttle = 2047;

    if (throttle > 0) {
        motorDriver.arm();
        motorDriver.feedWatchdog();
        motorDriver.setThrottle(throttle);
    } else {
        motorDriver.stop();
        motorDriver.disarm();
    }
    _server.send(200, "application/json", "{\"success\":true,\"throttle\":" + String(throttle) + "}");
}

void TestStandWebServer::_handleEscConfig() {
    if (!_server.hasArg("plain")) {
        _server.send(400, "application/json", "{\"error\":\"Missing payload\"}");
        return;
    }
    JsonDocument doc;
    deserializeJson(doc, _server.arg("plain"));
    if (doc["invert"].is<bool>()) {
        escTelemetry.setInvert(doc["invert"].as<bool>());
    }
    if (doc["baud"].is<uint32_t>()) {
        escTelemetry.setBaud(doc["baud"].as<uint32_t>());
    }
    if (doc["pullup"].is<bool>()) {
        escTelemetry.setPullup(doc["pullup"].as<bool>());
    }
    if (doc["bidirectional"].is<bool>()) {
        motorDriver.setBidirectional(doc["bidirectional"].as<bool>());
    }
    if (doc["dshot_mode"].is<const char*>()) {
        String modeStr = doc["dshot_mode"].as<const char*>();
        if (modeStr == "DSHOT150") motorDriver.setDShotMode(DSHOT150);
        else if (modeStr == "DSHOT300") motorDriver.setDShotMode(DSHOT300);
        else if (modeStr == "DSHOT600") motorDriver.setDShotMode(DSHOT600);
    }
    _server.send(200, "application/json", "{\"success\":true,\"inverted\":" + String(escTelemetry.isInverted() ? "true" : "false") + 
                                          ",\"baud\":" + String(escTelemetry.getBaud()) + 
                                          ",\"pullup\":" + String(escTelemetry.isPullup() ? "true" : "false") +
                                          ",\"bidirectional\":" + String(motorDriver.isBidirectional() ? "true" : "false") +
                                          ",\"dshot_mode\":\"" + String(dshot_mode_name[motorDriver.getDShotMode()]) + "\"}");
}

void TestStandWebServer::_handlePinDiag() {
    JsonDocument doc;
    const int testPins[] = {0, 2, 4, 12, 13, 14, 15, 16, 17, 18, 19, 21, 22, 23, 25, 26, 27, 32, 33, 34, 35, 36, 39};
    const size_t numPins = sizeof(testPins) / sizeof(testPins[0]);
    int lastLevels[numPins];
    uint16_t transitions[numPins];
    memset(transitions, 0, sizeof(transitions));

    for (size_t i = 0; i < numPins; i++) {
        lastLevels[i] = gpio_get_level((gpio_num_t)testPins[i]);
    }

    uint32_t start = millis();
    // Sample for 50ms to catch active 30ms telemetry bursts
    while (millis() - start < 50) {
        for (size_t i = 0; i < numPins; i++) {
            int lvl = gpio_get_level((gpio_num_t)testPins[i]);
            if (lvl != lastLevels[i]) {
                transitions[i]++;
                lastLevels[i] = lvl;
            }
        }
    }

    for (size_t i = 0; i < numPins; i++) {
        JsonObject pinObj = doc[String("gpio_") + testPins[i]].to<JsonObject>();
        pinObj["level"] = lastLevels[i];
        pinObj["transitions"] = transitions[i];
    }
    String resp;
    serializeJson(doc, resp);
    _server.sendHeader("Access-Control-Allow-Origin", "*");
    _server.send(200, "application/json", resp);
}

void TestStandWebServer::_handleEscSniff() {
    static const size_t MAX_SAMPLES = 64;
    uint32_t durations_us[MAX_SAMPLES];
    size_t sampleCount = 0;

    int lastLvl = gpio_get_level((gpio_num_t)PIN_ESC_TELEMETRY_RX);
    int64_t lastTimeUs = esp_timer_get_time();
    uint32_t startMs = millis();

    // Sample for up to 120ms to catch 30ms-50ms telemetry frames
    while (millis() - startMs < 120 && sampleCount < MAX_SAMPLES) {
        int lvl = gpio_get_level((gpio_num_t)PIN_ESC_TELEMETRY_RX);
        if (lvl != lastLvl) {
            int64_t nowUs = esp_timer_get_time();
            durations_us[sampleCount++] = (uint32_t)(nowUs - lastTimeUs);
            lastTimeUs = nowUs;
            lastLvl = lvl;
        }
    }

    uint32_t minPulseUs = 999999;
    for (size_t i = 1; i < sampleCount; i++) {
        if (durations_us[i] >= 2 && durations_us[i] < minPulseUs) {
            minPulseUs = durations_us[i];
        }
    }

    uint32_t estBaud = (minPulseUs > 0 && minPulseUs < 999999) ? (1000000 / minPulseUs) : 0;

    JsonDocument doc;
    doc["pin"] = PIN_ESC_TELEMETRY_RX;
    doc["current_level"] = gpio_get_level((gpio_num_t)PIN_ESC_TELEMETRY_RX);
    doc["transition_count"] = sampleCount;
    doc["min_pulse_us"] = (minPulseUs == 999999) ? 0 : minPulseUs;
    doc["estimated_baud"] = estBaud;

    JsonArray arr = doc["intervals_us"].to<JsonArray>();
    for (size_t i = 0; i < sampleCount && i < 20; i++) {
        arr.add(durations_us[i]);
    }

    String resp;
    serializeJson(doc, resp);
    _server.sendHeader("Access-Control-Allow-Origin", "*");
    _server.send(200, "application/json", resp);
}

void TestStandWebServer::_handleNotFound() {
    // Safety requirement: Force throttle to STOP on malformed/invalid HTTP requests
    motorDriver.stop();
    _server.send(404, "text/plain", "404: Not Found - Motor stopped for safety.");
}
