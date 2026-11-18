#include "settopdistancephase.h"
#include "commandhandlingphase.h"
#include "persist.h"
#include "logger.h"
SetTopDistancePhase::SetTopDistancePhase(PhaseManager* manager, Movement* movement, Pen* pen) : CommandHandlingPhase(movement) {
    this->manager = manager;
    this->movement = movement;
    this->pen = pen;
}

void SetTopDistancePhase::setTopDistance(AsyncWebServerRequest *request) {
    const AsyncWebParameter* p = request->getParam(0);
    int distance = p->value().toInt();
    if (distance <= 0) {
        request->send(400, "text/plain", "Distance must be a positive number of millimetres");
        return;
    }
    Log.printf("Setting distance to %d mm\n", distance);
    movement->setTopDistance(distance);
    persist.saveTopDistance(distance);
    manager->advance();
    manager->respondWithState(request);
}

void SetTopDistancePhase::setServo(AsyncWebServerRequest *request) {
    const AsyncWebParameter* p = request->getParam(0);
    int angle = p->value().toInt();
    pen->moveTo(angle);
    request->send(200, "text/plain", "OK");
}

void SetTopDistancePhase::estepsCalibration(AsyncWebServerRequest* request) {
    Log.println("Extending 1000mm");
    movement->extend1000mm();
    request->send(200, "text/plain", "OK");
}

const char* SetTopDistancePhase::getName() {
    return "SetTopDistance";
}
