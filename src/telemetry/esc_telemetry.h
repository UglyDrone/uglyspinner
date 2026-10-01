#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "config.h"

// Telemetry snapshot container
struct EscTelemetryData {
    uint32_t erpm = 0;              // Raw electrical RPM reported by ESC
    uint32_t rpm = 0;               // Calculated mechanical shaft RPM
    float current_a = 0.0f;         // ESC phase current in Amps
    float voltage_v = 0.0f;         // Battery voltage in Volts
    int16_t temperature_c = 0;      // ESC temperature in °C
    uint32_t consumption_mah = 0;   // Accumulated consumption in mAh
    uint32_t last_packet_time_ms = 0;
    uint32_t total_packets = 0;
    uint32_t crc_errors = 0;
    bool is_healthy = false;
};

class EscTelemetry {
public:
    EscTelemetry();
    ~EscTelemetry();

    bool begin();
    void setMotorPoles(uint8_t poles);
    uint8_t getMotorPoles() const;

    // Retrieve atomic/mutex-protected telemetry snapshot
    EscTelemetryData getSnapshot();
    
    // Check if telemetry frames are fresh and valid
    bool isHealthy();

    // Ingest telemetry received via Bidirectional DShot (BDShot / EDT)
    void updateFromBidirectionalDShot(uint16_t erpm, uint16_t rpm, bool hasFullData, const dshot_telemetry_data_t &data);

    // Debugging and diagnostic inspection
    void getDebugInfo(uint32_t &byteCount, uint32_t &totalPackets, uint32_t &crcErrors, String &hexDump);
    bool setInvert(bool invert);
    bool setBaud(uint32_t baud);
    bool setPullup(bool enable);
    bool isInverted() const { return _inverted; }
    uint32_t getBaud() const { return _baud; }
    bool isPullup() const { return _pullup; }

    // Helper functions for CRC8 verification
    static uint8_t updateCrc8(uint8_t crc, uint8_t crc_seed);
    static uint8_t calculateCrc8(const uint8_t *buf, size_t len);

private:
    static void _taskStub(void *param);
    void _taskLoop();
    void _processByte(uint8_t b);
    void _parsePacket(const uint8_t *pkt);
    void _simulateTelemetry();

    uint8_t _motorPoles;
    HardwareSerial* _serial;
    SemaphoreHandle_t _mutex;
    EscTelemetryData _data;
    bool _inverted;
    uint32_t _baud;
    bool _pullup;

    uint32_t _totalBytesReceived;
    static constexpr size_t ROLLING_HISTORY_SIZE = 48;
    uint8_t _rollingHistory[ROLLING_HISTORY_SIZE];
    size_t _rollingHistoryLen;

    // Sliding circular buffer for frame synchronization
    static constexpr size_t BUFFER_SIZE = 64;
    uint8_t _rxBuffer[BUFFER_SIZE];
    size_t _rxIndex;

    TaskHandle_t _taskHandle;
    bool _taskRunning;

    // Simulation tracking
    float _simulatedConsumptionMah;
    float _simulatedTempC;
};

// Global ESC telemetry instance declaration
extern EscTelemetry escTelemetry;
