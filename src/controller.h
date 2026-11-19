#ifndef Controller_h
#define Controller_h
#include <ESPAsyncWebServer.h>
#include "movement.h"
#include "pen.h"
#include "runner.h"

struct DrawingFileInfo {
    bool exists = false;
    bool valid = false;
    double distance = 0;   // total travel [mm], from the "d" header line
    double height = 0;     // image height [mm], from the "h" header line
    size_t sizeBytes = 0;
};

// Setup is a set of independent steps (hanger distance, belt position, pen,
// drawing) that can be done in any order and revisited at any time. Instead of a
// fixed phase sequence, every action checks the state it needs (e.g. "not while
// moving", "needs the hanger distance") and the UI shows what is still missing.
class Controller {
    private:
    Movement *movement;
    Pen *pen;
    Runner *runner;
    AsyncWebServer *server;

    volatile bool positionedMove = false;  // extend-to-home or test move in progress
    bool positionRestored = false;         // position came from "resume", not homing
    bool testMoveDone = false;             // a test move ran since the position was restored
    String uploadError;                    // set by the upload handler, reported on completion
    bool wasDrawing = false;               // to notice when a drawing ends

    bool rejectIfDrawing(AsyncWebServerRequest *request);
    bool rejectIfBusy(AsyncWebServerRequest *request);
    bool drawingFileReady();
    public:
    Controller(Movement *movement, Pen *pen, Runner *runner, AsyncWebServer *server);
    void registerRoutes();
    void loop();

    // Short status for diagnostics: drawing / moving / idle
    const char *statusName();

    void respondWithState(AsyncWebServerRequest *request);
    void setTopDistance(AsyncWebServerRequest *request);
    void jog(AsyncWebServerRequest *request);
    void extendToHome(AsyncWebServerRequest *request);
    void resumeSession(AsyncWebServerRequest *request);
    void goHome(AsyncWebServerRequest *request);
    void setServo(AsyncWebServerRequest *request);
    void setPenCalibration(AsyncWebServerRequest *request);
    void estepsCalibration(AsyncWebServerRequest *request);
    void handleUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final);
    void uploadComplete(AsyncWebServerRequest *request);
    void run(AsyncWebServerRequest *request);
    void pauseDrawing(AsyncWebServerRequest *request);
    void resumeDrawing(AsyncWebServerRequest *request);
    void stopDrawing(AsyncWebServerRequest *request);

    static DrawingFileInfo readDrawingFileInfo();
};
#endif
