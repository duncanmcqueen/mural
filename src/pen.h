#ifndef Pen_h
#define Pen_h
#include <ESP32Servo.h>
#include <atomic>
#include "persist.h"

constexpr int SERVO_PIN = 2;

// Non-blocking, ramped servo control. Call update() from loop().
// Every move is ramped at speedDegPerSec and clamped to the soft limits, so the
// servo never slams into the end of its slot. After boot the servo starts from the
// last angle saved during setup; only if that's unknown is the first move a jump.
//
// Note: a hobby servo gives no feedback - everything reported here is what we
// *commanded*, not where the horn physically is.
class Pen {
    private:
    Servo *servo;
    PenSettings settings;

    volatile int targetAngle;           // written from the web server task, read in update()
    volatile bool pendingMove = false;  // a new target was requested
    volatile bool busy = false;         // move requested, ramping or settling
    volatile bool releaseRequested = false;
    volatile int cycleRemaining = 0;    // diagnostics: up/down test half-cycles left

    double currentAngle;                // last commanded angle (ramped)
    bool positionKnown = false;         // false until the first pulse after boot
    bool attached = false;
    bool ramping = false;
    unsigned long lastUpdateMs = 0;
    unsigned long settleUntilMs = 0;
    unsigned long moveStartMs = 0;

    // stats for diagnostics
    uint32_t moveCount = 0;
    unsigned long lastMoveDurationMs = 0;
    int lastMoveFrom = -1;
    int lastMoveTo = -1;

    // Boot behaviour: wait a moment so the first pulse doesn't coincide with WiFi
    // start-up and the stepper drivers energising, then start from the last angle
    // saved during setup (where the horn most likely still is) and ramp from there.
    static const unsigned long BOOT_HOLDOFF_MS = 2000;
    unsigned long holdOffUntilMs = 0;
    int bootStartAngle = -1;

    bool persistLastAngle = true;   // false while drawing - no flash writes mid-drawing
    int lastPersistedAngle = -1;

    // On-the-fly pen-down adjustment while drawing. The web task stores the new
    // angle here; update() applies it on the main loop (-1 = nothing pending).
    std::atomic<int> requestedDownAngle{-1};
    int downAngleBeforeAdjust = -1;     // calibrated angle before any adjustment, -1 = not adjusted
    void applyRequestedDownAngle();

    static const unsigned long SETTLE_MS = 200;
    void rememberAngle();

    int clamp(int angle);
    void writeAngle(int angle);
    public:
    Pen();
    void update();

    void moveTo(int angle);        // ramped, clamped
    void up();
    void down();
    void release();                // stop sending pulses (servo goes limp)
    void startCycleTest(int cycles);
    void stopTest();

    bool isMoving() { return busy; }
    bool isDown();
    bool isCalibrated() { return settings.downAngle >= 0; }

    const PenSettings &getSettings() { return settings; }
    // Nudge the pen-down angle by `steps` degrees towards the wall (negative = away).
    // Safe to call from the web task. If the pen is down it moves right away (ramped),
    // otherwise the new angle is used at the next pen-down. Stays within the soft
    // limits and never reaches the up angle. Returns the new down angle.
    int adjustDownAngle(int steps);
    int getDownAngle();                 // including a not-yet-applied adjustment
    int getDownAngleBeforeAdjust() { return downAngleBeforeAdjust; }
    // Saves an adjusted down angle to flash (call when not drawing).
    void saveAdjustedDownAngle();
    void applySettings(const PenSettings &newSettings); // clamps, saves to flash

    // diagnostics
    int getTargetAngle() { return targetAngle; }
    double getCurrentAngle() { return currentAngle; }
    bool isAttached() { return attached; }
    bool isPositionKnown() { return positionKnown; }
    int getPulseWidthUs();
    uint32_t getMoveCount() { return moveCount; }
    unsigned long getLastMoveDurationMs() { return lastMoveDurationMs; }
    int getLastMoveFrom() { return lastMoveFrom; }
    int getLastMoveTo() { return lastMoveTo; }
    int getCycleRemaining() { return cycleRemaining; }
    int getBootStartAngle() { return bootStartAngle; }
    void setPersistLastAngle(bool enabled) { persistLastAngle = enabled; }
};
#endif
