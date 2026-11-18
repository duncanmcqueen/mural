#include "phasemanager.h"
#include "retractbeltsphase.h"
#include "settopdistancephase.h"
#include "extendtohomephase.h"
#include "pencalibrationphase.h"
#include "svgselectphase.h"
#include "begindrawingphase.h"
#include "AsyncJson.h"
#include "ArduinoJson.h"
#include "persist.h"
#include "logger.h"
#include <LittleFS.h>
#include <stdexcept>

PhaseManager::PhaseManager(Movement* movement, Pen* pen, Runner* runner, AsyncWebServer* server) {
    retractBeltsPhase = new RetractBeltsPhase(this, movement);
    setTopDistancePhase = new SetTopDistancePhase(this, movement, pen);
    extendToHomePhase = new ExtendToHomePhase(this, movement);
    penCalibrationPhase = new PenCalibrationPhase(this, pen);
    svgSelectPhase = new SvgSelectPhase(this);
    beginDrawingPhase = new BeginDrawingPhase(this, runner, server, movement, pen);

    this->movement = movement;
    this->pen = pen;
    this->runner = runner;
    reset();
}

Phase* PhaseManager::getCurrentPhase() {
    return currentPhase;
}

void PhaseManager::setPhase(PhaseNames name) {
    Log.print("Switching current phase to ");
    switch (name) {
        case PhaseNames::RetractBelts:
            Log.println("RetractBelts");
            currentPhase = retractBeltsPhase;
            break;
        case PhaseNames::SetTopDistance:
            Log.println("SetTopDistance");
            currentPhase = setTopDistancePhase;
            break;
        case PhaseNames::ExtendToHome:
            Log.println("ExtendToHome");
            currentPhase = extendToHomePhase;
            break;
        case PhaseNames::PenCalibration:
            Log.println("PenCalibration");
            currentPhase = penCalibrationPhase;
            break;
        case PhaseNames::SvgSelect:
            Log.println("SvgSelect");
            currentPhase = svgSelectPhase;
            break;
        case PhaseNames::BeginDrawing:
            Log.println("BeginDrawing");
            currentPhase = beginDrawingPhase;
            break;
        default:
            throw std::invalid_argument("Invalid Phase");
    }
}

void PhaseManager::advance() {
    if (movement->getTopDistance() == -1) {
        setPhase(SetTopDistance);
    } else if (!drawingReady) {
        setPhase(SvgSelect);
    } else if (!movement->isHomed()) {
        movement->resetHoming();
        setPhase(RetractBelts);
    } else if (!penConfirmed || !pen->isCalibrated()) {
        setPhase(PenCalibration);
    } else {
        setPhase(BeginDrawing);
    }
}

DrawingFileInfo PhaseManager::readDrawingFileInfo() {
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

bool PhaseManager::busy(AsyncWebServerRequest *request) {
    if (runner->isRunning() || movement->isMoving()) {
        request->send(409, "text/plain", "Mural is busy moving, try again when it has stopped");
        return true;
    }
    return false;
}

void PhaseManager::respondWithState(AsyncWebServerRequest *request) {
    auto currentPhase = getCurrentPhase()->getName();
    auto moving = movement->isMoving();
    auto startedHoming = movement->hasStartedHoming();
    auto homePosition = movement->getHomeCoordinates();

    auto topDistance = movement->getTopDistance();
    auto safeWidth = topDistance != -1 ? movement->getWidth() : -1;

    AsyncResponseStream *response = request->beginResponseStream("application/json");
    DynamicJsonBuffer jsonBuffer;
    JsonObject &root = jsonBuffer.createObject();

    root["phase"] = currentPhase;
    root["moving"] = moving;
    root["startedHoming"] = startedHoming;
    root["topDistance"] = topDistance;
    root["safeWidth"] = safeWidth;
    root["homeX"] = homePosition.x;
    root["homeY"] = homePosition.y;
    root["homed"] = movement->isHomed();
    root["savedTopDistance"] = persist.loadTopDistance();

    const PenSettings &ps = pen->getSettings();
    JsonObject &penJson = root.createNestedObject("pen");
    penJson["up"] = ps.upAngle;
    penJson["down"] = ps.downAngle;
    penJson["min"] = ps.minAngle;
    penJson["max"] = ps.maxAngle;
    penJson["calibrated"] = pen->isCalibrated();
    penJson["confirmed"] = penConfirmed;
    penJson["target"] = pen->getTargetAngle();

    SavedPosition pos = persist.loadPosition();
    JsonObject &session = root.createNestedObject("savedPosition");
    session["valid"] = pos.valid != 0;
    session["topDistance"] = pos.topDistance;
    session["x"] = pos.x;
    session["y"] = pos.y;
    if (!pos.valid) {
        session["reason"] = persist.getPositionInvalidReason();
    }

    SavedDrawing drawing = persist.loadDrawing();
    DrawingFileInfo fileInfo = readDrawingFileInfo();
    JsonObject &drawingJson = root.createNestedObject("savedDrawing");
    bool drawingAvailable = drawing.valid && fileInfo.valid;
    drawingJson["available"] = drawingAvailable;
    drawingJson["name"] = drawing.name;
    drawingJson["topDistance"] = drawing.topDistance;
    drawingJson["compatible"] = drawingAvailable && drawing.topDistance == topDistance;
    drawingJson["distance"] = fileInfo.distance;
    drawingJson["height"] = fileInfo.height;
    drawingJson["sizeBytes"] = fileInfo.sizeBytes;
    drawingJson["hasPreview"] = LittleFS.exists("/preview.txt");
    root["drawingReady"] = drawingReady;

    root.printTo(*response);
    request->send(response);
}

void PhaseManager::reset() {
    // Start the wizard over. The saved position stays on flash so "resume" is still
    // offered; we just stop trusting it in memory until the user picks resume or re-homes.
    drawingReady = false;
    penConfirmed = false;
    movement->forgetPosition();
    setPhase(PhaseManager::SetTopDistance);
}

void PhaseManager::resumeSession(AsyncWebServerRequest *request) {
    if (currentPhase != setTopDistancePhase) {
        request->send(400, "text/plain", "Resume is only available at the start of setup");
        return;
    }
    if (busy(request)) return;

    SavedPosition pos = persist.loadPosition();
    if (!pos.valid || pos.topDistance <= 0) {
        request->send(400, "text/plain", "No saved position to resume from");
        return;
    }

    movement->restorePosition(pos.topDistance, pos.leftSteps, pos.rightSteps, pos.x, pos.y);
    persist.saveTopDistance(pos.topDistance);
    penConfirmed = pen->isCalibrated();
    advance();
    respondWithState(request);
}

void PhaseManager::useSavedDrawing(AsyncWebServerRequest *request) {
    if (currentPhase != svgSelectPhase) {
        request->send(400, "text/plain", "Not choosing a drawing right now");
        return;
    }
    SavedDrawing drawing = persist.loadDrawing();
    DrawingFileInfo info = readDrawingFileInfo();
    if (!drawing.valid || !info.valid) {
        request->send(400, "text/plain", "No saved drawing available");
        return;
    }
    if (drawing.topDistance != movement->getTopDistance()) {
        request->send(400, "text/plain", "Saved drawing was made for a different hanger distance");
        return;
    }
    Log.printf("Re-using saved drawing \"%s\"\n", drawing.name);
    drawingReady = true;
    advance();
    respondWithState(request);
}

void PhaseManager::gotoPhase(AsyncWebServerRequest *request) {
    if (currentPhase != beginDrawingPhase) {
        request->send(400, "text/plain", "Can only jump back from the ready screen");
        return;
    }
    if (busy(request)) return;
    if (!request->hasParam("phase", true)) {
        request->send(400, "text/plain", "Missing phase");
        return;
    }
    String target = request->getParam("phase", true)->value();
    if (target == "SvgSelect") {
        drawingReady = false;
    } else if (target == "PenCalibration") {
        penConfirmed = false;
    } else if (target == "RetractBelts") {
        movement->forgetPosition();
    } else {
        request->send(400, "text/plain", "Unsupported phase");
        return;
    }
    advance();
    respondWithState(request);
}

void PhaseManager::goHome(AsyncWebServerRequest *request) {
    if (currentPhase != beginDrawingPhase) {
        request->send(400, "text/plain", "Not available right now");
        return;
    }
    if (busy(request)) return;
    if (!movement->isHomed()) {
        request->send(400, "text/plain", "Position unknown");
        return;
    }
    if (pen->isDown() || pen->isMoving()) {
        request->send(400, "text/plain", "Pen must be up before moving");
        return;
    }
    try {
        float seconds = movement->moveToHome();
        request->send(200, "text/plain", String(int(ceil(seconds))));
    } catch (const std::exception &e) {
        Log.printf("Move to home failed: %s\n", e.what());
        request->send(500, "text/plain", String("Move failed: ") + e.what());
    }
}
