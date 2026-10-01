#include "test_runner.h"
#include "motor/dshot_motor.h"
#include "telemetry/esc_telemetry.h"
#include "loadcell/loadcell.h"
#include <esp_log.h>
#include <cmath>

static const char* TAG = "TestRunner";

TestRunner testRunner;

bool TestConfig::validate(String &errorMsg) const {
    if (initialThrottle < TEST_MIN_THROTTLE_ALLOWED || initialThrottle > TEST_MAX_THROTTLE_ALLOWED) {
        errorMsg = "Initial throttle must be between " + String(TEST_MIN_THROTTLE_ALLOWED) + " and " + String(TEST_MAX_THROTTLE_ALLOWED);
        return false;
    }
    if (endThrottle < TEST_MIN_THROTTLE_ALLOWED || endThrottle > TEST_MAX_THROTTLE_ALLOWED) {
        errorMsg = "End throttle must be between " + String(TEST_MIN_THROTTLE_ALLOWED) + " and " + String(TEST_MAX_THROTTLE_ALLOWED);
        return false;
    }
    if (initialThrottle >= endThrottle) {
        errorMsg = "Initial throttle (" + String(initialThrottle) + ") must be strictly less than end throttle (" + String(endThrottle) + ")";
        return false;
    }
    if (throttleStep < TEST_MIN_STEP_ALLOWED || throttleStep > (endThrottle - initialThrottle)) {
        errorMsg = "Throttle step must be between " + String(TEST_MIN_STEP_ALLOWED) + " and " + String(endThrottle - initialThrottle);
        return false;
    }
    if (stabilizationMs < TEST_MIN_STABILIZE_MS || stabilizationMs > TEST_MAX_STABILIZE_MS) {
        errorMsg = "Stabilization time must be between " + String(TEST_MIN_STABILIZE_MS) + "ms and " + String(TEST_MAX_STABILIZE_MS) + "ms";
        return false;
    }
    if (measurementMs < TEST_MIN_MEASURE_MS || measurementMs > TEST_MAX_MEASURE_MS) {
        errorMsg = "Measurement duration must be between " + String(TEST_MIN_MEASURE_MS) + "ms and " + String(TEST_MAX_MEASURE_MS) + "ms";
        return false;
    }
    if (motorPoles < 2 || motorPoles > 64) {
        errorMsg = "Motor poles must be an even integer between 2 and 64";
        return false;
    }
    return true;
}

TestRunner::TestRunner()
    : _mutex(nullptr),
      _state(TEST_STATE_IDLE),
      _testStartTimeMs(0),
      _currentThrottle(0),
      _currentStepIndex(0),
      _totalStepCount(0),
      _stepStartTimeMs(0),
      _stepTargetDurationMs(0),
      _taskHandle(nullptr),
      _taskRunning(false),
      _abortRequested(false) {
}

TestRunner::~TestRunner() {
    abortTest("System destructing");
    _taskRunning = false;
    if (_taskHandle) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    if (_mutex) {
        vSemaphoreDelete(_mutex);
        _mutex = nullptr;
    }
}

bool TestRunner::begin() {
    _mutex = xSemaphoreCreateMutex();
    if (!_mutex) {
        ESP_LOGE(TAG, "Failed to create TestRunner mutex!");
        return false;
    }

    _taskRunning = true;
    BaseType_t res = xTaskCreatePinnedToCore(
        _taskStub,
        "TestRunnerTask",
        8192,
        this,
        4, // High priority coordinator task
        &_taskHandle,
        1 // Core 1
    );

    if (res != pdPASS) {
        ESP_LOGE(TAG, "Failed to start TestRunner task!");
        return false;
    }

    ESP_LOGI(TAG, "TestRunner coordinator started.");
    return true;
}

bool TestRunner::startTest(const TestConfig &cfg, String &errorMsg) {
    if (isRunning()) {
        errorMsg = "A test is already currently in progress!";
        return false;
    }

    if (!cfg.validate(errorMsg)) {
        return false;
    }

    xSemaphoreTake(_mutex, portMAX_DELAY);
    _config = cfg;
    _abortRequested = false;
    _state = TEST_STATE_PREFLIGHT_CHECK;
    _lastError = "";
    
    // Calculate total steps
    _totalStepCount = ((_config.endThrottle - _config.initialThrottle) / _config.throttleStep) + 1;
    _currentStepIndex = 0;
    _currentThrottle = 0;
    xSemaphoreGive(_mutex);

    // Apply motor poles to ESC telemetry decoder
    escTelemetry.setMotorPoles(_config.motorPoles);

    // Notify runner task via notification
    xTaskNotifyGive(_taskHandle);

    ESP_LOGI(TAG, "Test run initiated: Throttle %u -> %u (Step: %u, Total steps: %u)",
             _config.initialThrottle, _config.endThrottle, _config.throttleStep, _totalStepCount);
    return true;
}

void TestRunner::abortTest(const String &reason) {
    ESP_LOGW(TAG, "ABORTING TEST RUN: %s", reason.c_str());

    // Immediately stop motor output and disarm
    motorDriver.stop();
    motorDriver.disarm();

    xSemaphoreTake(_mutex, portMAX_DELAY);
    _abortRequested = true;
    _state = TEST_STATE_ABORTED;
    _lastError = reason;
    _currentThrottle = 0;
    xSemaphoreGive(_mutex);

    // Update metadata in test results
    TestMetadata meta = testResults.getMetadata();
    meta.status = "ABORTED";
    meta.abort_reason = reason;
    if (_testStartTimeMs > 0) {
        meta.run_duration_ms = millis() - _testStartTimeMs;
    }
    testResults.setMetadata(meta);
}

LiveTestStatus TestRunner::getLiveStatus() {
    LiveTestStatus status;
    
    if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        status.state = _state;
        status.commandedThrottle = _currentThrottle;
        status.currentStep = _currentStepIndex;
        status.totalSteps = _totalStepCount;
        status.lastError = _lastError;

        if (_state == TEST_STATE_IDLE) {
            status.stateString = "IDLE";
        } else if (_state == TEST_STATE_PREFLIGHT_CHECK) {
            status.stateString = "PREFLIGHT";
        } else if (_state == TEST_STATE_ARMING) {
            status.stateString = "ARMING";
        } else if (_state == TEST_STATE_ZERO_BASELINE) {
            status.stateString = "ZERO_BASELINE";
        } else if (_state == TEST_STATE_STABILIZING) {
            status.stateString = "STABILIZING";
        } else if (_state == TEST_STATE_MEASURING) {
            status.stateString = "MEASURING";
        } else if (_state == TEST_STATE_ADVANCING) {
            status.stateString = "ADVANCING";
        } else if (_state == TEST_STATE_COMPLETED) {
            status.stateString = "COMPLETED";
        } else if (_state == TEST_STATE_ABORTED) {
            status.stateString = "ABORTED";
        } else {
            status.stateString = "ERROR";
        }

        if (_testStartTimeMs > 0 && isRunning()) {
            status.testElapsedMs = millis() - _testStartTimeMs;
        }

        if (_stepStartTimeMs > 0 && _stepTargetDurationMs > 0) {
            uint32_t stepElapsed = millis() - _stepStartTimeMs;
            if (stepElapsed < _stepTargetDurationMs) {
                status.stepRemainingMs = _stepTargetDurationMs - stepElapsed;
            } else {
                status.stepRemainingMs = 0;
            }
        }

        if (_totalStepCount > 0) {
            status.progressPercent = ((float)_currentStepIndex / (float)_totalStepCount) * 100.0f;
            if (status.progressPercent > 100.0f) status.progressPercent = 100.0f;
        }

        xSemaphoreGive(_mutex);
    }

    // Append latest instantaneous sensor readings
    LoadCellSample lcSample;
    if (activeLoadCell) {
        activeLoadCell->getLatestSample(lcSample);
        status.currentLoad = lcSample.loadGrams;
    }

    EscTelemetryData escSnap = escTelemetry.getSnapshot();
    status.currentRpm = escSnap.rpm;
    status.currentErpm = escSnap.erpm;
    status.currentAmps = escSnap.current_a;
    status.currentVolts = escSnap.voltage_v;
    status.currentTemp = escSnap.temperature_c;

    return status;
}

TestConfig TestRunner::getConfig() const {
    return _config;
}

TestState TestRunner::getState() const {
    return _state;
}

bool TestRunner::isRunning() const {
    return (_state >= TEST_STATE_PREFLIGHT_CHECK && _state <= TEST_STATE_ADVANCING);
}

void TestRunner::_taskStub(void *param) {
    TestRunner *instance = static_cast<TestRunner*>(param);
    instance->_taskLoop();
}

void TestRunner::_taskLoop() {
    while (_taskRunning) {
        // Wait for start trigger
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        if (_abortRequested) {
            continue;
        }

        // =====================================================================
        // STEP 1: PRE-FLIGHT CHECK
        // =====================================================================
        xSemaphoreTake(_mutex, portMAX_DELAY);
        _state = TEST_STATE_PREFLIGHT_CHECK;
        xSemaphoreGive(_mutex);

#if !MOCK_ESC
        if (SAFETY_ABORT_ON_LOST_TLM && !escTelemetry.isHealthy()) {
            _handleError("Preflight failed: ESC Telemetry not receiving valid packets!");
            continue;
        }
#endif

#if !MOCK_LOADCELL
        if (!activeLoadCell->isHealthy()) {
            _handleError("Preflight failed: HX711 UART Load Cell module offline!");
            continue;
        }
#endif

        // Reset and initialize test storage
        testResults.reset();
        _testStartTimeMs = millis();

        TestMetadata meta;
        meta.firmware_version = FIRMWARE_VERSION;
        meta.dshot_mode = (_config.initialThrottle > 0) ? "DSHOT600" : "DSHOT300";
        meta.telemetry_protocol = "KISS/AM32";
        meta.start_time_ms = _testStartTimeMs;
        meta.initial_throttle = _config.initialThrottle;
        meta.end_throttle = _config.endThrottle;
        meta.throttle_step = _config.throttleStep;
        meta.stabilization_ms = _config.stabilizationMs;
        meta.measurement_ms = _config.measurementMs;
        meta.motor_poles = _config.motorPoles;
        meta.load_cell_zero_offset = activeLoadCell ? activeLoadCell->getZeroOffset() : 0;
        meta.load_units = LOADCELL_UNIT_STRING;
        meta.status = "RUNNING";
        testResults.setMetadata(meta);

        // =====================================================================
        // STEP 2 & 3: ENSURE ZERO THROTTLE & ARMING DELAY
        // =====================================================================
        _runArmingSequence();
        if (_abortRequested) continue;

        // =====================================================================
        // STEP 4: ZERO LOAD BASELINE (AUTO-TARE)
        // =====================================================================
        _runBaselineZero();
        if (_abortRequested) continue;

        // =====================================================================
        // STEP 5..11: THROTTLE STEP SEQUENCE
        // =====================================================================
        uint16_t targetThrottle = _config.initialThrottle;
        _currentStepIndex = 0;

        while (targetThrottle <= _config.endThrottle && !_abortRequested) {
            _currentStepIndex++;
            _runStep(targetThrottle);

            if (_abortRequested) break;

            if (targetThrottle == _config.endThrottle) {
                break; // Last step reached
            }

            targetThrottle += _config.throttleStep;
            if (targetThrottle > _config.endThrottle) {
                targetThrottle = _config.endThrottle;
            }
        }

        // =====================================================================
        // STEP 12 & 13: TEST COMPLETION & MOTOR STOP
        // =====================================================================
        if (!_abortRequested) {
            _completeTest();
        }
    }

    vTaskDelete(NULL);
}

void TestRunner::_runArmingSequence() {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    _state = TEST_STATE_ARMING;
    _currentThrottle = 0;
    _stepStartTimeMs = millis();
    _stepTargetDurationMs = ARMING_DELAY_MS;
    xSemaphoreGive(_mutex);

    // Motor driver safety arming
    motorDriver.arm();
    motorDriver.setThrottle(0);

    // Wait for ESC to calibrate and arm at zero throttle
    uint32_t start = millis();
    while (millis() - start < ARMING_DELAY_MS) {
        if (_abortRequested) return;
        motorDriver.feedWatchdog();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void TestRunner::_runBaselineZero() {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    _state = TEST_STATE_ZERO_BASELINE;
    _stepStartTimeMs = millis();
    _stepTargetDurationMs = 1000;
    xSemaphoreGive(_mutex);

    // Tare the load cell at zero throttle
    if (activeLoadCell) {
        activeLoadCell->tare();
    }

    uint32_t start = millis();
    while (millis() - start < 1000) {
        if (_abortRequested) return;
        motorDriver.feedWatchdog();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void TestRunner::_runStep(uint16_t throttle) {
    // -------------------------------------------------------------------------
    // Phase A: Stabilization Period
    // -------------------------------------------------------------------------
    xSemaphoreTake(_mutex, portMAX_DELAY);
    _state = TEST_STATE_STABILIZING;
    _currentThrottle = throttle;
    _stepStartTimeMs = millis();
    _stepTargetDurationMs = _config.stabilizationMs;
    xSemaphoreGive(_mutex);

    ESP_LOGI(TAG, "Step %u/%u: Commanding throttle %u (Stabilizing %u ms)...",
             _currentStepIndex, _totalStepCount, throttle, _config.stabilizationMs);

    if (!motorDriver.setThrottle(throttle)) {
        _handleError("Failed to set motor throttle to " + String(throttle));
        return;
    }

    uint32_t stabStart = millis();
    while (millis() - stabStart < _config.stabilizationMs) {
        if (_abortRequested) return;
        motorDriver.feedWatchdog();
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    // -------------------------------------------------------------------------
    // Phase B: Measurement Period (High-Frequency Synchronized Sampling)
    // -------------------------------------------------------------------------
    xSemaphoreTake(_mutex, portMAX_DELAY);
    _state = TEST_STATE_MEASURING;
    _stepStartTimeMs = millis();
    _stepTargetDurationMs = _config.measurementMs;
    xSemaphoreGive(_mutex);

    ESP_LOGI(TAG, "Step %u/%u: Measuring samples for %u ms...",
             _currentStepIndex, _totalStepCount, _config.measurementMs);

    std::vector<float> stepLoads;
    stepLoads.reserve(250);

    double sumLoad = 0.0;
    float minLoad = 1e9f;
    float maxLoad = -1e9f;

    double sumRpm = 0.0;
    double sumErpm = 0.0;
    double sumCurrent = 0.0;
    double sumVoltage = 0.0;
    double sumTemp = 0.0;
    uint32_t sampleCount = 0;

    uint32_t measureStart = millis();

    while (millis() - measureStart < _config.measurementMs) {
        if (_abortRequested) return;
        motorDriver.feedWatchdog();

        uint32_t nowMs = millis();
        uint32_t elapsedTestMs = nowMs - _testStartTimeMs;

        // Sample load cell
        LoadCellSample lc;
        if (activeLoadCell) {
            activeLoadCell->getLatestSample(lc);
        }

        // Sample ESC telemetry
        EscTelemetryData esc = escTelemetry.getSnapshot();

        // Accumulate statistics
        float load = lc.loadGrams;
        stepLoads.push_back(load);
        sumLoad += load;
        if (load < minLoad) minLoad = load;
        if (load > maxLoad) maxLoad = load;

        sumRpm += esc.rpm;
        sumErpm += esc.erpm;
        sumCurrent += esc.current_a;
        sumVoltage += esc.voltage_v;
        sumTemp += esc.temperature_c;
        sampleCount++;

        // Store raw individual sample for vibration/noise analysis
        RawMeasurement raw;
        raw.timestamp_ms = nowMs;
        raw.elapsed_ms = elapsedTestMs;
        raw.throttle = throttle;
        raw.load = load;
        raw.rpm = esc.rpm;
        raw.erpm = esc.erpm;
        raw.current_a = esc.current_a;
        raw.voltage_v = esc.voltage_v;
        raw.temperature_c = (float)esc.temperature_c;
        testResults.addRawSample(raw);

        vTaskDelay(pdMS_TO_TICKS(25));
    }

    // -------------------------------------------------------------------------
    // Phase C: Compute Descriptive Statistics
    // -------------------------------------------------------------------------
    if (sampleCount > 0) {
        float meanLoad = (float)(sumLoad / sampleCount);
        float meanRpm = (float)(sumRpm / sampleCount);
        float meanErpm = (float)(sumErpm / sampleCount);
        float meanCurrent = (float)(sumCurrent / sampleCount);
        float meanVoltage = (float)(sumVoltage / sampleCount);
        float meanTemp = (float)(sumTemp / sampleCount);

        // Standard deviation calculation
        double varianceSum = 0.0;
        for (float val : stepLoads) {
            double diff = val - meanLoad;
            varianceSum += diff * diff;
        }
        float stddevLoad = 0.0f;
        if (sampleCount > 1) {
            stddevLoad = (float)sqrt(varianceSum / (sampleCount - 1));
        }

        TestPoint pt;
        pt.throttle = throttle;
        pt.load.mean = meanLoad;
        pt.load.min = minLoad;
        pt.load.max = maxLoad;
        pt.load.stddev = stddevLoad;
        pt.load.samples = sampleCount;
        pt.rpm = (uint32_t)(meanRpm + 0.5f);
        pt.erpm = (uint32_t)(meanErpm + 0.5f);
        pt.current_a = meanCurrent;
        pt.voltage_v = meanVoltage;
        pt.temperature_c = meanTemp;

        testResults.addPoint(pt);

        ESP_LOGI(TAG, "Step %u result: Throttle=%u, Load=%.2fg (±%.2f), RPM=%u, Curr=%.2fA, Volt=%.2fV",
                 _currentStepIndex, throttle, meanLoad, stddevLoad, pt.rpm, meanCurrent, meanVoltage);
    }
}

void TestRunner::_completeTest() {
    ESP_LOGI(TAG, "Test sequence completed successfully! Returning throttle to ZERO.");
    
    // Safety: Immediate stop
    motorDriver.stop();
    motorDriver.disarm();

    xSemaphoreTake(_mutex, portMAX_DELAY);
    _state = TEST_STATE_COMPLETED;
    _currentThrottle = 0;
    xSemaphoreGive(_mutex);

    TestMetadata meta = testResults.getMetadata();
    meta.status = "COMPLETED";
    meta.run_duration_ms = millis() - _testStartTimeMs;
    testResults.setMetadata(meta);
}

void TestRunner::_handleError(const String &msg) {
    ESP_LOGE(TAG, "TEST ERROR: %s", msg.c_str());

    motorDriver.stop();
    motorDriver.disarm();

    xSemaphoreTake(_mutex, portMAX_DELAY);
    _state = TEST_STATE_ERROR;
    _lastError = msg;
    _currentThrottle = 0;
    xSemaphoreGive(_mutex);

    TestMetadata meta = testResults.getMetadata();
    meta.status = "ERROR";
    meta.abort_reason = msg;
    testResults.setMetadata(meta);
}
