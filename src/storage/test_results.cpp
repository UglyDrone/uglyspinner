#include "test_results.h"
#include <esp_log.h>

static const char* TAG = "TestResults";

TestResults testResults;

TestResults::TestResults() {
    _mutex = xSemaphoreCreateMutex();
}

TestResults::~TestResults() {
    if (_mutex) {
        vSemaphoreDelete(_mutex);
        _mutex = nullptr;
    }
}

void TestResults::reset() {
    if (_mutex && xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE) {
        _points.clear();
        _points.reserve(64);
        _rawSamples.clear();
        _rawSamples.reserve(MAX_RAW_SAMPLES_BUFFER);
        _metadata.status = "IDLE";
        _metadata.abort_reason = "";
        _metadata.start_time_ms = 0;
        _metadata.run_duration_ms = 0;
        xSemaphoreGive(_mutex);
    }
}

void TestResults::setMetadata(const TestMetadata &meta) {
    if (_mutex && xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE) {
        _metadata = meta;
        xSemaphoreGive(_mutex);
    }
}

TestMetadata TestResults::getMetadata() {
    TestMetadata copy;
    if (_mutex && xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE) {
        copy = _metadata;
        xSemaphoreGive(_mutex);
    }
    return copy;
}

void TestResults::addPoint(const TestPoint &pt) {
    if (_mutex && xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE) {
        _points.push_back(pt);
        xSemaphoreGive(_mutex);
    }
}

void TestResults::addRawSample(const RawMeasurement &sample) {
    if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
        // Only insert if within pre-reserved capacity (never reallocate on the fly)
        if (_rawSamples.size() < MAX_RAW_SAMPLES_BUFFER && _rawSamples.size() < _rawSamples.capacity()) {
            _rawSamples.push_back(sample);
        }
        xSemaphoreGive(_mutex);
    }
}

std::vector<TestPoint> TestResults::getPoints() {
    std::vector<TestPoint> copy;
    if (_mutex && xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE) {
        copy = _points;
        xSemaphoreGive(_mutex);
    }
    return copy;
}

size_t TestResults::getPointCount() {
    size_t count = 0;
    if (_mutex && xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE) {
        count = _points.size();
        xSemaphoreGive(_mutex);
    }
    return count;
}

size_t TestResults::getRawSampleCount() {
    size_t count = 0;
    if (_mutex && xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE) {
        count = _rawSamples.size();
        xSemaphoreGive(_mutex);
    }
    return count;
}

String TestResults::summaryJson() {
    String json;
    json.reserve(2048);

    if (_mutex && xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE) {
        json += "{\"metadata\":{";
        json += "\"firmware_version\":\"" + _metadata.firmware_version + "\",";
        json += "\"dshot_mode\":\"" + _metadata.dshot_mode + "\",";
        json += "\"telemetry_protocol\":\"" + _metadata.telemetry_protocol + "\",";
        json += "\"start_time_ms\":" + String(_metadata.start_time_ms) + ",";
        json += "\"run_duration_ms\":" + String(_metadata.run_duration_ms) + ",";
        json += "\"initial_throttle\":" + String(_metadata.initial_throttle) + ",";
        json += "\"end_throttle\":" + String(_metadata.end_throttle) + ",";
        json += "\"throttle_step\":" + String(_metadata.throttle_step) + ",";
        json += "\"stabilization_ms\":" + String(_metadata.stabilization_ms) + ",";
        json += "\"measurement_ms\":" + String(_metadata.measurement_ms) + ",";
        json += "\"motor_poles\":" + String(_metadata.motor_poles) + ",";
        json += "\"load_cell_zero_offset\":" + String(_metadata.load_cell_zero_offset) + ",";
        json += "\"load_units\":\"" + _metadata.load_units + "\",";
        json += "\"status\":\"" + _metadata.status + "\",";
        json += "\"abort_reason\":\"" + _metadata.abort_reason + "\"";
        json += "},\"points\":[";

        for (size_t i = 0; i < _points.size(); i++) {
            const auto &pt = _points[i];
            if (i > 0) json += ",";
            json += "{\"throttle\":" + String(pt.throttle) + ",";
            json += "\"load\":{";
            json += "\"mean\":" + String(pt.load.mean, 2) + ",";
            json += "\"min\":" + String(pt.load.min, 2) + ",";
            json += "\"max\":" + String(pt.load.max, 2) + ",";
            json += "\"stddev\":" + String(pt.load.stddev, 3) + ",";
            json += "\"samples\":" + String(pt.load.samples);
            json += "},";
            json += "\"rpm\":" + String(pt.rpm) + ",";
            json += "\"erpm\":" + String(pt.erpm) + ",";
            json += "\"current_a\":" + String(pt.current_a, 2) + ",";
            json += "\"voltage_v\":" + String(pt.voltage_v, 2) + ",";
            json += "\"temperature_c\":" + String(pt.temperature_c, 1);
            json += "}";
        }
        json += "]}";

        xSemaphoreGive(_mutex);
    }

    return json;
}

String TestResults::exportJson(bool includeRawSamples) {
    String json;
    json.reserve(includeRawSamples ? 32768 : 4096);

    if (_mutex && xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE) {
        json += "{\n  \"metadata\": {\n";
        json += "    \"firmware_version\": \"" + _metadata.firmware_version + "\",\n";
        json += "    \"dshot_mode\": \"" + _metadata.dshot_mode + "\",\n";
        json += "    \"telemetry_protocol\": \"" + _metadata.telemetry_protocol + "\",\n";
        json += "    \"start_time_ms\": " + String(_metadata.start_time_ms) + ",\n";
        json += "    \"run_duration_ms\": " + String(_metadata.run_duration_ms) + ",\n";
        json += "    \"initial_throttle\": " + String(_metadata.initial_throttle) + ",\n";
        json += "    \"end_throttle\": " + String(_metadata.end_throttle) + ",\n";
        json += "    \"throttle_step\": " + String(_metadata.throttle_step) + ",\n";
        json += "    \"stabilization_ms\": " + String(_metadata.stabilization_ms) + ",\n";
        json += "    \"measurement_ms\": " + String(_metadata.measurement_ms) + ",\n";
        json += "    \"motor_poles\": " + String(_metadata.motor_poles) + ",\n";
        json += "    \"load_cell_zero_offset\": " + String(_metadata.load_cell_zero_offset) + ",\n";
        json += "    \"load_units\": \"" + _metadata.load_units + "\",\n";
        json += "    \"status\": \"" + _metadata.status + "\",\n";
        json += "    \"abort_reason\": \"" + _metadata.abort_reason + "\"\n";
        json += "  },\n  \"points\": [\n";

        for (size_t i = 0; i < _points.size(); i++) {
            const auto &pt = _points[i];
            json += "    {\n";
            json += "      \"throttle\": " + String(pt.throttle) + ",\n";
            json += "      \"load\": {\n";
            json += "        \"mean\": " + String(pt.load.mean, 3) + ",\n";
            json += "        \"min\": " + String(pt.load.min, 3) + ",\n";
            json += "        \"max\": " + String(pt.load.max, 3) + ",\n";
            json += "        \"stddev\": " + String(pt.load.stddev, 4) + ",\n";
            json += "        \"samples\": " + String(pt.load.samples) + "\n";
            json += "      },\n";
            json += "      \"rpm\": " + String(pt.rpm) + ",\n";
            json += "      \"erpm\": " + String(pt.erpm) + ",\n";
            json += "      \"current_a\": " + String(pt.current_a, 2) + ",\n";
            json += "      \"voltage_v\": " + String(pt.voltage_v, 2) + ",\n";
            json += "      \"temperature_c\": " + String(pt.temperature_c, 1) + "\n";
            json += "    }";
            if (i + 1 < _points.size()) json += ",";
            json += "\n";
        }
        json += "  ]";

        if (includeRawSamples && !_rawSamples.empty()) {
            json += ",\n  \"raw_samples\": [\n";
            for (size_t i = 0; i < _rawSamples.size(); i++) {
                const auto &s = _rawSamples[i];
                json += "    {\"t_ms\":" + String(s.timestamp_ms) +
                        ",\"el_ms\":" + String(s.elapsed_ms) +
                        ",\"thr\":" + String(s.throttle) +
                        ",\"load\":" + String(s.load, 2) +
                        ",\"rpm\":" + String(s.rpm) +
                        ",\"erpm\":" + String(s.erpm) +
                        ",\"curr\":" + String(s.current_a, 2) +
                        ",\"volt\":" + String(s.voltage_v, 2) +
                        ",\"temp\":" + String(s.temperature_c, 1) + "}";
                if (i + 1 < _rawSamples.size()) json += ",";
                json += "\n";
            }
            json += "  ]\n";
        } else {
            json += "\n";
        }

        json += "}\n";
        xSemaphoreGive(_mutex);
    }

    return json;
}
