#include "begindrawingphase.h"
#include "logger.h"
#include "persist.h"
BeginDrawingPhase::BeginDrawingPhase(PhaseManager* manager, Runner* runner, AsyncWebServer* server, Movement* movement, Pen* pen) {
    this->manager = manager;
    this->runner = runner;
    this->server = server;
    this->movement = movement;
    this->pen = pen;
}

void BeginDrawingPhase::run(AsyncWebServerRequest *request) {
    // Check everything up front - Runner::start() throws (and reboots the ESP) otherwise.
    if (movement->isMoving() || pen->isMoving()) {
        request->send(409, "text/plain", "Still moving, try again in a moment");
        return;
    }
    if (!movement->isHomed()) {
        request->send(400, "text/plain", "Belt position is unknown - re-home first");
        return;
    }
    if (!pen->isCalibrated()) {
        request->send(400, "text/plain", "Pen is not calibrated");
        return;
    }
    if (!PhaseManager::readDrawingFileInfo().valid) {
        request->send(400, "text/plain", "No valid drawing loaded");
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
    request->send(200, "text/plain", "OK");
    server->end();
}

void BeginDrawingPhase::doneWithPhase(AsyncWebServerRequest *request) {
    manager->reset();
    manager->respondWithState(request);
}

const char* BeginDrawingPhase::getName() {
    return "BeginDrawing";
}
