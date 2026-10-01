#pragma once

#include <Arduino.h>
#include <driver/gpio.h>
#include <DShotRMT.h>

// =============================================================================
// FIRMWARE VERSION & METADATA
// =============================================================================
#define FIRMWARE_VERSION            "1.0.0"
#define SYSTEM_DEVICE_NAME          "ESP32 Motor/Propeller Test Stand"
#define SYSTEM_HOSTNAME             "uglyspinner"

// =============================================================================
// HARDWARE PIN ASSIGNMENTS (LILYGO TTGO T7 V1.3)
// =============================================================================
// DShot Motor Control Output (RMT Peripheral)
#define PIN_DSHOT_SIGNAL            GPIO_NUM_25

// AM32 / KISS ESC Telemetry Input (Hardware UART1 RX)
#define PIN_ESC_TELEMETRY_RX        GPIO_NUM_32
#define PIN_ESC_TELEMETRY_TX        GPIO_NUM_27 // Assign unused pin (GPIO 27) so UART1 doesn't default to GPIO 10 (SPI flash)

// HX711 UART Load Cell Interface (Hardware UART2)
// On TTGO T7 V1.3 right header: Pin 22 (GPIO22) and Pin 21 (GPIO21) provide conflict-free
// UART2 pins located in the same outer header row right above GND and 5V.
#define PIN_LOADCELL_RX             GPIO_NUM_22 // Connect to Load Cell Module TX
#define PIN_LOADCELL_TX             GPIO_NUM_21 // Connect to Load Cell Module RX

// =============================================================================
// DSHOT & MOTOR CONFIGURATION
// =============================================================================
// DShot mode: DSHOT600 is preferred for low latency (1.67µs bit duration).
// Can be changed to DSHOT300 if long wiring causes signal integrity issues.
#define DEFAULT_DSHOT_MODE          DSHOT300
#define DSHOT_ENABLE_BIDIRECTIONAL  false

static const char *const dshot_mode_name[] = {
    "DSHOT_OFF",
    "DSHOT150",
    "DSHOT300",
    "DSHOT600",
    "DSHOT1200"
};

// DShot Protocol Constants:
// 0           : STOP / Disarm / Failsafe
// 1..47       : Special commands (beeps, rotation direction, etc.)
// 48..2047    : Active throttle range (48 = min spin, 2047 = 100% full power)
#define DSHOT_THROTTLE_STOP         0
#define DSHOT_THROTTLE_MIN_RUN      48
#define DSHOT_THROTTLE_MAX_RUN      2047

// Motor Specifications
#define DEFAULT_MOTOR_POLES         14      // 14 poles = 7 pole pairs (standard 22xx/23xx/28xx motors)

// =============================================================================
// ESC TELEMETRY (AM32 / KISS)
// =============================================================================
#define ESC_TELEMETRY_BAUD          115200  // Standard KISS/AM32 AutoTelemetry baud rate
#define ESC_TELEMETRY_UART_NUM      1       // Hardware UART 1
#define ESC_TELEMETRY_TIMEOUT_MS    1000    // Telemetry freshness timeout

// =============================================================================
// LOAD CELL (HX711 UART MODULE)
// =============================================================================
#define LOADCELL_UART_NUM           2       // Hardware UART 2
#define LOADCELL_BAUD               9600    // Standard baud rate for UART weight bridges (9600 or 115200)
#define LOADCELL_TIMEOUT_MS         1500    // Load cell freshness timeout
#define LOADCELL_DEFAULT_CAL_FACTOR 1.0f    // Calibration factor: load = (raw - offset) * factor
#define LOADCELL_UNIT_STRING        "grams" // Unit of measurement

// =============================================================================
// WI-FI CONFIGURATION
// =============================================================================
// Update these to match your local Wi-Fi network:
#define WIFI_DEFAULT_SSID           "YourWiFiNetwork"
#define WIFI_DEFAULT_PASSWORD       "YourWiFiPassword"

// Fallback SoftAP mode if local Wi-Fi network cannot be reached within 10 seconds:
#define WIFI_AP_SSID                "UglySpinner-TestStand"
#define WIFI_AP_PASSWORD            "spinnertest"
#define WIFI_CONNECT_TIMEOUT_MS     10000

#define HTTP_SERVER_PORT            80

// =============================================================================
// TEST LIMITS & SAFETY WATCHDOG
// =============================================================================
#define SAFETY_ABORT_ON_LOST_TLM    false   // Abort test if telemetry is lost mid-run (set true for live high-power runs)
#define TEST_WATCHDOG_TIMEOUT_MS    1500    // Force throttle to 0 if task heartbeats stop
#define ARMING_DELAY_MS             2500    // Required zero-throttle period before starting test

// Test parameter validation ranges
#define TEST_MIN_THROTTLE_ALLOWED   48
#define TEST_MAX_THROTTLE_ALLOWED   2000    // Safety margin below absolute 2047
#define TEST_MIN_STEP_ALLOWED       5
#define TEST_MAX_STEP_ALLOWED       500
#define TEST_MIN_STABILIZE_MS       500
#define TEST_MAX_STABILIZE_MS       20000
#define TEST_MIN_MEASURE_MS         1000
#define TEST_MAX_MEASURE_MS         30000

// Default test parameter values
#define DEFAULT_INIT_THROTTLE       100
#define DEFAULT_END_THROTTLE        1200
#define DEFAULT_THROTTLE_STEP       50
#define DEFAULT_STABILIZATION_MS    2000
#define DEFAULT_MEASUREMENT_MS      3000

// Maximum stored raw samples in memory during a single test (500 * 32B = 16KB)
#define MAX_RAW_SAMPLES_BUFFER      500

// =============================================================================
// COMPILE-TIME MOCK MODES (FOR DESK/BENCH TESTING WITHOUT PROP/MOTOR)
// =============================================================================
// Set either to 1 to enable software simulation of hardware components.
// With both set to 1, the full web UI, live telemetry, interactive graph,
// test sequencing, and JSON export can be thoroughly tested on the ESP32.
#ifndef MOCK_ESC
#define MOCK_ESC                    0   // 0 = physical SEQURE 130A ESC (DShot on GPIO25, Telemetry RX on GPIO32)
#endif

#ifndef MOCK_LOADCELL
#define MOCK_LOADCELL               0   // 0 = physical HX711 UART on GPIO26(RX)/GPIO27(TX); 1 = simulated physics
#endif
