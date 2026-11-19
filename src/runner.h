#ifndef Runner_h
#define Runner_h
#include "movement.h"
#include "tasks/task.h"
#include "pen.h"
#include "display.h"
#include "LittleFS.h"

// Executes the uploaded /commands file one line at a time.
//
// Pause/stop are requested from the web server task (flags only) and carried out
// here in loop(): the current <=1 mm movement increment or pen move is allowed to
// finish, then the pen is lifted. Resume lowers the pen again if it was down and
// carries on from the same point. Stop ends the drawing where the bot is; its
// position stays known so it can be moved home or started again.
class Runner {
    public:
    enum class Outcome { None, Finished, Stopped, Failed };

    private:
    Movement *movement;
    Pen *pen;
    Display *display;
    void initTaskProvider();
    Task* getNextTask();
    void finishRun(Outcome outcome);
    Task* currentTask = nullptr;
    bool stopped;
    File openedFile;
    double totalDistance = 0;
    double distanceSoFar = 0;
    Movement::Point startPosition;
    Movement::Point targetPosition;
    int progress;
    Task *finishingSequence[1] = {nullptr};
    int sequenceIx = 0;

    // pause / stop (requested from the web task, acted on in run())
    volatile bool pauseRequested = false;
    volatile bool resumeRequested = false;
    volatile bool stopRequested = false;
    bool paused = false;
    bool resuming = false;           // waiting for the pen to go back down
    bool penWasDown = false;

    unsigned long startMs = 0;
    unsigned long pauseStartMs = 0;
    unsigned long pausedTotalMs = 0;
    unsigned long finishedElapsedMs = 0;
    Outcome lastOutcome = Outcome::None;

    public:
    Runner(Movement *movement, Pen *pen, Display *display);
    void start();
    void run();
    void dryRun();

    void requestPause();
    void requestResume();
    void requestStop();

    bool isRunning() { return !stopped; }       // true while drawing, including paused
    bool isPaused() { return paused; }
    const char *stateName();                    // idle / drawing / pausing / paused / resuming / stopping
    int getProgress() { return progress; }
    double getDistanceSoFar() { return distanceSoFar; }
    double getTotalDistance() { return totalDistance; }
    unsigned long getElapsedMs();               // drawing time, excluding pauses
    Outcome getLastOutcome() { return lastOutcome; }
    const char *lastOutcomeName();
};
#endif
