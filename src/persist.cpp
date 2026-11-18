#include "persist.h"
#include "movement.h"
#include "logger.h"
#include <esp_system.h>

Persist persist;

namespace {
    constexpr const char *NS = "mural";
    constexpr unsigned long POSITION_IDLE_SAVE_MS = 1000;      // still for this long -> save
}

const char *resetReasonToString(int reason) {
    switch (reason) {
        case ESP_RST_POWERON:   return "Power on";
        case ESP_RST_EXT:       return "External reset pin";
        case ESP_RST_SW:        return "Software restart";
        case ESP_RST_PANIC:     return "Crash (panic/exception)";
        case ESP_RST_INT_WDT:   return "Interrupt watchdog";
        case ESP_RST_TASK_WDT:  return "Task watchdog";
        case ESP_RST_WDT:       return "Other watchdog";
        case ESP_RST_DEEPSLEEP: return "Wake from deep sleep";
        case ESP_RST_BROWNOUT:  return "BROWNOUT (supply voltage dropped)";
        case ESP_RST_SDIO:      return "SDIO reset";
        default:                return "Unknown";
    }
}

void Persist::begin() {
    prefs.begin(NS, false);

    bootCount = prefs.getUInt("bootCount", 0) + 1;
    prefs.putUInt("bootCount", bootCount);

    prefs.getBytes("rstHist", resetHistory, sizeof(resetHistory));
    memmove(resetHistory + 1, resetHistory, RESET_HISTORY - 1);
    resetHistory[0] = (uint8_t)esp_reset_reason();
    prefs.putBytes("rstHist", resetHistory, sizeof(resetHistory));

    logDrawingMoves = prefs.getBool("logMoves", false);

    Log.printf("Boot #%u, reset reason: %s\n", bootCount, resetReasonToString(resetHistory[0]));
    if (resetHistory[0] == ESP_RST_BROWNOUT) {
        Log.println("WARNING: last reset was a BROWNOUT - check the power supply / servo current draw");
    }
}

int Persist::getResetHistory(uint8_t *out, int max) {
    int n = 0;
    for (int i = 0; i < RESET_HISTORY && i < max; i++) {
        if (resetHistory[i] == 0) break; // ESP_RST_UNKNOWN / unused slot
        out[n++] = resetHistory[i];
    }
    return n;
}

int Persist::loadTopDistance() {
    return prefs.getInt("topDist", -1);
}

void Persist::saveTopDistance(int distance) {
    prefs.putInt("topDist", distance);
}

PenSettings Persist::loadPenSettings() {
    PenSettings s;
    s.upAngle = prefs.getInt("penUp", s.upAngle);
    s.downAngle = prefs.getInt("penDown", s.downAngle);
    s.minAngle = prefs.getInt("srvMin", s.minAngle);
    s.maxAngle = prefs.getInt("srvMax", s.maxAngle);
    s.speedDegPerSec = prefs.getInt("srvSpeed", s.speedDegPerSec);
    s.releaseWhenUp = prefs.getBool("srvRelease", s.releaseWhenUp);
    return s;
}

void Persist::savePenSettings(const PenSettings &s) {
    prefs.putInt("penUp", s.upAngle);
    prefs.putInt("penDown", s.downAngle);
    prefs.putInt("srvMin", s.minAngle);
    prefs.putInt("srvMax", s.maxAngle);
    prefs.putInt("srvSpeed", s.speedDegPerSec);
    prefs.putBool("srvRelease", s.releaseWhenUp);
}

SavedPosition Persist::loadPosition() {
    SavedPosition p;
    if (prefs.getBytesLength("pos") == sizeof(SavedPosition)) {
        prefs.getBytes("pos", &p, sizeof(SavedPosition));
    }
    return p;
}

void Persist::savePosition(Movement *movement) {
    if (!movement->isHomed() || movement->isMoving()) {
        return;
    }
    SavedPosition p;
    p.valid = 1;
    p.topDistance = movement->getTopDistance();
    p.leftSteps = movement->getLeftSteps();
    p.rightSteps = movement->getRightSteps();
    p.x = movement->getRawX();
    p.y = movement->getRawY();

    unsigned long start = micros();
    prefs.putBytes("pos", &p, sizeof(SavedPosition));
    lastSaveDurationUs = micros() - start;

    lastSavedLeft = p.leftSteps;
    lastSavedRight = p.rightSteps;
    lastSaveMs = millis();
    positionSaveCount++;
}

void Persist::invalidatePosition(const char *reason) {
    SavedPosition p = loadPosition();
    if (p.valid) {
        p.valid = 0;
        prefs.putBytes("pos", &p, sizeof(SavedPosition));
        prefs.putString("posWhy", reason);
        Log.printf("Saved position invalidated: %s\n", reason);
    }
    lastSavedLeft = lastSavedRight = INT32_MIN;
}

String Persist::getPositionInvalidReason() {
    return prefs.getString("posWhy", "");
}

int Persist::loadLastServoAngle() {
    return prefs.getInt("srvLast", -1);
}

void Persist::saveLastServoAngle(int angle) {
    prefs.putInt("srvLast", angle);
}

void Persist::setLogDrawingMoves(bool enabled) {
    logDrawingMoves = enabled;
    prefs.putBool("logMoves", enabled);
    Log.printf("Logging of drawing moves %s\n", enabled ? "enabled" : "disabled");
}

void Persist::loopPosition(Movement *movement, bool drawing) {
    if (movement->consumePositionInvalidated()) {
        invalidatePosition("belts were moved manually");
    }

    if (drawing || !movement->isHomed() || movement->isMoving()) {
        return;
    }

    int32_t left = movement->getLeftSteps();
    int32_t right = movement->getRightSteps();
    if (left == lastSavedLeft && right == lastSavedRight) {
        return;
    }

    unsigned long now = millis();
    if (left != lastSeenLeft || right != lastSeenRight) {
        lastSeenLeft = left;
        lastSeenRight = right;
        lastChangeMs = now;
    }

    if (now - lastChangeMs >= POSITION_IDLE_SAVE_MS) {
        savePosition(movement);
    }
}

SavedDrawing Persist::loadDrawing() {
    SavedDrawing d;
    if (prefs.getBytesLength("drawing") == sizeof(SavedDrawing)) {
        prefs.getBytes("drawing", &d, sizeof(SavedDrawing));
    }
    d.name[sizeof(d.name) - 1] = '\0';
    return d;
}

void Persist::saveDrawing(int topDistance, const String &name) {
    SavedDrawing d;
    d.valid = 1;
    d.topDistance = topDistance;
    strncpy(d.name, name.c_str(), sizeof(d.name) - 1);
    prefs.putBytes("drawing", &d, sizeof(SavedDrawing));
}

void Persist::invalidateDrawing() {
    SavedDrawing d = loadDrawing();
    if (d.valid) {
        d.valid = 0;
        prefs.putBytes("drawing", &d, sizeof(SavedDrawing));
    }
}
