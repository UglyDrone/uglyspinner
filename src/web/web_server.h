#pragma once

#include <Arduino.h>
#include <WebServer.h>
#include "config.h"

class TestStandWebServer {
public:
    TestStandWebServer(uint16_t port = HTTP_SERVER_PORT);
    ~TestStandWebServer();

    bool begin();
    void update();

private:
    void _setupRoutes();
    void _handleRoot();
    void _handleStatus();
    void _handleStart();
    void _handleAbort();
    void _handleTare();
    void _handleResults();
    void _handleExportJson();
    void _handleLoadCellDebug();
    void _handleLoadCellConfig();
    void _handleLoadCellCalibrate();
    void _handleEscDebug();
    void _handleEscConfig();
    void _handleMotorThrottle();
    void _handlePinDiag();
    void _handleEscSniff();
    void _handleNotFound();

    WebServer _server;
    uint16_t _port;
};

extern TestStandWebServer webServer;
