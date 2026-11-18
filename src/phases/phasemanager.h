#ifndef PhaseManager_H
#define PhaseManager_H
#include "phase.h"
#include "movement.h"
#include "pen.h"
#include "runner.h"
#include <ESPAsyncWebServer.h>

struct DrawingFileInfo {
    bool exists = false;
    bool valid = false;
    double distance = 0;   // total travel [mm], from the "d" header line
    double height = 0;     // image height [mm], from the "h" header line
    size_t sizeBytes = 0;
};

class PhaseManager {
    private:
    Phase* currentPhase;
    Phase* retractBeltsPhase;
    Phase* setTopDistancePhase;
    Phase* extendToHomePhase;
    Phase* penCalibrationPhase;
    Phase* svgSelectPhase;
    Phase* beginDrawingPhase;
    Movement* movement;
    Pen* pen;
    Runner* runner;

    // Per-session progress. These are deliberately NOT persisted: after a reboot the
    // user explicitly chooses to resume (see resumeSession / useSavedDrawing).
    bool drawingReady = false;   // a drawing was uploaded or re-used for this session
    bool penConfirmed = false;   // pen calibration confirmed for this session

    bool busy(AsyncWebServerRequest *request);
    public:
    enum PhaseNames {RetractBelts, SetTopDistance, ExtendToHome, PenCalibration, SvgSelect, BeginDrawing};
    PhaseManager(Movement* movement, Pen* pen, Runner* runner, AsyncWebServer* server);
    Phase* getCurrentPhase();
    void setPhase(PhaseNames name);
    void respondWithState(AsyncWebServerRequest *request);
    void reset();

    // Moves to the first step that still needs doing.
    void advance();
    void setDrawingReady(bool ready) { drawingReady = ready; }
    void setPenConfirmed(bool confirmed) { penConfirmed = confirmed; }
    Movement* getMovement() { return movement; }

    // Endpoints that span phases
    void resumeSession(AsyncWebServerRequest *request);
    void useSavedDrawing(AsyncWebServerRequest *request);
    void gotoPhase(AsyncWebServerRequest *request);
    void goHome(AsyncWebServerRequest *request);

    static DrawingFileInfo readDrawingFileInfo();
};
#endif
