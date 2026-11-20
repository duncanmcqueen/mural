#include <Arduino.h>
#include <WiFiManager.h>
#include <esp_wifi.h>
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
    
    // 802.11b only: the ESP32 fails the 4-way handshake at this spot in the house
    // on 802.11n rates (-66 dBm). See nikivanov/mural issue #109.
    WiFi.mode(WIFI_STA);
    esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B);

    WiFiManager wifiManager;

    // The country code decides which channels and power levels the radio may use, so it
    // has to match where Mural is installed. It is asked for on the captive portal (empty
    // on the first boot, prefilled afterwards), kept in NVS and applied on every boot.
    // "US" is used until someone answers.
    String wifiCountry = persist.loadWifiCountry();
    // Length 3 preserves overlong input for rejection below; the pattern enforces two letters.
    WiFiManagerParameter countryParam("country", "WiFi country code (two letters, e.g. US)",
                                      wifiCountry.c_str(), 3,
                                      "size=2 pattern='[A-Za-z]{2}' title='Two letters, e.g. US'");
    wifiManager.addParameter(&countryParam);
    wifiManager.setSaveParamsCallback([&]() {
        String cc = countryParam.getValue();
        cc.trim();
        cc.toUpperCase();
        if (cc.length() == 2 && cc[0] >= 'A' && cc[0] <= 'Z' && cc[1] >= 'A' && cc[1] <= 'Z') {
            persist.saveWifiCountry(cc);
        } else {
            Log.println("Country code not saved - it must be two letters, e.g. US");
        }
    });

    wifiManager.setCountry(wifiCountry.length() > 0 ? wifiCountry : String("US"));
    wifiManager.setConnectTimeout(20);
    wifiManager.setConnectRetries(2);
    wifiManager.setCleanConnect(true);
    wifiManager.setTitle("Connect to WiFi");
    wifiManager.setMenu(menu);
    wifiManager.setWebServerCallback(serverCallback);
    wifiManager.autoConnect("Mural");

    if (resetAfterConnect) {
        Log.println("Connected to WiFi through captive portal, restarting...");
        ESP.restart();
    }
    
    Log.println("Connected to wifi");

    wifi_country_t appliedCountry;
    if (esp_wifi_get_country(&appliedCountry) == ESP_OK) {
        Log.printf("WiFi country: %c%c, RSSI: %d dBm\n", appliedCountry.cc[0], appliedCountry.cc[1], WiFi.RSSI());
    }

    MDNS.begin("mural");

    Log.println("Started mDNS for mural");

    pen = new Pen();
    Log.println("Initialized servo");

    runner = new Runner(movement, pen, display);
    Log.println("Initialized runner");

    controller = new Controller(movement, pen, runner, &server);
    controller->registerRoutes();
    registerDiagnosticsRoutes(&server, movement, pen, runner, controller, display);

    // Registered last: handlers are tried in registration order, so the API routes above
    // answer first. With this first, every GET (/log, /diagnostics, /getState) made the
    // static handler look for a file that cannot exist and log an error before giving up.
    server.serveStatic("/", LittleFS, "/www/").setDefaultFile("index.html").setCacheControl("no-cache");

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
