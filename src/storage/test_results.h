#pragma once

#include <Arduino.h>
#include <vector>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "config.h"

struct LoadStatistics {
    float mean = 0.0f;
    float min = 0.0f;
    float max = 0.0f;
    float stddev = 0.0f;
    uint32_t samples = 0;
};

struct TestPoint {
    uint16_t throttle = 0;
    LoadStatistics load;
    uint32_t rpm = 0;
    uint32_t erpm = 0;
    float current_a = 0.0f;
    float voltage_v = 0.0f;
    float temperature_c = 0.0f;
};

struct RawMeasurement {
    uint32_t timestamp_ms = 0;
    uint32_t elapsed_ms = 0;
    uint16_t throttle = 0;
    float load = 0.0f;
    uint32_t rpm = 0;
    uint32_t erpm = 0;
    float current_a = 0.0f;
    float voltage_v = 0.0f;
    float temperature_c = 0.0f;
};

struct TestMetadata {
    String firmware_version = FIRMWARE_VERSION;
    String dshot_mode = "DSHOT600";
    String telemetry_protocol = "KISS/AM32";
    uint32_t start_time_ms = 0;
    uint32_t run_duration_ms = 0;
    uint16_t initial_throttle = DEFAULT_INIT_THROTTLE;
    uint16_t end_throttle = DEFAULT_END_THROTTLE;
    uint16_t throttle_step = DEFAULT_THROTTLE_STEP;
    uint32_t stabilization_ms = DEFAULT_STABILIZATION_MS;
    uint32_t measurement_ms = DEFAULT_MEASUREMENT_MS;
    uint8_t motor_poles = DEFAULT_MOTOR_POLES;
    int32_t load_cell_zero_offset = 0;
    String load_units = LOADCELL_UNIT_STRING;
    String status = "IDLE";
    String abort_reason = "";
};

class TestResults {
public:
    TestResults();
    ~TestResults();

    void reset();
    void setMetadata(const TestMetadata &meta);
    TestMetadata getMetadata();

    void addPoint(const TestPoint &pt);
    void addRawSample(const RawMeasurement &sample);

    std::vector<TestPoint> getPoints();
    size_t getPointCount();
    size_t getRawSampleCount();

    // Serializes results into full JSON export format
    String exportJson(bool includeRawSamples = true);

    // Lightweight summary JSON for web dashboard
    String summaryJson();

private:
    SemaphoreHandle_t _mutex;
    TestMetadata _metadata;
    std::vector<TestPoint> _points;
    std::vector<RawMeasurement> _rawSamples;
};

// Global test results storage
extern TestResults testResults;
