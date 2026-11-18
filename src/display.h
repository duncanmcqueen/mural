#ifndef Display_h
#define Display_h
#include <Adafruit_SSD1306.h>

// v1.1 hardware has no OLED. Set to 1 (e.g. build_flags = -DMURAL_HAS_OLED=1) if
// you fit a 128x64 SSD1306 on I2C 0x3C; the firmware then probes for it at boot.
#ifndef MURAL_HAS_OLED
#define MURAL_HAS_OLED 0
#endif

class Display {
    private:
    Adafruit_SSD1306 *display;
    bool present = false;
    public:
    Display();
    bool isPresent() { return present; }
    void displayText(String text);
    void displayHomeScreen(String ipLine, String orLine, String mdnsLine);
};
#endif