#include "runner.h"
#include "tasks/movementtask.h"
#include "tasks/interpolatingmovementtask.h"
#include "tasks/pentask.h"
#include "pen.h"
#include "display.h"
#include "LittleFS.h"
#include "persist.h"
#include "logger.h"
using namespace std;

Runner::Runner(Movement *movement, Pen *pen, Display *display) {
    stopped = true;
    progress = -1;
    this->movement = movement;
    this->pen = pen;
    this->display = display;
}

void Runner::initTaskProvider() {
    openedFile = LittleFS.open("/commands");
    if (!openedFile || !openedFile.available()) {
        Log.println("Failed to open file");
        throw std::invalid_argument("No File");
    }

    auto line = openedFile.readStringUntil('\n');
    if (line.charAt(0) == 'd') {
        totalDistance = line.substring(1, line.length() - 1).toDouble();
    } else {
        Log.println("Bad file - no distance");
        openedFile.close();
        throw std::invalid_argument("bad file");
    }

    auto heightLine = openedFile.readStringUntil('\n');
    if (heightLine.charAt(0) != 'h') {
        Log.println("Bad file - no height");
        openedFile.close();
        throw std::invalid_argument("bad file");
    }

    Log.println("Total distance to travel: " + String(totalDistance));

    distanceSoFar = 0;
    progress = -1; // so 0% appears right away
    startPosition = movement->getCoordinates();
    targetPosition = startPosition;

    auto homeCoordinates = movement->getHomeCoordinates();
    finishingSequence[0] = new InterpolatingMovementTask(movement, homeCoordinates);
    sequenceIx = 0;
}

void Runner::start() {
    pauseRequested = resumeRequested = stopRequested = false;
    paused = resuming = penWasDown = false;
    pausedTotalMs = 0;
    lastOutcome = Outcome::None;

    initTaskProvider();
    startMs = millis();
    currentTask = getNextTask();
    currentTask->startRunning();
    stopped = false;
    Log.println("Drawing started");
}

void Runner::requestPause() {
    if (!stopped && !paused) {
        resumeRequested = false;
        pauseRequested = true;
    }
}

void Runner::requestResume() {
    if (!stopped && (paused || pauseRequested)) {
        resumeRequested = true;
    }
}

void Runner::requestStop() {
    if (!stopped) {
        stopRequested = true;
    }
}

const char *Runner::stateName() {
    if (stopped) return "idle";
    if (stopRequested) return "stopping";
    if (paused) return resumeRequested ? "resuming" : "paused";
    if (pauseRequested) return "pausing";
    if (resuming) return "resuming";
    return "drawing";
}

unsigned long Runner::getElapsedMs() {
    if (stopped) {
        return finishedElapsedMs;
    }
    unsigned long now = millis();
    unsigned long pausedNow = paused ? now - pauseStartMs : 0;
    return now - startMs - pausedTotalMs - pausedNow;
}

const char *Runner::lastOutcomeName() {
    switch (lastOutcome) {
        case Outcome::Finished: return "finished";
        case Outcome::Stopped:  return "stopped";
        case Outcome::Failed:   return "failed";
        default:                return "none";
    }
}

Task *Runner::getNextTask()
{
    if (openedFile.available())
    {
        auto line = openedFile.readStringUntil('\n');
        if (line.charAt(0) == 'p')
        {
            if (line.charAt(1) == '1')
            {
                return new PenTask(false, pen);
            }
            else
            {
                return new PenTask(true, pen);
            }
        }
        else
        {
            auto x = line.substring(0, line.indexOf(" ")).toDouble();
            auto y = line.substring(line.indexOf(" ") + 1).toDouble();
            targetPosition = Movement::Point(x, y);
            return new InterpolatingMovementTask(movement, targetPosition);
        }
    }
    else
    {
        if (sequenceIx < (end(finishingSequence) - begin(finishingSequence))) {
            auto currentIx = sequenceIx;
            sequenceIx = sequenceIx + 1;
            Task *task = finishingSequence[currentIx];
            finishingSequence[currentIx] = nullptr; // ownership passes to currentTask
            return task;
        } else {
            return NULL;
        }
    }
}

// Ends the run: frees tasks, closes the file. The bot stays where it is with its
// position known (we only ever stop between movement increments).
void Runner::finishRun(Outcome outcome) {
    finishedElapsedMs = getElapsedMs();
    if (currentTask) {
        delete currentTask;
        currentTask = nullptr;
    }
    for (auto &task : finishingSequence) {
        if (task) {
            delete task;
            task = nullptr;
        }
    }
    openedFile.close();
    pauseRequested = resumeRequested = stopRequested = false;
    paused = resuming = false;
    lastOutcome = outcome;
    stopped = true;

    unsigned long s = finishedElapsedMs / 1000;
    if (outcome == Outcome::Finished) {
        // Back home: save the position so the next session can resume without re-homing.
        persist.savePosition(movement);
        Log.printf("Drawing finished in %lum %lus\n", s / 60, s % 60);
        display->displayText("Done");
    } else {
        Log.printf("Drawing stopped at %d%% after %lum %lus\n", progress < 0 ? 0 : progress, s / 60, s % 60);
        display->displayText("Stopped");
    }
}

void Runner::run()
{
    if (stopped)
    {
        return;
    }

    // Pause / stop: let the current increment or pen move finish, then lift the pen.
    if (pauseRequested || stopRequested) {
        if (!paused) {
            if (movement->isMoving() || pen->isMoving()) {
                return;
            }
            paused = true;
            pauseRequested = true;
            pauseStartMs = millis();
            penWasDown = pen->isDown();
            if (penWasDown) {
                pen->up();
            }
            Log.printf("Drawing paused at %d%%%s\n", progress < 0 ? 0 : progress, penWasDown ? " (pen lifted)" : "");
        }

        if (stopRequested) {
            if (pen->isMoving()) {
                return;
            }
            finishRun(Outcome::Stopped);
            return;
        }

        if (!resumeRequested) {
            return; // paused
        }

        // Resume
        pausedTotalMs += millis() - pauseStartMs;
        paused = false;
        pauseRequested = false;
        resumeRequested = false;
        if (penWasDown) {
            pen->down();
            resuming = true;
        }
        Log.println("Drawing resumed");
        return;
    }

    if (resuming) {
        if (pen->isMoving()) {
            return; // wait for the pen to be back on the wall
        }
        resuming = false;
    }

    if (currentTask->isDone())
    {
        if (currentTask->name() == InterpolatingMovementTask::NAME) {
            auto distanceCovered = Movement::distanceBetweenPoints(startPosition, targetPosition);
            distanceSoFar += distanceCovered;
            startPosition = targetPosition;
            auto newProgress = int(floor(distanceSoFar / totalDistance * 100));
            if (newProgress > 100) {
                newProgress = 100;
            }
            if (progress != newProgress) {
                Log.println("Progress: " + String(newProgress));
                progress = newProgress;
                display->displayText(String(progress) + "%");
            }

        }
        delete currentTask;
        currentTask = getNextTask();
        if (currentTask != NULL)
        {
            currentTask->startRunning();
        }
        else
        {
            finishRun(Outcome::Finished);
        }
    }
}

void Runner::dryRun() {
    initTaskProvider();
    auto task = getNextTask();
    auto index = 1;
    while (task != NULL) {
        index = index + 1;
        delete task;
        task = getNextTask();
    }
    Log.println("All done");
}
