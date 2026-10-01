#pragma once

#include <Arduino.h>
#include <atomic>
#include <DShotRMT.h>
#include "config.h"

class DShotMotor {
public:
    DShotMotor();
    ~DShotMotor();

    // Initializes RMT peripheral and establishes initial 0-throttle safety state
    bool begin();

    // Arms the motor driver (requires zero throttle state)
    bool arm();

    // Disarms the motor and forces throttle to 0
    void disarm();

    // Sets active throttle (48..2047, or 0 for stop)
    // Rejects out-of-range values and enforces safety clamps
    bool setThrottle(uint16_t throttle);

    // Immediate stop (throttle = 0)
    void stop();

    // Emergency Abort (locks motor out until re-armed)
    void emergencyStop();

    // Watchdog feeding mechanism
    void feedWatchdog();

    // State queries
    uint16_t getThrottle() const;
    bool isArmed() const;
    bool isEmergencyStopped() const;
    dshot_mode_t getDShotMode() const;
    bool setDShotMode(dshot_mode_t mode);
    bool setBidirectional(bool bidi);
    bool isBidirectional() const;
    void getTxStats(uint32_t &frames, uint32_t &fails, uint16_t &lastErr, uint16_t &lastRaw) const {
        frames = _txFrameCount.load();
        fails = _txFailCount.load();
        lastErr = _txLastErrCode.load();
        lastRaw = _txLastRawValue.load();
    }

private:
    void _sendZeroThrottleFrame();
    bool _reinitDriver();
    static void _motorTaskStub(void *param);
    void _motorTaskLoop();

    DShotRMT* _dshotDriver;
    dshot_mode_t _mode;
    std::atomic<bool> _bidirectional;
    
    std::atomic<uint16_t> _commandedThrottle;
    std::atomic<bool> _armed;
    std::atomic<bool> _emergencyStopTriggered;
    std::atomic<uint32_t> _lastWatchdogFeedMs;
    std::atomic<bool> _taskRunning;

    std::atomic<uint32_t> _txFrameCount{0};
    std::atomic<uint32_t> _txFailCount{0};
    std::atomic<uint16_t> _txLastErrCode{0};
    std::atomic<uint16_t> _txLastRawValue{0};

    TaskHandle_t _taskHandle;
};

// Global motor instance declaration
extern DShotMotor motorDriver;
