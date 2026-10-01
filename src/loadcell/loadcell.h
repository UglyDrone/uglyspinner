#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "config.h"

struct LoadCellSample {
    float loadGrams = 0.0f;     // Calibrated thrust / weight in grams (or configured units)
    int32_t rawAdc = 0;         // Raw ADC count from load cell module
    uint32_t timestampMs = 0;   // Ingestion timestamp
};

// Abstract base class for load cell hardware interface
class LoadCellInterface {
public:
    virtual ~LoadCellInterface() = default;
    virtual bool begin() = 0;
    virtual bool getLatestSample(LoadCellSample &sample) = 0;
    virtual void tare() = 0;
    virtual void setCalibrationFactor(float factor) = 0;
    virtual float getCalibrationFactor() const = 0;
    virtual int32_t getZeroOffset() const = 0;
    virtual bool isHealthy() const = 0;
    virtual void getDebugInfo(uint32_t &byteCount, String &hexDump, String &asciiDump) = 0;
    virtual bool setBaud(uint32_t newBaud) { return false; }
    virtual void sendBytes(const uint8_t *data, size_t len) {}
    virtual uint32_t getBaud() const { return 9600; }
};

// Real Hardware Implementation for HX711 UART Bridge Modules
class UARTLoadCell : public LoadCellInterface {
public:
    UARTLoadCell(uint8_t uartNum = LOADCELL_UART_NUM, 
                 int rxPin = PIN_LOADCELL_RX, 
                 int txPin = PIN_LOADCELL_TX, 
                 uint32_t baud = LOADCELL_BAUD);
    virtual ~UARTLoadCell();

    bool begin() override;
    bool getLatestSample(LoadCellSample &sample) override;
    void tare() override;
    void setCalibrationFactor(float factor) override;
    float getCalibrationFactor() const override;
    int32_t getZeroOffset() const override;
    bool isHealthy() const override;
    void getDebugInfo(uint32_t &byteCount, String &hexDump, String &asciiDump) override;
    bool setBaud(uint32_t newBaud) override;
    void sendBytes(const uint8_t *data, size_t len) override;
    uint32_t getBaud() const override { return _baud; }

    // Pluggable frame parser placeholder
    // Returns true if a complete valid frame was decoded from data buffer
    bool parseFrame(const uint8_t *data, size_t len, int32_t &rawOut, float &loadOut, size_t &consumedBytes);

private:
    static void _taskStub(void *param);
    void _taskLoop();
    void _processIncomingBytes();

    uint8_t _uartNum;
    int _rxPin;
    int _txPin;
    uint32_t _baud;

    HardwareSerial* _serial;
    SemaphoreHandle_t _mutex;
    LoadCellSample _latestSample;

    int32_t _zeroOffset;
    float _calibrationFactor;
    uint32_t _lastSampleTimeMs;
    uint32_t _sampleCount;
    uint32_t _totalBytesReceived;

    static constexpr size_t ROLLING_HISTORY_SIZE = 48;
    uint8_t _rollingHistory[ROLLING_HISTORY_SIZE];
    size_t _rollingHistoryLen;
    uint32_t _lastLogTimeMs;

    static constexpr size_t RX_BUFFER_SIZE = 128;
    uint8_t _rxBuffer[RX_BUFFER_SIZE];
    size_t _rxLen;

    TaskHandle_t _taskHandle;
    bool _taskRunning;
};

// Mock Implementation for Desk/Bench Testing without physical load cell
class MockLoadCell : public LoadCellInterface {
public:
    MockLoadCell();
    virtual ~MockLoadCell();

    bool begin() override;
    bool getLatestSample(LoadCellSample &sample) override;
    void tare() override;
    void setCalibrationFactor(float factor) override;
    float getCalibrationFactor() const override;
    int32_t getZeroOffset() const override;
    bool isHealthy() const override;
    void getDebugInfo(uint32_t &byteCount, String &hexDump, String &asciiDump) override;
    uint32_t getBaud() const override { return 9600; }

private:
    static void _taskStub(void *param);
    void _taskLoop();

    SemaphoreHandle_t _mutex;
    LoadCellSample _latestSample;
    int32_t _zeroOffset;
    float _calibrationFactor;
    uint32_t _lastSampleTimeMs;

    TaskHandle_t _taskHandle;
    bool _taskRunning;
};

// Global active load cell instance
extern LoadCellInterface* activeLoadCell;
