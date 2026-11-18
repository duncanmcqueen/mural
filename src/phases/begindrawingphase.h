#ifndef BeginDrawingPhase_h
#define BeginDrawingPhase_h
#include "notsupportedphase.h"
#include "phasemanager.h"
class BeginDrawingPhase : public NotSupportedPhase {
    private:
    PhaseManager* manager;
    Runner* runner;
    AsyncWebServer* server;
    Movement* movement;
    Pen* pen;
    public:
    BeginDrawingPhase(PhaseManager* manager, Runner* runner, AsyncWebServer* server, Movement* movement, Pen* pen);
    const char* getName();
    void run(AsyncWebServerRequest *request);
    void doneWithPhase(AsyncWebServerRequest *request);
};
#endif
