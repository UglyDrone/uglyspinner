#include <Arduino.h>
#include <WiFi.h>
#include "config.h"
#include "motor/dshot_motor.h"
#include "telemetry/esc_telemetry.h"
#include "loadcell/loadcell.h"
#include "test/test_runner.h"
#include "web/web_server.h"
#include <esp_log.h>

static const char* TAG = "Main";

void printBanner() {
    Serial.println("\n=============================================================");
    Serial.println("  ESP32 AUTOMATED MOTOR & PROPELLER TEST STAND");
    Serial.println("  Hardware: LILYGO TTGO T7 V1.3 Mini32 (ESP32)");
    Serial.println("  Firmware Version: " FIRMWARE_VERSION);
    Serial.println("=============================================================");
}

void initWiFi() {
    Serial.println("\n[Wi-Fi] Initializing network subsystem...");
    
    // Safety requirement: Motor defaults to STOP during Wi-Fi initialization
    motorDriver.stop();

    WiFi.setHostname(SYSTEM_HOSTNAME);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_DEFAULT_SSID, WIFI_DEFAULT_PASSWORD);

    Serial.printf("[Wi-Fi] Connecting to SSID: '%s'", WIFI_DEFAULT_SSID);

    uint32_t startAttempt = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < WIFI_CONNECT_TIMEOUT_MS) {
        delay(400);
        Serial.print(".");
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\n[Wi-Fi] Connection established successfully!");
        Serial.printf("[Wi-Fi] IP Address:    http://%s/\n", WiFi.localIP().toString().c_str());
        Serial.printf("[Wi-Fi] Subnet Mask:   %s\n", WiFi.subnetMask().toString().c_str());
        Serial.printf("[Wi-Fi] Gateway:       %s\n", WiFi.gatewayIP().toString().c_str());
        Serial.printf("[Wi-Fi] Signal (RSSI): %d dBm\n", WiFi.RSSI());
    } else {
        Serial.println("\n[Wi-Fi] Connection timed out (Reason 201: NO_AP_FOUND).");
        Serial.println("[Wi-Fi] Scanning visible 2.4GHz Wi-Fi networks in range...");
        int n = WiFi.scanNetworks();
        if (n == 0) {
            Serial.println("  [Wi-Fi] No 2.4GHz networks found. Check antenna if your board has an external IPEX connector.");
        } else {
            Serial.printf("  [Wi-Fi] Found %d visible 2.4GHz network(s):\n", n);
            for (int i = 0; i < n; ++i) {
                Serial.printf("    - %-24s (%d dBm)\n", WiFi.SSID(i).c_str(), WiFi.RSSI(i));
            }
        }
        Serial.println("[Wi-Fi] Starting fallback SoftAP mode...");
        WiFi.mode(WIFI_AP);
        WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD);
        Serial.printf("[Wi-Fi AP] SoftAP SSID:     %s\n", WIFI_AP_SSID);
        Serial.printf("[Wi-Fi AP] SoftAP Password: %s\n", WIFI_AP_PASSWORD);
        Serial.printf("[Wi-Fi AP] Web Dashboard:   http://%s/\n", WiFi.softAPIP().toString().c_str());
    }
}

void setup() {
    // 1. Initialize Serial Console
    Serial.begin(115200);
    delay(500);
    printBanner();

    // 2. Safety First: Ensure all outputs are strictly in STOP / OFF state
    Serial.println("[Safety] Enforcing initial zero-throttle state...");

    // 3. Initialize DShot Motor Subsystem
    if (!motorDriver.begin()) {
        Serial.println("[ERROR] Failed to initialize DShot Motor Driver!");
    } else {
        Serial.printf("[Motor] DShot initialized on GPIO%d (Mode: %s)\n", 
                      PIN_DSHOT_SIGNAL, dshot_mode_name[DEFAULT_DSHOT_MODE]);
    }

    // 4. Initialize AM32 / KISS ESC Telemetry Subsystem
    if (!escTelemetry.begin()) {
        Serial.println("[ERROR] Failed to initialize ESC Telemetry!");
    } else {
        Serial.printf("[Telemetry] AM32/KISS Telemetry listening on UART%d RX=GPIO%d (Baud: %d)\n",
                      ESC_TELEMETRY_UART_NUM, PIN_ESC_TELEMETRY_RX, ESC_TELEMETRY_BAUD);
    }

    // 5. Initialize HX711 Load Cell Subsystem
    if (activeLoadCell && activeLoadCell->begin()) {
        Serial.printf("[LoadCell] HX711 Load Cell initialized on UART%d (RX=GPIO%d, TX=GPIO%d, Baud: %d)\n",
                      LOADCELL_UART_NUM, PIN_LOADCELL_RX, PIN_LOADCELL_TX, LOADCELL_BAUD);
    } else {
        Serial.println("[ERROR] Failed to initialize Load Cell Driver!");
    }

    // 6. Initialize Automated Test Runner Task
    if (!testRunner.begin()) {
        Serial.println("[ERROR] Failed to initialize Test Runner coordinator!");
    } else {
        Serial.println("[TestRunner] Automated sequencer task initialized and ready.");
    }

    // 7. Initialize Wi-Fi
    initWiFi();

    // 8. Safety Check before starting web server
    motorDriver.stop();

    // 9. Initialize HTTP Web Server
    if (!webServer.begin()) {
        Serial.println("[ERROR] Failed to start HTTP server!");
    } else {
        Serial.printf("[Web] HTTP Web Server running on port %d\n", HTTP_SERVER_PORT);
    }

    // 10. Print System Status Report
    Serial.println("\n----------------- HARDWARE & SYSTEM STATUS -----------------");
    Serial.printf("  * Controller Board:     LILYGO TTGO T7 V1.3 Mini32\n");
    Serial.printf("  * DShot Control Pin:    GPIO %d (%s)\n", PIN_DSHOT_SIGNAL, dshot_mode_name[DEFAULT_DSHOT_MODE]);
    Serial.printf("  * ESC Telemetry Pin:    GPIO %d RX (115200 8N1)\n", PIN_ESC_TELEMETRY_RX);
    Serial.printf("  * Load Cell UART:       GPIO %d RX, GPIO %d TX (%d baud)\n", PIN_LOADCELL_RX, PIN_LOADCELL_TX, LOADCELL_BAUD);
    Serial.printf("  * MOCK_ESC Mode:        %s\n", MOCK_ESC ? "ACTIVE (Simulated)" : "DISABLED (Hardware)");
    // Print boot reset reason
    esp_reset_reason_t rstReason = esp_reset_reason();
    const char* rst_str = "UNKNOWN";
    switch (rstReason) {
        case ESP_RST_POWERON: rst_str = "POWER_ON"; break;
        case ESP_RST_EXT: rst_str = "EXTERNAL_PIN (EN glitch)"; break;
        case ESP_RST_SW: rst_str = "SOFTWARE_RESTART"; break;
        case ESP_RST_PANIC: rst_str = "EXCEPTION_PANIC (Crash/Abort/bad_alloc)"; break;
        case ESP_RST_INT_WDT: rst_str = "INT_WATCHDOG"; break;
        case ESP_RST_TASK_WDT: rst_str = "TASK_WATCHDOG"; break;
        case ESP_RST_WDT: rst_str = "OTHER_WATCHDOG"; break;
        case ESP_RST_DEEPSLEEP: rst_str = "DEEP_SLEEP"; break;
        case ESP_RST_BROWNOUT: rst_str = "BROWNOUT (Voltage drop on 3.3V/5V)"; break;
        case ESP_RST_SDIO: rst_str = "SDIO"; break;
        default: break;
    }
    Serial.printf("  * Boot Reset Reason:    %s (%d)\n", rst_str, (int)rstReason);
    Serial.printf("  * Web Dashboard:        http://%s/\n", WiFi.isConnected() ? WiFi.localIP().toString().c_str() : WiFi.softAPIP().toString().c_str());
    Serial.println("------------------------------------------------------------\n");
    Serial.println("[System] Ready. Open the web dashboard in your browser to begin testing.\n");
}

void loop() {
    // Non-blocking servicing of incoming HTTP client requests
    webServer.update();

    // Auto-reconnect Wi-Fi if connection drops mid-test
    static uint32_t lastWifiCheckMs = 0;
    if (millis() - lastWifiCheckMs >= 3000) {
        lastWifiCheckMs = millis();
        if (WiFi.status() != WL_CONNECTED && WiFi.getMode() == WIFI_STA) {
            WiFi.reconnect();
        }
    }

    // Periodic heartbeat console report (every 5 seconds)
    static uint32_t lastPrintMs = 0;
    if (millis() - lastPrintMs >= 5000) {
        lastPrintMs = millis();

        LiveTestStatus st = testRunner.getLiveStatus();
        EscTelemetryData esc = escTelemetry.getSnapshot();
        LoadCellSample lc;
        if (activeLoadCell) activeLoadCell->getLatestSample(lc);

        uint32_t dshotFrames = 0, dshotFails = 0;
        uint16_t dshotLastErr = 0, dshotLastRaw = 0;
        motorDriver.getTxStats(dshotFrames, dshotFails, dshotLastErr, dshotLastRaw);

        Serial.printf("[Heartbeat] State: %-12s | Throttle: %4u | DShotTx: %lu (Fail: %lu) | Load: %6.1fg | RPM: %5u | Curr: %4.1fA | Volt: %4.1fV | WiFi: %s\n",
                      st.stateString.c_str(),
                      st.commandedThrottle,
                      (unsigned long)dshotFrames,
                      (unsigned long)dshotFails,
                      lc.loadGrams,
                      esc.rpm,
                      esc.current_a,
                      esc.voltage_v,
                      WiFi.isConnected() ? "Connected" : "AP Mode");
    }

    // Small yield to allow FreeRTOS IDLE task to run
    vTaskDelay(pdMS_TO_TICKS(5));
}
