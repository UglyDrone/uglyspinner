#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "config.h"
#include "storage/test_results.h"

enum TestState {
    TEST_STATE_IDLE = 0,
    TEST_STATE_PREFLIGHT_CHECK,
    TEST_STATE_ARMING,
    TEST_STATE_ZERO_BASELINE,
    TEST_STATE_STABILIZING,
    TEST_STATE_MEASURING,
    TEST_STATE_ADVANCING,
    TEST_STATE_COMPLETED,
    TEST_STATE_ABORTED,
    TEST_STATE_ERROR
};

struct TestConfig {
    uint16_t initialThrottle = DEFAULT_INIT_THROTTLE;
    uint16_t endThrottle = DEFAULT_END_THROTTLE;
    uint16_t throttleStep = DEFAULT_THROTTLE_STEP;
    uint32_t stabilizationMs = DEFAULT_STABILIZATION_MS;
    uint32_t measurementMs = DEFAULT_MEASUREMENT_MS;
    uint8_t motorPoles = DEFAULT_MOTOR_POLES;

    bool validate(String &errorMsg) const;
};

struct LiveTestStatus {
    TestState state = TEST_STATE_IDLE;
    String stateString = "IDLE";
    uint16_t commandedThrottle = 0;
    uint16_t currentStep = 0;
    uint16_t totalSteps = 0;
    float progressPercent = 0.0f;
    uint32_t testElapsedMs = 0;
    uint32_t stepRemainingMs = 0;
    
    // Live sensor values
    float currentLoad = 0.0f;
    uint32_t currentRpm = 0;
    uint32_t currentErpm = 0;
    float currentAmps = 0.0f;
    float currentVolts = 0.0f;
    int16_t currentTemp = 0;

    String lastError = "";
};

class TestRunner {
public:
    TestRunner();
    ~TestRunner();

    bool begin();

    // Start automated sequence with specified configuration
    bool startTest(const TestConfig &cfg, String &errorMsg);

    // Immediate thread-safe test termination & motor stop
    void abortTest(const String &reason = "User aborted");

    // Live status snapshot query for Web UI
    LiveTestStatus getLiveStatus();

    TestConfig getConfig() const;
    TestState getState() const;
    bool isRunning() const;

private:
    static void _taskStub(void *param);
    void _taskLoop();

    void _runArmingSequence();
    void _runBaselineZero();
    void _runStep(uint16_t throttle);
    void _completeTest();
    void _handleError(const String &msg);

    SemaphoreHandle_t _mutex;
    TestConfig _config;
    TestState _state;
    String _lastError;

    uint32_t _testStartTimeMs;
    uint16_t _currentThrottle;
    uint16_t _currentStepIndex;
    uint16_t _totalStepCount;
    uint32_t _stepStartTimeMs;
    uint32_t _stepTargetDurationMs;

    TaskHandle_t _taskHandle;
    bool _taskRunning;
    bool _abortRequested;
};

extern TestRunner testRunner;
