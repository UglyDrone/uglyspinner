#include "esc_telemetry.h"
#include "motor/dshot_motor.h"
#include <esp_log.h>

static const char* TAG = "EscTelemetry";

EscTelemetry escTelemetry;

EscTelemetry::EscTelemetry()
    : _motorPoles(DEFAULT_MOTOR_POLES),
      _serial(nullptr),
      _mutex(nullptr),
      _inverted(false),
      _baud(ESC_TELEMETRY_BAUD),
      _pullup(false),
      _totalBytesReceived(0),
      _rollingHistoryLen(0),
      _rxIndex(0),
      _taskHandle(nullptr),
      _taskRunning(false),
      _simulatedConsumptionMah(0.0f),
      _simulatedTempC(28.0f) {
    memset(_rollingHistory, 0, sizeof(_rollingHistory));
}

EscTelemetry::~EscTelemetry() {
    _taskRunning = false;
    if (_taskHandle) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    if (_serial) {
        _serial->end();
        delete _serial;
        _serial = nullptr;
    }
    if (_mutex) {
        vSemaphoreDelete(_mutex);
        _mutex = nullptr;
    }
}

uint8_t EscTelemetry::updateCrc8(uint8_t crc, uint8_t crc_seed) {
    uint8_t crc_u = crc ^ crc_seed;
    for (uint8_t i = 0; i < 8; i++) {
        crc_u = (crc_u & 0x80) ? (0x07 ^ (crc_u << 1)) : (crc_u << 1);
    }
    return crc_u;
}

uint8_t EscTelemetry::calculateCrc8(const uint8_t *buf, size_t len) {
    uint8_t crc = 0;
    for (size_t i = 0; i < len; i++) {
        crc = updateCrc8(buf[i], crc);
    }
    return crc;
}

bool EscTelemetry::begin() {
    _mutex = xSemaphoreCreateMutex();
    if (!_mutex) {
        ESP_LOGE(TAG, "Failed to create telemetry mutex!");
        return false;
    }

#if !MOCK_ESC
    ESP_LOGI(TAG, "Initializing KISS/AM32 ESC Telemetry on UART%d RX=GPIO%d (Baud: %d)...",
             ESC_TELEMETRY_UART_NUM, PIN_ESC_TELEMETRY_RX, ESC_TELEMETRY_BAUD);

    _serial = new HardwareSerial(ESC_TELEMETRY_UART_NUM);
    _serial->begin(ESC_TELEMETRY_BAUD, SERIAL_8N1, PIN_ESC_TELEMETRY_RX, PIN_ESC_TELEMETRY_TX);
    setPullup(_pullup);
#else
    ESP_LOGI(TAG, "MOCK_ESC mode active: hardware ESC Telemetry UART bypassed; simulation active.");
#endif

    _taskRunning = true;
    BaseType_t res = xTaskCreatePinnedToCore(
        _taskStub,
        "EscTelemetryTask",
        4096,
        this,
        configMAX_PRIORITIES - 2, // High priority telemetry ingestion
        &_taskHandle,
        1 // Core 1
    );

    if (res != pdPASS) {
        ESP_LOGE(TAG, "Failed to create ESC telemetry task!");
        return false;
    }

    ESP_LOGI(TAG, "ESC telemetry subsystem initialized.");
    return true;
}

void EscTelemetry::setMotorPoles(uint8_t poles) {
    if (poles >= 2 && poles <= 64) {
        xSemaphoreTake(_mutex, portMAX_DELAY);
        _motorPoles = poles;
        xSemaphoreGive(_mutex);
        ESP_LOGI(TAG, "Motor pole count updated to: %u", poles);
    }
}

uint8_t EscTelemetry::getMotorPoles() const {
    return _motorPoles;
}

EscTelemetryData EscTelemetry::getSnapshot() {
    EscTelemetryData snapshot;
    if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        snapshot = _data;
        snapshot.is_healthy = (millis() - snapshot.last_packet_time_ms < ESC_TELEMETRY_TIMEOUT_MS) &&
                             (snapshot.total_packets > 0);
        xSemaphoreGive(_mutex);
    } else {
        snapshot = _data; // Fallback read
    }
    return snapshot;
}

bool EscTelemetry::isHealthy() {
    EscTelemetryData snap = getSnapshot();
    return snap.is_healthy;
}

void EscTelemetry::updateFromBidirectionalDShot(uint16_t erpm, uint16_t rpm, bool hasFullData, const dshot_telemetry_data_t &data) {
    if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        _data.erpm = erpm;
        _data.rpm = rpm;
        _data.last_packet_time_ms = millis();
        _data.total_packets++;
        _totalBytesReceived += hasFullData ? 10 : 2;

        if (hasFullData) {
            _data.voltage_v = data.voltage / 1000.0f;
            _data.current_a = data.current / 1000.0f;
            _data.temperature_c = data.temperature;
            _data.consumption_mah = data.consumption;
        }
        xSemaphoreGive(_mutex);
    }
}

void EscTelemetry::getDebugInfo(uint32_t &byteCount, uint32_t &totalPackets, uint32_t &crcErrors, String &hexDump) {
    if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        byteCount = _totalBytesReceived;
        totalPackets = _data.total_packets;
        crcErrors = _data.crc_errors;
        hexDump = "";
        char hBuf[5];
        for (size_t i = 0; i < _rollingHistoryLen; i++) {
            snprintf(hBuf, sizeof(hBuf), "%02X ", _rollingHistory[i]);
            hexDump += hBuf;
        }
        xSemaphoreGive(_mutex);
    } else {
        byteCount = _totalBytesReceived;
        totalPackets = _data.total_packets;
        crcErrors = _data.crc_errors;
        hexDump = "(busy)";
    }
}

bool EscTelemetry::setInvert(bool invert) {
    _inverted = invert;
    if (_serial) {
        _serial->begin(_baud, SERIAL_8N1, PIN_ESC_TELEMETRY_RX, PIN_ESC_TELEMETRY_TX, _inverted);
        setPullup(_pullup);
        ESP_LOGI(TAG, "Reconfigured ESC Telemetry UART: baud=%u, inverted=%s", _baud, _inverted ? "YES" : "NO");
    }
    return true;
}

bool EscTelemetry::setBaud(uint32_t baud) {
    if (baud < 1200 || baud > 1000000) return false;
    _baud = baud;
    if (_serial) {
        _serial->begin(_baud, SERIAL_8N1, PIN_ESC_TELEMETRY_RX, PIN_ESC_TELEMETRY_TX, _inverted);
        setPullup(_pullup);
        ESP_LOGI(TAG, "Reconfigured ESC Telemetry UART: baud=%u, inverted=%s", _baud, _inverted ? "YES" : "NO");
    }
    return true;
}

bool EscTelemetry::setPullup(bool enable) {
    _pullup = enable;
    if (_pullup) {
        gpio_set_pull_mode((gpio_num_t)PIN_ESC_TELEMETRY_RX, GPIO_PULLUP_ONLY);
        ESP_LOGI(TAG, "Internal pull-up ENABLED on GPIO%d", PIN_ESC_TELEMETRY_RX);
    } else {
        gpio_set_pull_mode((gpio_num_t)PIN_ESC_TELEMETRY_RX, GPIO_FLOATING);
        ESP_LOGI(TAG, "Internal pull-up DISABLED (floating) on GPIO%d", PIN_ESC_TELEMETRY_RX);
    }
    return true;
}

void EscTelemetry::_taskStub(void *param) {
    EscTelemetry *instance = static_cast<EscTelemetry*>(param);
    instance->_taskLoop();
}

void EscTelemetry::_taskLoop() {
    TickType_t xLastSimTime = xTaskGetTickCount();
    uint32_t lastLogTimeMs = 0;

    while (_taskRunning) {
#if !MOCK_ESC
        if (_serial) {
            while (_serial->available()) {
                uint8_t b = _serial->read();
                if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                    _totalBytesReceived++;
                    if (_rollingHistoryLen < ROLLING_HISTORY_SIZE) {
                        _rollingHistory[_rollingHistoryLen++] = b;
                    } else {
                        memmove(_rollingHistory, _rollingHistory + 1, ROLLING_HISTORY_SIZE - 1);
                        _rollingHistory[ROLLING_HISTORY_SIZE - 1] = b;
                    }
                    xSemaphoreGive(_mutex);
                } else {
                    _totalBytesReceived++;
                }
                _processByte(b);
            }

            uint32_t now = millis();
            if (now - lastLogTimeMs >= 2000) {
                lastLogTimeMs = now;
                if (_totalBytesReceived > 0) {
                    uint32_t count = 0, pkts = 0, crcs = 0;
                    String hexStr;
                    getDebugInfo(count, pkts, crcs, hexStr);
                    EscTelemetryData snap = getSnapshot();
                    Serial.printf("[EscTelemetry RX] TotalBytes=%u, Pkts=%u, CRC_Err=%u | Volt=%.2fV, Curr=%.2fA, Temp=%dC, eRPM=%u | Hex:[ %s]\n",
                                  count, pkts, crcs, snap.voltage_v, snap.current_a, snap.temperature_c, snap.erpm, hexStr.c_str());
                } else {
                    Serial.printf("[EscTelemetry] Listening on GPIO32 (RX) @ %d baud (0 bytes received so far)...\n", ESC_TELEMETRY_BAUD);
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(5)); // Read frequently to avoid UART FIFO overflow
#else
        // Mock simulation updates at 50 Hz (20ms interval)
        _simulateTelemetry();
        vTaskDelayUntil(&xLastSimTime, pdMS_TO_TICKS(20));
#endif
    }

    vTaskDelete(NULL);
}

void EscTelemetry::_processByte(uint8_t b) {
    if (_rxIndex < BUFFER_SIZE) {
        _rxBuffer[_rxIndex++] = b;
    } else {
        // Buffer full without match, shift out one byte
        memmove(_rxBuffer, _rxBuffer + 1, BUFFER_SIZE - 1);
        _rxBuffer[BUFFER_SIZE - 1] = b;
    }

    // KISS ESC packet is exactly 10 bytes:
    // [0]: Temp (°C)
    // [1..2]: Voltage (0.01V)
    // [3..4]: Current (0.01A)
    // [5..6]: Consumption (mAh)
    // [7..8]: eRPM (100 eRPM)
    // [9]: CRC8
    while (_rxIndex >= 10) {
        uint8_t calculatedCrc = calculateCrc8(_rxBuffer, 9);
        uint8_t expectedCrc = _rxBuffer[9];

        // Sanity check: All zeros produces CRC 0, but is invalid telemetry on a live ESC
        bool allZero = true;
        for (int i = 0; i < 9; i++) {
            if (_rxBuffer[i] != 0) { allZero = false; break; }
        }

        if (calculatedCrc == expectedCrc && !allZero) {
            // Valid packet found
            _parsePacket(_rxBuffer);
            // Consume 10 bytes
            size_t remaining = _rxIndex - 10;
            if (remaining > 0) {
                memmove(_rxBuffer, _rxBuffer + 10, remaining);
            }
            _rxIndex = remaining;
        } else {
            // CRC mismatch: slide forward by 1 byte to find alignment
            xSemaphoreTake(_mutex, portMAX_DELAY);
            _data.crc_errors++;
            xSemaphoreGive(_mutex);

            memmove(_rxBuffer, _rxBuffer + 1, _rxIndex - 1);
            _rxIndex--;
        }
    }
}

void EscTelemetry::_parsePacket(const uint8_t *pkt) {
    int16_t temp = (int8_t)pkt[0];
    uint16_t rawVolt = ((uint16_t)pkt[1] << 8) | pkt[2];
    uint16_t rawCurr = ((uint16_t)pkt[3] << 8) | pkt[4];
    uint16_t rawCons = ((uint16_t)pkt[5] << 8) | pkt[6];
    uint16_t rawErpm = ((uint16_t)pkt[7] << 8) | pkt[8];

    float volt = rawVolt / 100.0f;
    float curr = rawCurr / 100.0f;
    uint32_t erpm = (uint32_t)rawErpm * 100;
    
    // Mechanical RPM calculation:
    // eRPM = RPM * (Poles / 2) -> RPM = (eRPM * 2) / Poles
    uint32_t rpm = 0;
    if (_motorPoles > 0) {
        rpm = (erpm * 2) / _motorPoles;
    }

    xSemaphoreTake(_mutex, portMAX_DELAY);
    _data.erpm = erpm;
    _data.rpm = rpm;
    _data.voltage_v = volt;
    _data.current_a = curr;
    _data.temperature_c = temp;
    _data.consumption_mah = rawCons;
    _data.last_packet_time_ms = millis();
    _data.total_packets++;
    xSemaphoreGive(_mutex);
}

void EscTelemetry::_simulateTelemetry() {
    uint16_t throttle = motorDriver.getThrottle();
    bool armed = motorDriver.isArmed();

    uint32_t targetErpm = 0;
    uint32_t targetRpm = 0;
    float targetCurr = 0.35f; // Quiescent idle current
    float targetVolt = 25.20f; // Nominal 6S LiPo

    if (armed && throttle >= DSHOT_THROTTLE_MIN_RUN) {
        float norm = (float)(throttle - DSHOT_THROTTLE_MIN_RUN) / (float)(DSHOT_THROTTLE_MAX_RUN - DSHOT_THROTTLE_MIN_RUN);
        if (norm > 1.0f) norm = 1.0f;

        // Model realistic mechanical RPM curve: up to ~22,000 RPM at full throttle
        float baseRpm = norm * 22000.0f;
        // Add random vibration noise (+/- 40 RPM)
        float noiseRpm = ((float)(esp_random() % 81)) - 40.0f;
        float calculatedRpm = baseRpm + noiseRpm;
        if (calculatedRpm < 0) calculatedRpm = 0;

        targetRpm = (uint32_t)calculatedRpm;
        targetErpm = targetRpm * (_motorPoles / 2);

        // Current follows power law I ~ I_0 + k * throttle^2.3 (aerodynamic load)
        float baseCurr = 0.5f + powf(norm, 2.3f) * 55.0f;
        float noiseCurr = (((float)(esp_random() % 101)) - 50.0f) / 500.0f; // +/- 0.1A
        targetCurr = baseCurr + noiseCurr;

        // Battery sag: V = V_rest - (I * R_int)
        float batteryResistance = 0.035f; // 35 mOhm pack
        float noiseVolt = (((float)(esp_random() % 101)) - 50.0f) / 1000.0f;
        targetVolt = 25.20f - (targetCurr * batteryResistance) + noiseVolt;

        // Heating model: slight gradual rise with high current
        _simulatedTempC += (targetCurr * 0.005f) - ((_simulatedTempC - 28.0f) * 0.002f);
    } else {
        // Natural cool-down towards 28°C
        _simulatedTempC -= (_simulatedTempC - 28.0f) * 0.01f;
    }

    // Accumulate mAh: 20ms delta -> (Current_A * 1000 mA * (20/3600000) h)
    _simulatedConsumptionMah += (targetCurr * 1000.0f * (20.0f / 3600000.0f));

    xSemaphoreTake(_mutex, portMAX_DELAY);
    _data.erpm = targetErpm;
    _data.rpm = targetRpm;
    _data.voltage_v = targetVolt;
    _data.current_a = targetCurr;
    _data.temperature_c = (int16_t)(_simulatedTempC + 0.5f);
    _data.consumption_mah = (uint32_t)_simulatedConsumptionMah;
    _data.last_packet_time_ms = millis();
    _data.total_packets++;
    xSemaphoreGive(_mutex);
}
