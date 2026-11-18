#include "pencalibrationphase.h"
#include "logger.h"
PenCalibrationPhase::PenCalibrationPhase(PhaseManager* manager, Pen* pen) {
    this->manager = manager;
    this->pen = pen;
}

void PenCalibrationPhase::setServo(AsyncWebServerRequest *request) {
    const AsyncWebParameter* p = request->getParam(0);
    int angle = p->value().toInt();
    pen->moveTo(angle);  // ramped and clamped to the soft limits
    request->send(200, "text/plain", "OK");
}

// Params: angle = pen-down angle, up = pen-up angle (optional, keeps current if omitted)
void PenCalibrationPhase::setPenDistance(AsyncWebServerRequest *request) {
    if (!request->hasParam("angle", true)) {
        request->send(400, "text/plain", "Missing angle");
        return;
    }
    PenSettings settings = pen->getSettings();
    settings.downAngle = request->getParam("angle", true)->value().toInt();
    if (request->hasParam("up", true)) {
        settings.upAngle = request->getParam("up", true)->value().toInt();
    }
    if (settings.downAngle >= settings.upAngle) {
        Log.printf("Warning: pen down angle (%d) is not below pen up angle (%d)\n", settings.downAngle, settings.upAngle);
    }
    pen->applySettings(settings);
    pen->up();
    manager->setPenConfirmed(true);
    manager->advance();
    manager->respondWithState(request);
}

const char* PenCalibrationPhase::getName() {
    return "PenCalibration";
}
