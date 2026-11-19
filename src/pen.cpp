#include "pen.h"
#include "logger.h"

Pen::Pen()
{
    servo = new Servo();
    settings = persist.loadPenSettings();
    // Don't attach yet: ESP32Servo emits no pulses until attached, so the servo stays
    // wherever it physically is until the first update(). Head for the "up" angle.
    targetAngle = clamp(settings.upAngle);
    currentAngle = targetAngle;
    pendingMove = true;
    busy = true;
    holdOffUntilMs = millis() + BOOT_HOLDOFF_MS;
    int saved = persist.loadLastServoAngle();
    if (saved >= 0 && saved <= 180) {
        bootStartAngle = clamp(saved);
        lastPersistedAngle = saved;
    }
    Log.printf("Pen: up=%d down=%d limits=%d..%d speed=%d deg/s releaseWhenUp=%d\n",
               settings.upAngle, settings.downAngle, settings.minAngle, settings.maxAngle,
               settings.speedDegPerSec, settings.releaseWhenUp);
}

int Pen::clamp(int angle) {
    if (angle < settings.minAngle) return settings.minAngle;
    if (angle > settings.maxAngle) return settings.maxAngle;
    return angle;
}

void Pen::writeAngle(int angle) {
    if (!attached) {
        servo->attach(SERVO_PIN);
        attached = true;
    }
    servo->write(angle);
}

void Pen::rememberAngle() {
    int angle = (int)round(currentAngle);
    if (!persistLastAngle || cycleRemaining > 0 || angle == lastPersistedAngle) {
        return; // not while drawing or cycle-testing, and only when it changed
    }
    persist.saveLastServoAngle(angle);
    lastPersistedAngle = angle;
}

int Pen::getPulseWidthUs() {
    return attached ? servo->readMicroseconds() : 0;
}

void Pen::moveTo(int angle) {
    int clamped = clamp(angle);
    if (clamped != angle) {
        Log.printf("Pen: requested %d deg clamped to %d (limits %d..%d)\n", angle, clamped, settings.minAngle, settings.maxAngle);
    }
    releaseRequested = false;
    targetAngle = clamped;
    busy = true;
    pendingMove = true;
}

void Pen::up() {
    moveTo(settings.upAngle);
}

void Pen::down() {
    if (!isCalibrated()) {
        Log.println("Pen: down requested but pen isn't calibrated - ignoring");
        return;
    }
    moveTo(settings.downAngle);
}

bool Pen::isDown() {
    return isCalibrated() && targetAngle == clamp(settings.downAngle);
}

int Pen::getDownAngle() {
    int requested = requestedDownAngle.load();
    return requested >= 0 ? requested : settings.downAngle;
}

int Pen::adjustDownAngle(int steps) {
    if (!isCalibrated()) {
        return settings.downAngle;
    }
    // Web requests are handled one at a time, so build on any pending request.
    int base = getDownAngle();
    int up = settings.upAngle;
    // Towards the wall = from the up angle towards the down angle.
    int towardsWall = settings.downAngle <= up ? -1 : 1;
    int next = clamp(base + towardsWall * steps);
    if (towardsWall < 0 && next >= up) next = base < up ? up - 1 : base;
    if (towardsWall > 0 && next <= up) next = base > up ? up + 1 : base;
    if (next != base) {
        requestedDownAngle.store(next);
    }
    return next;
}

// Main loop only.
void Pen::applyRequestedDownAngle() {
    int requested = requestedDownAngle.load();
    if (requested < 0) {
        return;
    }
    int oldDown = settings.downAngle;
    bool wasDown = isDown();
    if (downAngleBeforeAdjust < 0) {
        downAngleBeforeAdjust = oldDown;
    }
    settings.downAngle = requested;
    // If another request came in meanwhile, it stays pending and is applied next time.
    requestedDownAngle.compare_exchange_strong(requested, -1);
    Log.printf("Pen: down angle adjusted %d -> %d deg (calibrated %d)%s\n", oldDown, settings.downAngle,
               downAngleBeforeAdjust, wasDown ? "" : ", used at next pen-down");
    if (wasDown) {
        moveTo(settings.downAngle);
    }
}

void Pen::saveAdjustedDownAngle() {
    applyRequestedDownAngle();
    if (downAngleBeforeAdjust < 0) {
        return;
    }
    if (settings.downAngle != downAngleBeforeAdjust) {
        persist.savePenSettings(settings);
        Log.printf("Pen: adjusted down angle saved (%d -> %d deg)\n", downAngleBeforeAdjust, settings.downAngle);
    }
    downAngleBeforeAdjust = -1;
}

void Pen::release() {
    cycleRemaining = 0;
    releaseRequested = true;
}

void Pen::startCycleTest(int cycles) {
    if (!isCalibrated()) {
        Log.println("Pen: cycle test needs a calibrated down angle");
        return;
    }
    Log.printf("Pen: starting %d up/down test cycles\n", cycles);
    cycleRemaining = cycles * 2;
}

void Pen::stopTest() {
    cycleRemaining = 0;
}

void Pen::applySettings(const PenSettings &newSettings) {
    PenSettings s = newSettings;
    // An explicit calibration replaces any on-the-fly adjustment.
    requestedDownAngle.store(-1);
    downAngleBeforeAdjust = -1;
    if (s.minAngle < 0) s.minAngle = 0;
    if (s.maxAngle > 180) s.maxAngle = 180;
    if (s.minAngle > s.maxAngle) s.minAngle = s.maxAngle;
    if (s.speedDegPerSec < 5) s.speedDegPerSec = 5;
    if (s.speedDegPerSec > 600) s.speedDegPerSec = 600;
    settings = s;
    settings.upAngle = clamp(settings.upAngle);
    if (settings.downAngle >= 0) {
        settings.downAngle = clamp(settings.downAngle);
    }
    persist.savePenSettings(settings);
    Log.printf("Pen settings saved: up=%d down=%d limits=%d..%d speed=%d releaseWhenUp=%d\n",
               settings.upAngle, settings.downAngle, settings.minAngle, settings.maxAngle,
               settings.speedDegPerSec, settings.releaseWhenUp);
    // keep the current target inside the (possibly narrower) limits
    if (targetAngle != clamp(targetAngle)) {
        moveTo(targetAngle);
    }
}

void Pen::update() {
    unsigned long now = millis();

    if (releaseRequested) {
        releaseRequested = false;
        pendingMove = false;
        ramping = false;
        busy = false;
        if (attached) {
            servo->detach();
            attached = false;
            Log.println("Pen: servo released (no pulses)");
            rememberAngle();
        }
        return;
    }

    applyRequestedDownAngle();

    if (pendingMove) {
        if (!positionKnown && now < holdOffUntilMs) {
            return; // let boot settle before the servo's first pulse
        }
        pendingMove = false;
        busy = true; // re-assert in case the web task's moveTo() raced with the settle step below
        int target = targetAngle;
        if (!positionKnown && bootStartAngle >= 0) {
            // Start pulsing at the angle we last commanded during setup. If the horn
            // hasn't been moved since, this pulse doesn't move it at all; then ramp.
            Log.printf("Pen: first move after boot, starting from last saved angle %d deg, ramping to %d\n", bootStartAngle, target);
            writeAngle(bootStartAngle);
            currentAngle = bootStartAngle;
            positionKnown = true;
            lastMoveFrom = bootStartAngle;
        } else if (!positionKnown) {
            // No saved angle (first boot with this firmware), so the first pulse is
            // necessarily a jump. Make it to the "up" angle.
            Log.printf("Pen: first move after boot, going straight to %d deg (position unknown)\n", target);
            writeAngle(target);
            currentAngle = target;
            positionKnown = true;
            lastMoveFrom = -1;
        } else {
            lastMoveFrom = (int)round(currentAngle);
            if (!attached) {
                // re-attach at the last commanded angle, then ramp from there
                writeAngle(lastMoveFrom);
            }
        }
        lastMoveTo = target;
        moveStartMs = now;
        lastUpdateMs = now;
        settleUntilMs = 0;
        ramping = true;
    }

    if (ramping) {
        int target = targetAngle;
        lastMoveTo = target;
        double maxStep = settings.speedDegPerSec * (now - lastUpdateMs) / 1000.0;
        lastUpdateMs = now;
        double delta = target - currentAngle;
        if (fabs(delta) <= maxStep) {
            currentAngle = target;
        } else if (maxStep > 0) {
            currentAngle += delta > 0 ? maxStep : -maxStep;
        }
        writeAngle((int)round(currentAngle));

        if (currentAngle == target) {
            ramping = false;
            settleUntilMs = now + SETTLE_MS;
        }
        return;
    }

    if (busy) {
        if (now < settleUntilMs) {
            return;
        }
        busy = false;
        lastMoveDurationMs = now - moveStartMs;
        moveCount++;
        rememberAngle();
        if (settings.releaseWhenUp && cycleRemaining == 0 && targetAngle == clamp(settings.upAngle) && attached) {
            servo->detach();
            attached = false;
        }
        return;
    }

    if (cycleRemaining > 0) {
        bool goDown = (cycleRemaining % 2) == 0;
        cycleRemaining = cycleRemaining - 1;
        if (goDown) {
            down();
        } else {
            up();
        }
    }
}
