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
#include "controller.h"
#include "persist.h"
#include "logger.h"
#include "diagnostics.h"

AsyncWebServer server(80);

Movement *movement;
Runner *runner;
Pen *pen;
Display *display;

Controller *controller;

void notFound(AsyncWebServerRequest *request)
{
    request->send(404, "text/plain", "Not found");
}

std::vector<const char *> menu = {"wifi", "sep"};
void setup()
{
    delay(10);
    Serial.begin(115200); // was 9600: logging blocks the loop ~1 ms per character at that speed
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

    controller = new Controller(movement, pen, runner, &server);
    controller->registerRoutes();
    registerDiagnosticsRoutes(&server, movement, pen, runner, controller, display);

    server.onNotFound(notFound);

    Log.println("Finished setting up the server");

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
    controller->loop();
}
