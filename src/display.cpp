#include "display.h"
#include "logger.h"
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
#define SCREEN_ADDRESS 0x3C
Display::Display() {
    display = nullptr;

#if !MURAL_HAS_OLED
    // v1.1 hardware has no OLED: don't touch the I2C bus at all.
    present = false;
    return;
#endif

    // Probe the I2C bus and run headless if nothing answers.
    Wire.begin();
    Wire.beginTransmission(SCREEN_ADDRESS);
    present = Wire.endTransmission() == 0;
    if (!present) {
        Log.println("No OLED display detected, running without one");
        return;
    }

    display = new Adafruit_SSD1306(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
    if (!display->begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS))
    {
        Log.println("SSD1306 allocation failed, running without display");
        delete display;
        display = nullptr;
        present = false;
        return;
    }
    Log.println("OLED display detected");
    delay(2000);
    display->setRotation(2);
    display->clearDisplay();
    display->setTextColor(WHITE);
    display->setTextSize(1);
    display->display();
}

void Display::displayText(String text)
{
    if (!display) {
        return; // headless - callers already log what they display
    }
    int16_t x1;
    int16_t y1;
    uint16_t width;
    uint16_t height;

    display->getTextBounds(text, 0, 0, &x1, &y1, &width, &height);

    // display on horizontal and vertical center
    display->clearDisplay(); // clear display
    display->setCursor((SCREEN_WIDTH - width) / 2, (SCREEN_HEIGHT - height) / 2);
    display->println(text); // text to display
    display->display();
    Log.println("Displayed " + text);
}

void Display::displayHomeScreen(String ipLine, String orLine, String mdnsLine) {
    if (!display) {
        Log.println("Ready at " + ipLine + " " + orLine + " " + mdnsLine);
        return;
    }
    display->clearDisplay();

    int16_t x1;
    int16_t y1;
    uint16_t width;
    uint16_t height;

    display->getTextBounds(ipLine, 0, 0, &x1, &y1, &width, &height);
    display->setCursor((SCREEN_WIDTH - width) / 2, 10);
    display->println(ipLine);

    display->getTextBounds(orLine, 0, 0, &x1, &y1, &width, &height);
    display->setCursor((SCREEN_WIDTH - width) / 2, 10 + SCREEN_HEIGHT / 3);
    display->println(orLine);

    display->getTextBounds(mdnsLine, 0, 0, &x1, &y1, &width, &height);
    display->setCursor((SCREEN_WIDTH - width) / 2, 10 + SCREEN_HEIGHT / 3 * 2);
    display->println(mdnsLine);

    display->display();
}