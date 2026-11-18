#include <Arduino.h>
#include <WiFiManager.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <FS.h>
#include <LittleFS.h>
#include <Wire.h>
#include <ESPmDNS.h>
#include "movement.h"
#include "runner.h"
#include "pen.h"
#include "display.h"
#include "phases/phasemanager.h"
#include "persist.h"
#include "logger.h"
#include "diagnostics.h"

AsyncWebServer server(80);

Movement *movement;
Runner *runner;
Pen *pen;
Display *display;

PhaseManager* phaseManager;

void notFound(AsyncWebServerRequest *request)
{
    request->send(404, "text/plain", "Not found");
}

void handleUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
    phaseManager->getCurrentPhase()->handleUpload(request, filename, index, data, len, final);
}

void handleGetState(AsyncWebServerRequest *request) {
    phaseManager->respondWithState(request);
}

std::vector<const char *> menu = {"wifi", "sep"};
void setup()
{
    delay(10);
    Serial.begin(9600);
    Log.printf("\nMural firmware %s (built %s %s)\n", MURAL_FW_VERSION, __DATE__, __TIME__);

    // First thing: record why we (re)booted - brownouts show up here.
    persist.begin();

    if (!LittleFS.begin(true)) {
        Log.println("An Error has occurred while mounting LittleFS");
        return;
    }

    display = new Display();
    Log.println("Initialized display");

    // initialize movement right away or the motors can start creeping due to floating output
    movement = new Movement(display);
    Log.println("Initialized steppers");

    bool resetAfterConnect = false;
    std::function<void()> serverCallback = [&] () {
        resetAfterConnect = true;
    };
    
    WiFiManager wifiManager;
    
    wifiManager.setConnectTimeout(20);
    wifiManager.setTitle("Connect to WiFi");
    wifiManager.setMenu(menu);
    wifiManager.setWebServerCallback(serverCallback);
    wifiManager.autoConnect("Mural");

    if (resetAfterConnect) {
        Log.println("Connected to WiFi through captive portal, restarting...");
        ESP.restart();
    }
    
    Log.println("Connected to wifi");

    MDNS.begin("mural");

    Log.println("Started mDNS for mural");

    pen = new Pen();
    Log.println("Initialized servo");

    runner = new Runner(movement, pen, display);
    Log.println("Initialized runner");

    server.serveStatic("/", LittleFS, "/www/").setDefaultFile("index.html").setCacheControl("no-cache");

    server.on("/command", HTTP_POST, [](AsyncWebServerRequest *request)
              { phaseManager->getCurrentPhase()->handleCommand(request); });

    server.on("/setTopDistance", HTTP_POST, [](AsyncWebServerRequest *request)
              { phaseManager->getCurrentPhase()->setTopDistance(request); });

    server.on("/extendToHome", HTTP_POST, [](AsyncWebServerRequest *request)
              { phaseManager->getCurrentPhase()->extendToHome(request); });

    server.on("/setServo", HTTP_POST, [](AsyncWebServerRequest *request)
              { phaseManager->getCurrentPhase()->setServo(request); });

    server.on("/setPenDistance", HTTP_POST, [](AsyncWebServerRequest *request)
              { phaseManager->getCurrentPhase()->setPenDistance(request); });

    server.on("/estepsCalibration", HTTP_POST, [](AsyncWebServerRequest *request)
              { phaseManager->getCurrentPhase()->estepsCalibration(request); });

    server.on("/doneWithPhase", HTTP_POST, [](AsyncWebServerRequest *request)
              { phaseManager->getCurrentPhase()->doneWithPhase(request); });

    server.on("/run", HTTP_POST, [](AsyncWebServerRequest *request)
              { phaseManager->getCurrentPhase()->run(request); });

    server.on("/resume", HTTP_POST, [](AsyncWebServerRequest *request)
              { phaseManager->getCurrentPhase()->resumeTopDistance(request); });

    server.on("/getState", HTTP_GET, [](AsyncWebServerRequest *request)
              { handleGetState(request); });

    server.on(
        "/uploadCommands", HTTP_POST,
        [](AsyncWebServerRequest *request) {
            handleGetState(request);
        }, 
        handleUpload
    );

    server.on("/resumeSession", HTTP_POST, [](AsyncWebServerRequest *request)
              { phaseManager->resumeSession(request); });

    server.on("/useSavedDrawing", HTTP_POST, [](AsyncWebServerRequest *request)
              { phaseManager->useSavedDrawing(request); });

    server.on("/gotoPhase", HTTP_POST, [](AsyncWebServerRequest *request)
              { phaseManager->gotoPhase(request); });

    server.on("/goHome", HTTP_POST, [](AsyncWebServerRequest *request)
              { phaseManager->goHome(request); });

    server.on(
        "/downloadCommands", HTTP_GET,
        [](AsyncWebServerRequest *request) {
            request->send(LittleFS, "/commands", "text/plain");
        }
    );

    server.onNotFound(notFound);

    Log.println("Finished setting up the server");

    phaseManager = new PhaseManager(movement, pen, runner, &server);
    registerDiagnosticsRoutes(&server, movement, pen, runner, phaseManager, display);

    server.begin();
    Log.println("Server started");

    display->displayHomeScreen("http://" + WiFi.localIP().toString(), "or", "http://mural.local");
    
}

void loop()
{
    movement->runSteppers();
    // Right after stepping: between drawing increments the motors are momentarily
    // idle, which is when a consistent position can be saved.
    persist.loopPosition(movement, runner->isRunning());
    pen->update();
    runner->run();
    phaseManager->getCurrentPhase()->loopPhase();
}
