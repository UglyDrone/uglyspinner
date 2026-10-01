#include "dshot_motor.h"
#include "telemetry/esc_telemetry.h"
#include <esp_log.h>

static const char* TAG = "DShotMotor";

DShotMotor motorDriver;

DShotMotor::DShotMotor()
    : _dshotDriver(nullptr),
      _mode(DEFAULT_DSHOT_MODE),
      _bidirectional(DSHOT_ENABLE_BIDIRECTIONAL),
      _commandedThrottle(0),
      _armed(false),
      _emergencyStopTriggered(false),
      _lastWatchdogFeedMs(0),
      _taskRunning(false),
      _taskHandle(nullptr) {
}

DShotMotor::~DShotMotor() {
    stop();
    if (_taskRunning) {
        _taskRunning = false;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    if (_dshotDriver) {
        delete _dshotDriver;
        _dshotDriver = nullptr;
    }
}

bool DShotMotor::begin() {
    ESP_LOGI(TAG, "Initializing DShot motor driver on GPIO%d (Mode: %s, Bidirectional: %s)...", 
             PIN_DSHOT_SIGNAL, dshot_mode_name[_mode], _bidirectional.load() ? "YES" : "NO");

    _commandedThrottle = 0;
    _armed = false;
    _emergencyStopTriggered = false;
    _lastWatchdogFeedMs = millis();

    if (!_reinitDriver()) {
        ESP_LOGE(TAG, "Failed to initialize DShot driver!");
        return false;
    }

    // Start dedicated high-priority motor output FreeRTOS task on Core 1
    _taskRunning = true;
    BaseType_t res = xTaskCreatePinnedToCore(
        _motorTaskStub,
        "DShotMotorTask",
        4096,
        this,
        configMAX_PRIORITIES - 1, // High priority to guarantee regular DShot timing
        &_taskHandle,
        1 // Core 1 (Core 0 handles Wi-Fi and WebServer)
    );

    if (res != pdPASS) {
        ESP_LOGE(TAG, "Failed to create motor task!");
        return false;
    }

    ESP_LOGI(TAG, "Motor driver initialized successfully in safe STOP state.");
    return true;
}

bool DShotMotor::arm() {
    if (_emergencyStopTriggered) {
        ESP_LOGW(TAG, "Cannot arm: Emergency stop flag is active. Clear error first.");
        return false;
    }
    _commandedThrottle = 0;
    _lastWatchdogFeedMs = millis();
    _armed = true;
    ESP_LOGI(TAG, "Motor ARMED. Ready for test commands.");
    return true;
}

void DShotMotor::disarm() {
    _commandedThrottle = 0;
    _armed = false;
    _sendZeroThrottleFrame();
    ESP_LOGI(TAG, "Motor DISARMED. Throttle forced to 0.");
}

bool DShotMotor::setThrottle(uint16_t throttle) {
    // Safety check 1: Emergency stop
    if (_emergencyStopTriggered) {
        _commandedThrottle = 0;
        _sendZeroThrottleFrame();
        return false;
    }

    // Safety check 2: Disarmed state
    if (!_armed && throttle != 0) {
        ESP_LOGW(TAG, "Rejected throttle %u: Motor is not armed!", throttle);
        _commandedThrottle = 0;
        _sendZeroThrottleFrame();
        return false;
    }

    // Safety check 3: Zero throttle is always accepted
    if (throttle == 0) {
        _commandedThrottle = 0;
        return true;
    }

    // Safety check 4: Range validation
    if (throttle < DSHOT_THROTTLE_MIN_RUN || throttle > DSHOT_THROTTLE_MAX_RUN) {
        ESP_LOGE(TAG, "Rejected throttle %u: Out of valid range [%u..%u]!", 
                 throttle, DSHOT_THROTTLE_MIN_RUN, DSHOT_THROTTLE_MAX_RUN);
        _commandedThrottle = 0;
        _sendZeroThrottleFrame();
        return false;
    }

    _commandedThrottle = throttle;
    _lastWatchdogFeedMs = millis();
    return true;
}

void DShotMotor::stop() {
    _commandedThrottle = 0;
    _sendZeroThrottleFrame();
    ESP_LOGI(TAG, "Motor STOP commanded.");
}

void DShotMotor::emergencyStop() {
    _emergencyStopTriggered = true;
    _armed = false;
    _commandedThrottle = 0;
    _sendZeroThrottleFrame();
    ESP_LOGE(TAG, "EMERGENCY STOP TRIGGERED! Motor output disabled.");
}

void DShotMotor::feedWatchdog() {
    _lastWatchdogFeedMs = millis();
}

uint16_t DShotMotor::getThrottle() const {
    return _commandedThrottle.load();
}

bool DShotMotor::isArmed() const {
    return _armed.load();
}

bool DShotMotor::isEmergencyStopped() const {
    return _emergencyStopTriggered.load();
}

dshot_mode_t DShotMotor::getDShotMode() const {
    return _mode;
}

bool DShotMotor::setDShotMode(dshot_mode_t mode) {
    if (mode == _mode) return true;
    _mode = mode;
    return _reinitDriver();
}

bool DShotMotor::setBidirectional(bool bidi) {
    if (bidi == _bidirectional.load()) return true;
    _bidirectional = bidi;
    return _reinitDriver();
}

bool DShotMotor::isBidirectional() const {
    return _bidirectional.load();
}

bool DShotMotor::_reinitDriver() {
    _commandedThrottle = 0;
    _sendZeroThrottleFrame();
#if !MOCK_ESC
    if (_dshotDriver) {
        delete _dshotDriver;
        _dshotDriver = nullptr;
    }

    if (_bidirectional.load()) {
        pinMode(PIN_DSHOT_SIGNAL, INPUT_PULLUP);
        gpio_pullup_en((gpio_num_t)PIN_DSHOT_SIGNAL);
        gpio_set_pull_mode((gpio_num_t)PIN_DSHOT_SIGNAL, GPIO_PULLUP_ONLY);
    } else {
        // Standard DShot: Push-pull output, idle LOW (0V), max drive strength
        pinMode(PIN_DSHOT_SIGNAL, OUTPUT);
        digitalWrite(PIN_DSHOT_SIGNAL, LOW);
        gpio_pullup_dis((gpio_num_t)PIN_DSHOT_SIGNAL);
        gpio_pulldown_en((gpio_num_t)PIN_DSHOT_SIGNAL);
        gpio_set_pull_mode((gpio_num_t)PIN_DSHOT_SIGNAL, GPIO_PULLDOWN_ONLY);
        gpio_set_drive_capability((gpio_num_t)PIN_DSHOT_SIGNAL, GPIO_DRIVE_CAP_3);
    }

    _dshotDriver = new DShotRMT(PIN_DSHOT_SIGNAL, _mode, _bidirectional.load(), DEFAULT_MOTOR_POLES);
    dshot_result_t res = _dshotDriver->begin();
    if (!res.success) {
        ESP_LOGE(TAG, "Failed to initialize DShotRMT! (code: %d)", res.result_code);
        return false;
    }

    if (!_bidirectional.load()) {
        // Enforce maximum drive capability on GPIO pin after RMT channel attachment
        gpio_set_drive_capability((gpio_num_t)PIN_DSHOT_SIGNAL, GPIO_DRIVE_CAP_3);
    }

    ESP_LOGI(TAG, "DShotRMT initialized: mode=%s, bidirectional=%s", 
             dshot_mode_name[_mode], _bidirectional.load() ? "YES" : "NO");

    // Continuous burst of 250 zero-throttle frames (~500ms) to allow AM32 to detect and arm on boot
    for (int i = 0; i < 250; i++) {
        _sendZeroThrottleFrame();
        delay(2);
    }
#endif
    return true;
}

void DShotMotor::_sendZeroThrottleFrame() {
#if !MOCK_ESC
    if (_dshotDriver) {
        _dshotDriver->sendThrottle(0);
    }
#endif
}

void DShotMotor::_motorTaskStub(void *param) {
    DShotMotor *motor = static_cast<DShotMotor*>(param);
    motor->_motorTaskLoop();
}

void DShotMotor::_motorTaskLoop() {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(2); // 500 Hz standard flight-controller output rate

    while (_taskRunning) {
        uint32_t now = millis();

        // Safety Watchdog check: If armed and throttle > 0, watchdog must be fed regularly
        if (_armed.load() && _commandedThrottle.load() > 0) {
            if (now - _lastWatchdogFeedMs.load() > TEST_WATCHDOG_TIMEOUT_MS) {
                ESP_LOGE(TAG, "WATCHDOG TIMEOUT! No heartbeat received within %d ms. Forcing STOP.", 
                         TEST_WATCHDOG_TIMEOUT_MS);
                _commandedThrottle = 0;
                _emergencyStopTriggered = true;
                _armed = false;
            }
        }

        uint16_t currentThrottle = _commandedThrottle.load();
        if (_emergencyStopTriggered.load() || !_armed.load()) {
            currentThrottle = 0;
        }

#if !MOCK_ESC
        if (_dshotDriver) {
            dshot_result_t res = _dshotDriver->sendThrottle(currentThrottle);
            if (res.success && res.result_code == DSHOT_TRANSMISSION_SUCCESS) {
                _txFrameCount++;
            } else if (!res.success) {
                _txFailCount++;
                _txLastErrCode = res.result_code;
            }
            _txLastRawValue = _dshotDriver->getEncodedFrameValue();

            // Read telemetry from bidirectional DShot (eRPM, voltage, current, temp, mAh)
            dshot_result_t tlm = _dshotDriver->getTelemetry();
            if (tlm.success) {
                escTelemetry.updateFromBidirectionalDShot(tlm.erpm, tlm.motor_rpm, tlm.telemetry_available, tlm.telemetry_data);
            }
        }
#endif

        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }

    vTaskDelete(NULL);
}
