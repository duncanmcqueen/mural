#include "controller.h"
#include "persist.h"
#include "logger.h"
#include "AsyncJson.h"
#include "ArduinoJson.h"
#include <LittleFS.h>
#include <stdexcept>

namespace {
    int intParam(AsyncWebServerRequest *request, const char *name, int fallback) {
        if (request->hasParam(name, true)) {
            return request->getParam(name, true)->value().toInt();
        }
        if (request->hasParam(name)) {
            return request->getParam(name)->value().toInt();
        }
        return fallback;
    }
}

Controller::Controller(Movement *movement, Pen *pen, Runner *runner, AsyncWebServer *server) {
    this->movement = movement;
    this->pen = pen;
    this->runner = runner;
    this->server = server;

    // The hanger distance is a property of where Mural is hung; restore it so that
    // step is already done after a reboot. Belt position is never restored
    // automatically - that's an explicit "resume" action.
    int savedDistance = persist.loadTopDistance();
    if (savedDistance > 0) {
        movement->setTopDistance(savedDistance);
    }
}

void Controller::registerRoutes() {
    server->on("/getState", HTTP_GET, [this](AsyncWebServerRequest *r) { respondWithState(r); });
    server->on("/setTopDistance", HTTP_POST, [this](AsyncWebServerRequest *r) { setTopDistance(r); });
    server->on("/command", HTTP_POST, [this](AsyncWebServerRequest *r) { jog(r); });
    server->on("/extendToHome", HTTP_POST, [this](AsyncWebServerRequest *r) { extendToHome(r); });
    server->on("/resumeSession", HTTP_POST, [this](AsyncWebServerRequest *r) { resumeSession(r); });
    server->on("/goHome", HTTP_POST, [this](AsyncWebServerRequest *r) { goHome(r); });
    server->on("/setServo", HTTP_POST, [this](AsyncWebServerRequest *r) { setServo(r); });
    server->on("/setPenDistance", HTTP_POST, [this](AsyncWebServerRequest *r) { setPenCalibration(r); });
    server->on("/estepsCalibration", HTTP_POST, [this](AsyncWebServerRequest *r) { estepsCalibration(r); });
    server->on("/run", HTTP_POST, [this](AsyncWebServerRequest *r) { run(r); });
    server->on("/pauseDrawing", HTTP_POST, [this](AsyncWebServerRequest *r) { pauseDrawing(r); });
    server->on("/resumeDrawing", HTTP_POST, [this](AsyncWebServerRequest *r) { resumeDrawing(r); });
    server->on("/stopDrawing", HTTP_POST, [this](AsyncWebServerRequest *r) { stopDrawing(r); });
    server->on("/adjustPenDown", HTTP_POST, [this](AsyncWebServerRequest *r) { adjustPenDown(r); });
    server->on("/uploadCommands", HTTP_POST,
        [this](AsyncWebServerRequest *r) { uploadComplete(r); },
        [this](AsyncWebServerRequest *r, String filename, size_t index, uint8_t *data, size_t len, bool final) {
            handleUpload(r, filename, index, data, len, final);
        });
    server->on("/downloadCommands", HTTP_GET, [](AsyncWebServerRequest *r) {
        r->send(LittleFS, "/commands", "text/plain");
    });
}

void Controller::loop() {
    if (positionedMove && !movement->isMoving()) {
        positionedMove = false;
        Log.println("Move complete");
    }

    bool drawing = runner->isRunning();
    if (wasDrawing && !drawing) {
        // Drawing finished or was stopped: back to setup, flash writes allowed again.
        pen->setPersistLastAngle(true);
        pen->saveAdjustedDownAngle(); // keep any pen-down adjustment made while drawing
        positionRestored = false;
        testMoveDone = false;
    }
    wasDrawing = drawing;
}

const char *Controller::statusName() {
    if (runner->isRunning()) return runner->stateName();
    if (positionedMove) return "moving to home";
    if (movement->isMoving()) return "moving";
    return "idle";
}

bool Controller::rejectIfDrawing(AsyncWebServerRequest *request) {
    if (runner->isRunning()) {
        request->send(409, "text/plain", "Not available while drawing");
        return true;
    }
    return false;
}

bool Controller::rejectIfBusy(AsyncWebServerRequest *request) {
    if (rejectIfDrawing(request)) return true;
    if (movement->isMoving()) {
        request->send(409, "text/plain", "Mural is moving, try again when it has stopped");
        return true;
    }
    return false;
}

DrawingFileInfo Controller::readDrawingFileInfo() {
    DrawingFileInfo info;
    if (!LittleFS.exists("/commands")) {
        return info;
    }
    File file = LittleFS.open("/commands");
    if (!file) {
        return info;
    }
    info.exists = true;
    info.sizeBytes = file.size();
    String distanceLine = file.readStringUntil('\n');
    String heightLine = file.readStringUntil('\n');
    file.close();
    distanceLine.trim();
    heightLine.trim();
    if (distanceLine.length() > 1 && distanceLine.charAt(0) == 'd' &&
        heightLine.length() > 1 && heightLine.charAt(0) == 'h') {
        info.distance = distanceLine.substring(1).toDouble();
        info.height = heightLine.substring(1).toDouble();
        info.valid = true;
    }
    return info;
}

// A drawing is usable if the file is intact and it was made for the current
// hanger distance (drawings are scaled to the drawable width).
bool Controller::drawingFileReady() {
    SavedDrawing drawing = persist.loadDrawing();
    return drawing.valid && readDrawingFileInfo().valid &&
           movement->getTopDistance() > 0 && drawing.topDistance == movement->getTopDistance();
}

void Controller::respondWithState(AsyncWebServerRequest *request) {
    auto topDistance = movement->getTopDistance();
    auto homePosition = movement->getHomeCoordinates();

    AsyncResponseStream *response = request->beginResponseStream("application/json");
    DynamicJsonBuffer jsonBuffer;
    JsonObject &root = jsonBuffer.createObject();

    root["status"] = statusName();
    root["drawingActive"] = runner->isRunning();
    root["webWhileDrawing"] = persist.getWebWhileDrawing();

    JsonObject &runJson = root.createNestedObject("drawing");
    runJson["state"] = runner->stateName();
    runJson["progress"] = runner->getProgress() < 0 ? 0 : runner->getProgress();
    runJson["distanceDone"] = runner->getDistanceSoFar();
    runJson["totalDistance"] = runner->getTotalDistance();
    runJson["elapsedMs"] = runner->getElapsedMs();
    runJson["penDown"] = pen->isDown();
    runJson["lastOutcome"] = runner->lastOutcomeName();
    root["moving"] = movement->isMoving();
    root["homing"] = (bool)positionedMove;
    root["topDistance"] = topDistance;
    root["safeWidth"] = topDistance > 0 ? movement->getWidth() : -1;
    root["homeX"] = homePosition.x;
    root["homeY"] = homePosition.y;

    JsonObject &position = root.createNestedObject("position");
    position["known"] = movement->isHomed();
    position["restored"] = movement->isHomed() && positionRestored;
    position["testMoveDone"] = testMoveDone;
    position["x"] = movement->getRawX();
    position["y"] = movement->getRawY();

    SavedPosition pos = persist.loadPosition();
    JsonObject &saved = root.createNestedObject("savedPosition");
    saved["valid"] = pos.valid != 0;
    saved["topDistance"] = pos.topDistance;
    saved["x"] = pos.x;
    saved["y"] = pos.y;
    if (!pos.valid) {
        saved["reason"] = persist.getPositionInvalidReason();
    }

    const PenSettings &ps = pen->getSettings();
    JsonObject &penJson = root.createNestedObject("pen");
    penJson["up"] = ps.upAngle;
    penJson["down"] = pen->getDownAngle();
    penJson["downCalibrated"] = pen->getDownAngleBeforeAdjust() >= 0 ? pen->getDownAngleBeforeAdjust() : ps.downAngle;
    penJson["min"] = ps.minAngle;
    penJson["max"] = ps.maxAngle;
    penJson["calibrated"] = pen->isCalibrated();
    penJson["target"] = pen->getTargetAngle();
    penJson["moving"] = pen->isMoving();

    SavedDrawing drawing = persist.loadDrawing();
    DrawingFileInfo fileInfo = readDrawingFileInfo();
    bool drawingAvailable = drawing.valid && fileInfo.valid;
    bool drawingCompatible = drawingAvailable && topDistance > 0 && drawing.topDistance == topDistance;
    JsonObject &drawingJson = root.createNestedObject("drawingFile");
    drawingJson["available"] = drawingAvailable;
    drawingJson["compatible"] = drawingCompatible;
    drawingJson["name"] = drawing.name;
    drawingJson["topDistance"] = drawing.topDistance;
    drawingJson["distance"] = fileInfo.distance;
    drawingJson["height"] = fileInfo.height;
    drawingJson["sizeBytes"] = fileInfo.sizeBytes;
    drawingJson["hasPreview"] = LittleFS.exists("/preview.txt");

    JsonObject &ready = root.createNestedObject("ready");
    bool distanceReady = topDistance > 0;
    bool positionReady = movement->isHomed();
    bool penReady = pen->isCalibrated();
    ready["distance"] = distanceReady;
    ready["position"] = positionReady;
    ready["pen"] = penReady;
    ready["drawing"] = drawingCompatible;
    ready["all"] = distanceReady && positionReady && penReady && drawingCompatible;

    root.printTo(*response);
    request->send(response);
}

void Controller::setTopDistance(AsyncWebServerRequest *request) {
    if (rejectIfBusy(request)) return;
    int distance = intParam(request, "distance", -1);
    if (distance <= 0) {
        request->send(400, "text/plain", "Distance must be a positive number of millimetres");
        return;
    }
    if (distance != movement->getTopDistance() && movement->isHomed()) {
        // The pen's x/y are relative to the hangers; with a different distance the
        // current coordinates no longer describe where the bot physically is.
        movement->forgetPosition();
        persist.invalidatePosition("the hanger distance was changed");
    }
    Log.printf("Setting hanger distance to %d mm\n", distance);
    movement->setTopDistance(distance);
    persist.saveTopDistance(distance);
    respondWithState(request);
}

void Controller::jog(AsyncWebServerRequest *request) {
    if (rejectIfDrawing(request)) return;
    if (positionedMove) {
        request->send(409, "text/plain", "Wait for the current move to finish");
        return;
    }
    String command = request->hasParam("command", true) ? request->getParam("command", true)->value() : String("");
    if (command == "l-ret") {
        movement->leftStepper(-1);
    } else if (command == "l-ext") {
        movement->leftStepper(1);
    } else if (command == "l-0") {
        movement->leftStepper(0);
    } else if (command == "r-ret") {
        movement->rightStepper(-1);
    } else if (command == "r-ext") {
        movement->rightStepper(1);
    } else if (command == "r-0") {
        movement->rightStepper(0);
    } else {
        request->send(400, "text/plain", "Unsupported command");
        return;
    }
    if (command.endsWith("-ret") || command.endsWith("-ext")) {
        positionRestored = false;
        testMoveDone = false;
    }
    request->send(200, "text/plain", "OK");
}

void Controller::extendToHome(AsyncWebServerRequest *request) {
    if (rejectIfBusy(request)) return;
    if (movement->getTopDistance() <= 0) {
        request->send(400, "text/plain", "Set the hanger distance first");
        return;
    }
    try {
        auto moveTime = movement->extendToHome() + 1; // extra second for good measure
        positionedMove = true;
        positionRestored = false;
        testMoveDone = false;
        request->send(200, "text/plain", String(moveTime));
    } catch (const std::exception &e) {
        Log.printf("Extend to home failed: %s\n", e.what());
        request->send(500, "text/plain", String("Extend to home failed: ") + e.what());
    }
}

void Controller::resumeSession(AsyncWebServerRequest *request) {
    if (rejectIfBusy(request)) return;
    SavedPosition pos = persist.loadPosition();
    if (!pos.valid || pos.topDistance <= 0) {
        request->send(400, "text/plain", "No saved position to resume from");
        return;
    }
    movement->restorePosition(pos.topDistance, pos.leftSteps, pos.rightSteps, pos.x, pos.y);
    persist.saveTopDistance(pos.topDistance);
    positionRestored = true;
    testMoveDone = false;
    respondWithState(request);
}

void Controller::goHome(AsyncWebServerRequest *request) {
    if (rejectIfBusy(request)) return;
    if (!movement->isHomed()) {
        request->send(400, "text/plain", "Belt position unknown");
        return;
    }
    if (pen->isDown() || pen->isMoving()) {
        request->send(400, "text/plain", "Pen must be up before moving");
        return;
    }
    try {
        float seconds = movement->moveToHome();
        positionedMove = true;
        testMoveDone = true;
        request->send(200, "text/plain", String(int(ceil(seconds))));
    } catch (const std::exception &e) {
        Log.printf("Move to home failed: %s\n", e.what());
        request->send(500, "text/plain", String("Move failed: ") + e.what());
    }
}

void Controller::setServo(AsyncWebServerRequest *request) {
    if (rejectIfDrawing(request)) return;
    if (!request->hasParam("angle", true)) {
        request->send(400, "text/plain", "Missing angle");
        return;
    }
    pen->moveTo(intParam(request, "angle", pen->getTargetAngle())); // ramped and clamped
    request->send(200, "text/plain", "OK");
}

// Params: angle = pen-down angle, up = pen-up angle (optional)
void Controller::setPenCalibration(AsyncWebServerRequest *request) {
    if (rejectIfDrawing(request)) return;
    if (!request->hasParam("angle", true)) {
        request->send(400, "text/plain", "Missing angle");
        return;
    }
    PenSettings settings = pen->getSettings();
    settings.downAngle = intParam(request, "angle", settings.downAngle);
    settings.upAngle = intParam(request, "up", settings.upAngle);
    if (settings.downAngle >= settings.upAngle) {
        Log.printf("Warning: pen down angle (%d) is not below pen up angle (%d)\n", settings.downAngle, settings.upAngle);
    }
    pen->applySettings(settings);
    pen->up();
    respondWithState(request);
}

void Controller::estepsCalibration(AsyncWebServerRequest *request) {
    if (rejectIfBusy(request)) return;
    Log.println("Extending 1000mm");
    movement->extend1000mm();
    positionRestored = false;
    testMoveDone = false;
    request->send(200, "text/plain", "OK");
}

void Controller::handleUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
    if (!index) {
        uploadError = "";
        if (runner->isRunning()) {
            uploadError = "Not available while drawing";
            return;
        }
        if (movement->getTopDistance() <= 0) {
            uploadError = "Set the hanger distance first";
            return;
        }

        // The old drawing is about to be overwritten - don't offer it any more.
        persist.invalidateDrawing();
        if (LittleFS.exists("/commands")) {
            LittleFS.remove("/commands");
        }

        Log.printf("%d bytes total, %d bytes free\n", LittleFS.totalBytes(), LittleFS.totalBytes() - LittleFS.usedBytes());
        Log.printf("Upload size: %d bytes\n", request->contentLength());
        if (LittleFS.totalBytes() - LittleFS.usedBytes() < request->contentLength()) {
            Log.println("Not enough space on LittleFS");
            uploadError = "Not enough space for upload";
            return;
        }

        request->_tempFile = LittleFS.open("/commands", "w");
        if (!request->_tempFile) {
            uploadError = "Couldn't open file for writing";
            return;
        }
        Log.println("Upload started");
    }

    if (len && request->_tempFile) {
        request->_tempFile.write(data, len);
    }

    if (final && request->_tempFile) {
        request->_tempFile.close();
        Log.println("Upload finished");

        // Promote the preview that the UI uploaded just before the commands.
        if (LittleFS.exists("/preview.txt")) {
            LittleFS.remove("/preview.txt");
        }
        if (LittleFS.exists("/preview.tmp")) {
            LittleFS.rename("/preview.tmp", "/preview.txt");
        }

        String name = request->hasParam("name") ? request->getParam("name")->value() : String("drawing");
        persist.saveDrawing(movement->getTopDistance(), name);
    }
}

void Controller::uploadComplete(AsyncWebServerRequest *request) {
    if (uploadError.length() > 0) {
        request->send(400, "text/plain", uploadError);
        return;
    }
    respondWithState(request);
}

void Controller::run(AsyncWebServerRequest *request) {
    // Check everything up front - Runner::start() throws (and reboots the ESP) otherwise.
    if (rejectIfDrawing(request)) return;
    if (movement->isMoving() || pen->isMoving()) {
        request->send(409, "text/plain", "Still moving, try again in a moment");
        return;
    }
    if (!movement->isHomed()) {
        request->send(400, "text/plain", "Belt position is unknown - resume or re-home first");
        return;
    }
    if (!pen->isCalibrated()) {
        request->send(400, "text/plain", "Pen is not calibrated");
        return;
    }
    if (!drawingFileReady()) {
        request->send(400, "text/plain", "No drawing loaded for this hanger distance");
        return;
    }

    try {
        runner->start();
    } catch (const std::exception &e) {
        Log.printf("Failed to start drawing: %s\n", e.what());
        request->send(500, "text/plain", String("Failed to start: ") + e.what());
        return;
    }
    // Nothing is written to flash while drawing. The pre-drawing position will be
    // wrong as soon as the bot moves, so don't offer it for resume if the drawing
    // is interrupted (e.g. brownout). Runner re-saves it once the drawing finishes.
    persist.invalidatePosition("a drawing was started and didn't finish");
    pen->setPersistLastAngle(false);
    positionRestored = false;
    testMoveDone = false;
    wasDrawing = true;
    respondWithState(request);
    if (!persist.getWebWhileDrawing()) {
        Log.println("Web UI disabled while drawing (diagnostics setting)");
        server->end();
    }
}

void Controller::pauseDrawing(AsyncWebServerRequest *request) {
    if (!runner->isRunning()) {
        request->send(400, "text/plain", "Not drawing");
        return;
    }
    Log.println("Pause requested");
    runner->requestPause();
    respondWithState(request);
}

void Controller::resumeDrawing(AsyncWebServerRequest *request) {
    if (!runner->isRunning()) {
        request->send(400, "text/plain", "Not drawing");
        return;
    }
    Log.println("Resume requested");
    runner->requestResume();
    respondWithState(request);
}

// Params: steps = degrees towards the wall (more pressure), negative = away from it.
// Allowed only while drawing (including paused); saved to flash when the drawing ends.
void Controller::adjustPenDown(AsyncWebServerRequest *request) {
    if (!runner->isRunning()) {
        request->send(400, "text/plain", "Not drawing - use the pen calibration step instead");
        return;
    }
    int steps = intParam(request, "steps", 0);
    if (steps < -10 || steps > 10 || steps == 0) {
        request->send(400, "text/plain", "steps must be between -10 and 10");
        return;
    }
    int before = pen->getDownAngle();
    int after = pen->adjustDownAngle(steps);
    if (after == before) {
        request->send(409, "text/plain", "Pen down angle is already at its limit");
        return;
    }
    Log.printf("Pen down adjustment requested: %d -> %d deg\n", before, after);
    respondWithState(request);
}

void Controller::stopDrawing(AsyncWebServerRequest *request) {
    if (!runner->isRunning()) {
        request->send(400, "text/plain", "Not drawing");
        return;
    }
    Log.println("Stop requested");
    runner->requestStop();
    respondWithState(request);
}
