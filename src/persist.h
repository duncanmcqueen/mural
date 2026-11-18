#ifndef Persist_h
#define Persist_h
#include <Arduino.h>
#include <Preferences.h>

class Movement;

// Last known belt position. Only meaningful if the bot hasn't been moved by hand
// (or slipped) since it was saved - restoring it is an explicit, user-confirmed action.
struct SavedPosition {
    uint8_t valid = 0;
    int32_t topDistance = -1;
    int32_t leftSteps = 0;
    int32_t rightSteps = 0;
    double x = 0;
    double y = 0;
};

// Metadata for the /commands file on LittleFS (which itself survives reboots).
struct SavedDrawing {
    uint8_t valid = 0;
    int32_t topDistance = -1;   // drawings are scaled to the drawable width, which depends on this
    char name[64] = {0};
};

// Pen/servo settings. Angles are raw servo degrees; lower = towards the wall.
struct PenSettings {
    int upAngle = 90;
    int downAngle = -1;         // -1 = never calibrated
    int minAngle = 0;           // soft limits - nothing will ever command the servo outside these
    int maxAngle = 180;
    int speedDegPerSec = 90;
    bool releaseWhenUp = false; // stop sending pulses once the pen is up (servo goes limp)
};

class Persist {
    private:
    Preferences prefs;
    static const int RESET_HISTORY = 8;
    uint8_t resetHistory[RESET_HISTORY] = {0};
    uint32_t bootCount = 0;
    bool logDrawingMoves = false;

    // position autosave bookkeeping
    int32_t lastSeenLeft = INT32_MIN, lastSeenRight = INT32_MIN;
    int32_t lastSavedLeft = INT32_MIN, lastSavedRight = INT32_MIN;
    unsigned long lastChangeMs = 0;
    unsigned long lastSaveMs = 0;
    unsigned long lastSaveDurationUs = 0;
    uint32_t positionSaveCount = 0;
    public:
    void begin();

    uint32_t getBootCount() { return bootCount; }
    int getResetHistory(uint8_t *out, int max);  // most recent first

    int loadTopDistance();
    void saveTopDistance(int distance);

    PenSettings loadPenSettings();
    void savePenSettings(const PenSettings &settings);

    SavedPosition loadPosition();
    void savePosition(Movement *movement);
    void invalidatePosition(const char *reason);
    String getPositionInvalidReason();
    // Call every loop() right after the steppers have been serviced. During setup,
    // saves the position once the motors have been still for a moment. Never saves
    // while drawing (no flash writes mid-drawing).
    void loopPosition(Movement *movement, bool drawing);

    // Last commanded servo angle (setup only), so the servo can start from where the
    // horn actually is after a reboot instead of jumping. -1 = unknown.
    int loadLastServoAngle();
    void saveLastServoAngle(int angle);

    // Diagnostics: log every drawing move ("Starting the move to x, y"). Off by
    // default - at 9600 baud each line blocks the main loop for ~35 ms.
    bool getLogDrawingMoves() { return logDrawingMoves; }
    void setLogDrawingMoves(bool enabled);

    unsigned long getLastPositionSaveMs() { return lastSaveMs; }
    unsigned long getLastPositionSaveDurationUs() { return lastSaveDurationUs; }
    uint32_t getPositionSaveCount() { return positionSaveCount; }

    SavedDrawing loadDrawing();
    void saveDrawing(int topDistance, const String &name);
    void invalidateDrawing();
};

extern Persist persist;

const char *resetReasonToString(int reason);
#endif
