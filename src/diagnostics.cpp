#include "diagnostics.h"
#include "persist.h"
#include "logger.h"
#include "AsyncJson.h"
#include "ArduinoJson.h"
#include <LittleFS.h>
#include <WiFi.h>
#include <esp_wifi.h>

namespace {
    Movement *gMovement;
    Pen *gPen;
    Runner *gRunner;
    Controller *gController;
    Display *gDisplay;

    bool rejectIfDrawing(AsyncWebServerRequest *request) {
        if (gRunner->isRunning()) {
            request->send(409, "text/plain", "Not available while drawing");
            return true;
        }
        return false;
    }

    int intParam(AsyncWebServerRequest *request, const char *name, int fallback) {
        if (request->hasParam(name, true)) {
            return request->getParam(name, true)->value().toInt();
        }
        return fallback;
    }

    void handleDiagnostics(AsyncWebServerRequest *request) {
        AsyncResponseStream *response = request->beginResponseStream("application/json");
        DynamicJsonBuffer jsonBuffer;
        JsonObject &root = jsonBuffer.createObject();

        JsonObject &system = root.createNestedObject("system");
        system["firmware"] = MURAL_FW_VERSION;
        system["built"] = __DATE__ " " __TIME__;
        system["uptimeMs"] = millis();
        system["bootCount"] = persist.getBootCount();
        uint8_t history[8];
        int n = persist.getResetHistory(history, 8);
        system["resetReason"] = n > 0 ? resetReasonToString(history[0]) : "Unknown";
        JsonArray &resets = system.createNestedArray("resetHistory");
        for (int i = 0; i < n; i++) {
            resets.add(resetReasonToString(history[i]));
        }
        system["freeHeap"] = ESP.getFreeHeap();
        system["minFreeHeap"] = ESP.getMinFreeHeap();
        system["cpuMHz"] = ESP.getCpuFreqMHz();
        system["wifiRssi"] = WiFi.RSSI();
        wifi_country_t country;
        if (esp_wifi_get_country(&country) == ESP_OK) {
            // cc[] is not guaranteed to be NUL-terminated - don't hand it to String().
            char cc[3] = {country.cc[0], country.cc[1], '\0'};
            system["wifiCountry"] = cc;
        }
        bool hotspotMode = WiFi.getMode() == WIFI_AP;
        system["ip"] = hotspotMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
        system["wifiMode"] = hotspotMode ? "hotspot" : "station";
        system["oledPresent"] = gDisplay->isPresent();
        system["phase"] = gController->statusName();
        system["drawing"] = gRunner->isRunning();
        system["progress"] = gRunner->getProgress();
        system["logDrawingMoves"] = persist.getLogDrawingMoves();
        system["webWhileDrawing"] = persist.getWebWhileDrawing();

        const PenSettings &ps = gPen->getSettings();
        JsonObject &servo = root.createNestedObject("servo");
        servo["pin"] = SERVO_PIN;
        servo["attached"] = gPen->isAttached();
        servo["positionKnown"] = gPen->isPositionKnown();
        servo["commandedAngle"] = gPen->getCurrentAngle();
        servo["targetAngle"] = gPen->getTargetAngle();
        servo["pulseUs"] = gPen->getPulseWidthUs();
        servo["moving"] = gPen->isMoving();
        servo["isDown"] = gPen->isDown();
        servo["moveCount"] = gPen->getMoveCount();
        servo["lastMoveFrom"] = gPen->getLastMoveFrom();
        servo["lastMoveTo"] = gPen->getLastMoveTo();
        servo["lastMoveMs"] = gPen->getLastMoveDurationMs();
        servo["cycleRemaining"] = gPen->getCycleRemaining();
        servo["bootStartAngle"] = gPen->getBootStartAngle();
        servo["upAngle"] = ps.upAngle;
        servo["downAngle"] = ps.downAngle;
        servo["minAngle"] = ps.minAngle;
        servo["maxAngle"] = ps.maxAngle;
        servo["speedDegPerSec"] = ps.speedDegPerSec;
        servo["releaseWhenUp"] = ps.releaseWhenUp;

        JsonObject &motors = root.createNestedObject("motors");
        motors["moving"] = gMovement->isMoving();
        motors["homed"] = gMovement->isHomed();
        motors["topDistance"] = gMovement->getTopDistance();
        motors["leftSteps"] = gMovement->getLeftSteps();
        motors["rightSteps"] = gMovement->getRightSteps();
        motors["x"] = gMovement->getRawX();
        motors["y"] = gMovement->getRawY();

        SavedPosition pos = persist.loadPosition();
        JsonObject &saved = root.createNestedObject("savedPosition");
        saved["valid"] = pos.valid != 0;
        if (!pos.valid) {
            saved["invalidReason"] = persist.getPositionInvalidReason();
        }
        saved["topDistance"] = pos.topDistance;
        saved["leftSteps"] = pos.leftSteps;
        saved["rightSteps"] = pos.rightSteps;
        saved["x"] = pos.x;
        saved["y"] = pos.y;
        unsigned long lastSave = persist.getLastPositionSaveMs();
        saved["ageMs"] = lastSave == 0 ? -1 : (long)(millis() - lastSave);
        saved["saveCount"] = persist.getPositionSaveCount();
        saved["lastSaveUs"] = persist.getLastPositionSaveDurationUs();

        JsonObject &storage = root.createNestedObject("storage");
        storage["totalBytes"] = LittleFS.totalBytes();
        storage["usedBytes"] = LittleFS.usedBytes();
        DrawingFileInfo info = Controller::readDrawingFileInfo();
        SavedDrawing drawing = persist.loadDrawing();
        storage["drawingName"] = drawing.name;
        storage["drawingValid"] = drawing.valid && info.valid;
        storage["drawingBytes"] = info.sizeBytes;
        storage["drawingTopDistance"] = drawing.topDistance;

        root.printTo(*response);
        request->send(response);
    }

    void handleServo(AsyncWebServerRequest *request) {
        if (rejectIfDrawing(request)) return;
        if (!request->hasParam("action", true)) {
            request->send(400, "text/plain", "Missing action");
            return;
        }
        String action = request->getParam("action", true)->value();
        Log.printf("Diagnostics: servo %s\n", action.c_str());
        if (action == "up") {
            gPen->stopTest();
            gPen->up();
        } else if (action == "down") {
            if (!gPen->isCalibrated()) {
                request->send(400, "text/plain", "Pen down angle not calibrated yet");
                return;
            }
            gPen->stopTest();
            gPen->down();
        } else if (action == "angle") {
            gPen->stopTest();
            gPen->moveTo(intParam(request, "value", gPen->getTargetAngle()));
        } else if (action == "nudge") {
            gPen->stopTest();
            gPen->moveTo(gPen->getTargetAngle() + intParam(request, "value", 0));
        } else if (action == "release") {
            gPen->release();
        } else if (action == "cycle") {
            int cycles = intParam(request, "value", 5);
            if (cycles < 1) cycles = 1;
            if (cycles > 100) cycles = 100;
            gPen->startCycleTest(cycles);
        } else if (action == "stop") {
            gPen->stopTest();
        } else {
            request->send(400, "text/plain", "Unknown action");
            return;
        }
        request->send(200, "text/plain", "OK");
    }

    void handleServoSettings(AsyncWebServerRequest *request) {
        if (rejectIfDrawing(request)) return;
        PenSettings s = gPen->getSettings();
        s.upAngle = intParam(request, "up", s.upAngle);
        s.downAngle = intParam(request, "down", s.downAngle);
        s.minAngle = intParam(request, "min", s.minAngle);
        s.maxAngle = intParam(request, "max", s.maxAngle);
        s.speedDegPerSec = intParam(request, "speed", s.speedDegPerSec);
        if (request->hasParam("releaseWhenUp", true)) {
            String v = request->getParam("releaseWhenUp", true)->value();
            s.releaseWhenUp = v == "1" || v == "true";
        }
        gPen->applySettings(s);
        request->send(200, "text/plain", "OK");
    }

    void handlePreviewUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
        if (gRunner->isRunning()) return;
        if (!index) {
            if (LittleFS.exists("/preview.tmp")) {
                LittleFS.remove("/preview.tmp");
            }
            size_t freeBytes = LittleFS.totalBytes() - LittleFS.usedBytes();
            // Keep plenty of room for the commands, which matter more than the preview.
            if (request->contentLength() * 3 > freeBytes) {
                Log.printf("Preview (%u bytes) skipped - not enough free space\n", request->contentLength());
                return;
            }
            request->_tempFile = LittleFS.open("/preview.tmp", "w");
        }
        if (len && request->_tempFile) {
            request->_tempFile.write(data, len);
        }
        if (final && request->_tempFile) {
            request->_tempFile.close();
        }
    }
}

void registerDiagnosticsRoutes(AsyncWebServer *server, Movement *movement, Pen *pen, Runner *runner,
                               Controller *controller, Display *display) {
    gMovement = movement;
    gPen = pen;
    gRunner = runner;
    gController = controller;
    gDisplay = display;

    server->on("/diagnostics", HTTP_GET, handleDiagnostics);

    server->on("/log", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "text/plain", Log.dump());
    });

    server->on("/servo", HTTP_POST, handleServo);
    server->on("/servoSettings", HTTP_POST, handleServoSettings);

    server->on("/diagSettings", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (rejectIfDrawing(request)) return;
        if (request->hasParam("logDrawingMoves", true)) {
            String v = request->getParam("logDrawingMoves", true)->value();
            persist.setLogDrawingMoves(v == "1" || v == "true");
        }
        if (request->hasParam("webWhileDrawing", true)) {
            String v = request->getParam("webWhileDrawing", true)->value();
            persist.setWebWhileDrawing(v == "1" || v == "true");
        }
        request->send(200, "text/plain", "OK");
    });

    server->on("/uploadPreview", HTTP_POST,
        [](AsyncWebServerRequest *request) { request->send(200, "text/plain", "OK"); },
        handlePreviewUpload);

    server->on("/savedPreview", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!LittleFS.exists("/preview.txt")) {
            request->send(404, "text/plain", "No preview");
            return;
        }
        request->send(LittleFS, "/preview.txt", "text/plain");
    });
}
