#include "loadcell.h"
#include "telemetry/esc_telemetry.h"
#include "motor/dshot_motor.h"
#include <esp_log.h>
#include <cmath>

static const char* TAG = "LoadCell";

// Instantiate the active load cell pointer based on compile configuration
#if MOCK_LOADCELL
static MockLoadCell mockLoadCellInstance;
LoadCellInterface* activeLoadCell = &mockLoadCellInstance;
#else
static UARTLoadCell uartLoadCellInstance(LOADCELL_UART_NUM, PIN_LOADCELL_RX, PIN_LOADCELL_TX, LOADCELL_BAUD);
LoadCellInterface* activeLoadCell = &uartLoadCellInstance;
#endif

// =============================================================================
// UART LOAD CELL IMPLEMENTATION
// =============================================================================

UARTLoadCell::UARTLoadCell(uint8_t uartNum, int rxPin, int txPin, uint32_t baud)
    : _uartNum(uartNum),
      _rxPin(rxPin),
      _txPin(txPin),
      _baud(baud),
      _serial(nullptr),
      _mutex(nullptr),
      _zeroOffset(0),
      _calibrationFactor(LOADCELL_DEFAULT_CAL_FACTOR),
      _lastSampleTimeMs(0),
      _sampleCount(0),
      _totalBytesReceived(0),
      _rollingHistoryLen(0),
      _lastLogTimeMs(0),
      _rxLen(0),
      _taskHandle(nullptr),
      _taskRunning(false) {
    memset(_rollingHistory, 0, sizeof(_rollingHistory));
}

UARTLoadCell::~UARTLoadCell() {
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

bool UARTLoadCell::begin() {
    ESP_LOGI(TAG, "Initializing UART Load Cell on UART%d (RX=%d, TX=%d, Baud=%u)...",
             _uartNum, _rxPin, _txPin, _baud);

    _mutex = xSemaphoreCreateMutex();
    if (!_mutex) {
        ESP_LOGE(TAG, "Failed to create load cell mutex!");
        return false;
    }

    _serial = new HardwareSerial(_uartNum);
    _serial->begin(_baud, SERIAL_8N1, _rxPin, _txPin);

    _taskRunning = true;
    BaseType_t res = xTaskCreatePinnedToCore(
        _taskStub,
        "LoadCellTask",
        4096,
        this,
        configMAX_PRIORITIES - 2, // High priority sensor ingestion
        &_taskHandle,
        1 // Core 1
    );

    if (res != pdPASS) {
        ESP_LOGE(TAG, "Failed to create load cell task!");
        return false;
    }

    ESP_LOGI(TAG, "UART Load Cell subsystem started.");
    return true;
}

bool UARTLoadCell::getLatestSample(LoadCellSample &sample) {
    if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        sample = _latestSample;
        xSemaphoreGive(_mutex);
        return true;
    }
    sample = _latestSample;
    return false;
}

void UARTLoadCell::tare() {
    ESP_LOGI(TAG, "Taring load cell: setting current raw reading as zero offset...");
    if (_mutex && xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE) {
        _zeroOffset = _latestSample.rawAdc;
        _latestSample.loadGrams = 0.0f;
        xSemaphoreGive(_mutex);
    }

    // If the physical UART module supports an active command for tare/zero, transmit it:
    if (_serial && _txPin >= 0) {
        // Send common ASCII tare commands: '=0\r\n', 'T\r\n', 'Z\r\n', and common hex tare 0xAA 0x01
        _serial->print("=0\r\n");
        _serial->print("T\r\n");
        _serial->print("Z\r\n");
        const uint8_t hexTare[] = {0xAA, 0x01, 0x00, 0x00, 0xAB};
        _serial->write(hexTare, sizeof(hexTare));
    }
}

void UARTLoadCell::setCalibrationFactor(float factor) {
    if (factor != 0.0f) {
        _calibrationFactor = factor;
        ESP_LOGI(TAG, "Load cell calibration factor set to: %f", factor);
    }
}

float UARTLoadCell::getCalibrationFactor() const {
    return _calibrationFactor;
}

int32_t UARTLoadCell::getZeroOffset() const {
    return _zeroOffset;
}

bool UARTLoadCell::isHealthy() const {
    return (millis() - _lastSampleTimeMs < LOADCELL_TIMEOUT_MS) && (_sampleCount > 0);
}

void UARTLoadCell::getDebugInfo(uint32_t &byteCount, String &hexDump, String &asciiDump) {
    if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        byteCount = _totalBytesReceived;
        hexDump = "";
        asciiDump = "";
        char hBuf[5];
        for (size_t i = 0; i < _rollingHistoryLen; i++) {
            snprintf(hBuf, sizeof(hBuf), "%02X ", _rollingHistory[i]);
            hexDump += hBuf;
            char c = (char)_rollingHistory[i];
            if (c >= 32 && c <= 126) {
                asciiDump += c;
            } else if (c == '\r') {
                asciiDump += "\\r";
            } else if (c == '\n') {
                asciiDump += "\\n ";
            } else {
                asciiDump += '.';
            }
        }
        xSemaphoreGive(_mutex);
    } else {
        byteCount = _totalBytesReceived;
        hexDump = "(busy)";
        asciiDump = "(busy)";
    }
}

bool UARTLoadCell::setBaud(uint32_t newBaud) {
    if (newBaud < 1200 || newBaud > 1000000) return false;
    _baud = newBaud;
    if (_serial) {
        _serial->begin(_baud, SERIAL_8N1, _rxPin, _txPin);
        ESP_LOGI(TAG, "Re-initialized Load Cell UART on pins RX=%d, TX=%d @ %u baud", _rxPin, _txPin, _baud);
    }
    return true;
}

void UARTLoadCell::sendBytes(const uint8_t *data, size_t len) {
    if (_serial && _txPin >= 0 && data && len > 0) {
        _serial->write(data, len);
        ESP_LOGI(TAG, "Transmitted %u bytes to load cell on TX GPIO%d", len, _txPin);
    }
}

/**
 * Robust Protocol Parser supporting:
 * 1. Binary 0xAA 0x55 (7-byte: [0xAA, 0x55, 4-byte raw ADC, checksum])
 * 2. Binary 0xAA 0x02 (6-byte: [0xAA, 0x02, 3-byte 24-bit raw ADC, checksum])
 * 3. Binary 0xAA [3-byte 24-bit raw ADC] [checksum] (5-byte)
 * 4. ASCII stream: "123.45\n", "W: 45.6 g\n", "ST,GS,+  12.3kg\n", "= 12.34 (g)\n"
 */
bool UARTLoadCell::parseFrame(const uint8_t *data, size_t len, int32_t &rawOut, float &loadOut, size_t &consumedBytes) {
    if (len < 1) return false;

    // --- 1. Binary Packet: 0xAA 0x55 (7 bytes) ---
    if (len >= 2 && data[0] == 0xAA && data[1] == 0x55) {
        if (len < 7) return false; // Need full packet
        int32_t raw = ((int32_t)data[2] << 24) |
                      ((int32_t)data[3] << 16) |
                      ((int32_t)data[4] << 8)  |
                      ((int32_t)data[5]);
        rawOut = raw;
        loadOut = (float)(rawOut - _zeroOffset) * _calibrationFactor;
        consumedBytes = 7;
        return true;
    }

    // --- 2. Binary Packet: 0xAA 0x02 (6 bytes, 24-bit ADC) ---
    if (len >= 2 && data[0] == 0xAA && data[1] == 0x02) {
        if (len < 6) return false;
        int32_t raw = ((int32_t)data[2] << 16) |
                      ((int32_t)data[3] << 8)  |
                      ((int32_t)data[4]);
        if (raw & 0x800000) raw |= 0xFF000000; // Sign-extend 24-bit
        rawOut = raw;
        loadOut = (float)(rawOut - _zeroOffset) * _calibrationFactor;
        consumedBytes = 6;
        return true;
    }

    // --- 3. Binary Packet: 0xAA (5 bytes, 24-bit ADC, e.g. 0xAA, D2, D1, D0, CS) ---
    if (data[0] == 0xAA && len >= 5 && data[1] != '\n' && data[1] != '\r') {
        int32_t raw = ((int32_t)data[1] << 16) |
                      ((int32_t)data[2] << 8)  |
                      ((int32_t)data[3]);
        if (raw & 0x800000) raw |= 0xFF000000;
        uint8_t cs = (data[1] + data[2] + data[3]) & 0xFF;
        if (data[4] == cs || data[4] == 0x55 || data[4] == 0xFF) {
            rawOut = raw;
            loadOut = (float)(rawOut - _zeroOffset) * _calibrationFactor;
            consumedBytes = 5;
            return true;
        }
    }

    // --- 4. ASCII Line delimited by '\n' or '\r' ---
    for (size_t i = 0; i < len; i++) {
        if (data[i] == '\n' || data[i] == '\r') {
            if (i == 0) {
                // Empty newline, skip it
                consumedBytes = 1;
                return false;
            }
            char lineBuf[64];
            size_t copyLen = (i < sizeof(lineBuf) - 1) ? i : (sizeof(lineBuf) - 1);
            memcpy(lineBuf, data, copyLen);
            lineBuf[copyLen] = '\0';

            char *p = lineBuf;
            while (*p && !isdigit((unsigned char)*p) && *p != '-' && *p != '+') {
                p++;
            }
            if (*p) {
                char *endP = nullptr;
                float val = strtof(p, &endP);
                if (endP != p) {
                    // Module reports in Kilograms (e.g. '=000.77' means 0.77 kg = 770 g)
                    float valGrams = val * 1000.0f;
                    rawOut = (int32_t)roundf(valGrams);
                    loadOut = (float)(rawOut - _zeroOffset) * _calibrationFactor;

                    // Skip consecutive \r\n if present
                    size_t next = i + 1;
                    if (next < len && ((data[i] == '\r' && data[next] == '\n') || (data[i] == '\n' && data[next] == '\r'))) {
                        next++;
                    }
                    consumedBytes = next;
                    return true;
                }
            }
            consumedBytes = i + 1;
            return false;
        }
    }

    // If buffer is filling up with no recognizable header or newline, discard the first byte
    if (len > 32) {
        consumedBytes = 1;
    }

    return false;
}

void UARTLoadCell::_taskStub(void *param) {
    UARTLoadCell *instance = static_cast<UARTLoadCell*>(param);
    instance->_taskLoop();
}

void UARTLoadCell::_taskLoop() {
    uint32_t lastConsoleReportMs = 0;

    while (_taskRunning) {
        if (_serial) {
            bool receivedAny = false;
            while (_serial->available()) {
                uint8_t b = _serial->read();
                receivedAny = true;

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

                if (_rxLen < RX_BUFFER_SIZE) {
                    _rxBuffer[_rxLen++] = b;
                } else {
                    // Buffer overrun, slide forward
                    memmove(_rxBuffer, _rxBuffer + 1, RX_BUFFER_SIZE - 1);
                    _rxBuffer[RX_BUFFER_SIZE - 1] = b;
                }
            }

            if (receivedAny) {
                _processIncomingBytes();
            }

            // Periodic console telemetry diagnostics
            uint32_t now = millis();
            if (now - lastConsoleReportMs >= 2000) {
                lastConsoleReportMs = now;
                if (_totalBytesReceived > 0) {
                    uint32_t count = 0;
                    String hexStr, ascStr;
                    getDebugInfo(count, hexStr, ascStr);
                    LoadCellSample sample;
                    getLatestSample(sample);
                    Serial.printf("[LoadCell RX] TotalBytes=%u, Load=%.2fg (raw=%d) | Hex:[ %s] Ascii:[ %s]\n",
                                  count, sample.loadGrams, sample.rawAdc, hexStr.c_str(), ascStr.c_str());
                } else {
                    Serial.printf("[LoadCell] Listening on GPIO26 (RX), GPIO27 (TX) @ %u baud (0 bytes received so far)...\n", _baud);
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    vTaskDelete(NULL);
}

void UARTLoadCell::_processIncomingBytes() {
    int32_t raw = 0;
    float load = 0.0f;
    size_t consumed = 0;

    while (_rxLen > 0) {
        consumed = 0;
        if (parseFrame(_rxBuffer, _rxLen, raw, load, consumed)) {
            // Successfully parsed frame
            if (_mutex && xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE) {
                _latestSample.rawAdc = raw;
                _latestSample.loadGrams = load;
                _latestSample.timestampMs = millis();
                _lastSampleTimeMs = _latestSample.timestampMs;
                _sampleCount++;
                xSemaphoreGive(_mutex);
            }

            if (consumed > 0 && consumed <= _rxLen) {
                memmove(_rxBuffer, _rxBuffer + consumed, _rxLen - consumed);
                _rxLen -= consumed;
            } else {
                _rxLen = 0;
            }
        } else if (consumed > 0) {
            // Discarded invalid bytes
            if (consumed <= _rxLen) {
                memmove(_rxBuffer, _rxBuffer + consumed, _rxLen - consumed);
                _rxLen -= consumed;
            } else {
                _rxLen = 0;
            }
        } else {
            // Incomplete frame, wait for more data
            break;
        }
    }
}

// =============================================================================
// MOCK LOAD CELL IMPLEMENTATION
// =============================================================================

MockLoadCell::MockLoadCell()
    : _mutex(nullptr),
      _zeroOffset(0),
      _calibrationFactor(1.0f),
      _lastSampleTimeMs(0),
      _taskHandle(nullptr),
      _taskRunning(false) {
}

MockLoadCell::~MockLoadCell() {
    _taskRunning = false;
    if (_taskHandle) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    if (_mutex) {
        vSemaphoreDelete(_mutex);
        _mutex = nullptr;
    }
}

bool MockLoadCell::begin() {
    ESP_LOGI(TAG, "Initializing MOCK Load Cell (Physics Simulation Mode)...");
    _mutex = xSemaphoreCreateMutex();
    if (!_mutex) {
        return false;
    }

    _taskRunning = true;
    BaseType_t res = xTaskCreatePinnedToCore(
        _taskStub,
        "MockLoadCellTask",
        4096,
        this,
        configMAX_PRIORITIES - 2,
        &_taskHandle,
        1
    );

    return (res == pdPASS);
}

bool MockLoadCell::getLatestSample(LoadCellSample &sample) {
    if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        sample = _latestSample;
        xSemaphoreGive(_mutex);
        return true;
    }
    sample = _latestSample;
    return false;
}

void MockLoadCell::tare() {
    if (_mutex && xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE) {
        _zeroOffset = _latestSample.rawAdc;
        _latestSample.loadGrams = 0.0f;
        xSemaphoreGive(_mutex);
    }
    ESP_LOGI(TAG, "MOCK Load Cell tared. Zero offset: %d", _zeroOffset);
}

void MockLoadCell::setCalibrationFactor(float factor) {
    if (factor != 0.0f) {
        _calibrationFactor = factor;
    }
}

float MockLoadCell::getCalibrationFactor() const {
    return _calibrationFactor;
}

int32_t MockLoadCell::getZeroOffset() const {
    return _zeroOffset;
}

bool MockLoadCell::isHealthy() const {
    return true; // Always healthy in mock mode
}

void MockLoadCell::getDebugInfo(uint32_t &byteCount, String &hexDump, String &asciiDump) {
    byteCount = 0;
    hexDump = "SIMULATED";
    asciiDump = "SIMULATED";
}

void MockLoadCell::_taskStub(void *param) {
    MockLoadCell *instance = static_cast<MockLoadCell*>(param);
    instance->_taskLoop();
}

void MockLoadCell::_taskLoop() {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(20); // 50 Hz acquisition rate

    while (_taskRunning) {
        uint16_t throttle = motorDriver.getThrottle();
        bool armed = motorDriver.isArmed();

        float simulatedThrustGrams = 0.0f;

        if (armed && throttle >= DSHOT_THROTTLE_MIN_RUN) {
            float norm = (float)(throttle - DSHOT_THROTTLE_MIN_RUN) / (float)(DSHOT_THROTTLE_MAX_RUN - DSHOT_THROTTLE_MIN_RUN);
            if (norm > 1.0f) norm = 1.0f;

            // Aerodynamic thrust equation: Thrust ~ K_t * RPM^2
            // At 100% throttle, typical high-performance drone motor/prop produces ~1800g to 2200g thrust
            float maxThrust = 2100.0f;
            float baseThrust = powf(norm, 2.05f) * maxThrust;

            // Motor vibration and aerodynamic turbulence noise (increases with throttle)
            float noiseAmplitude = 2.0f + (norm * 12.0f); // 2g at idle, up to 14g at max power
            float randomNoise = (((float)(esp_random() % 2001) - 1000.0f) / 1000.0f) * noiseAmplitude;

            simulatedThrustGrams = baseThrust + randomNoise;
            if (simulatedThrustGrams < 0.0f) simulatedThrustGrams = 0.0f;
        } else {
            // Zero throttle resting baseline noise (+/- 0.8 grams)
            simulatedThrustGrams = (((float)(esp_random() % 161) - 80.0f) / 100.0f);
        }

        int32_t rawSimAdc = (int32_t)(simulatedThrustGrams * 100.0f) + _zeroOffset;
        float calibratedLoad = (float)(rawSimAdc - _zeroOffset) * (0.01f * _calibrationFactor);

        if (_mutex && xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE) {
            _latestSample.rawAdc = rawSimAdc;
            _latestSample.loadGrams = calibratedLoad;
            _latestSample.timestampMs = millis();
            _lastSampleTimeMs = _latestSample.timestampMs;
            xSemaphoreGive(_mutex);
        }

        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }

    vTaskDelete(NULL);
}
