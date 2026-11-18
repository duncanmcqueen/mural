#include "pentask.h"
PenTask::PenTask(bool up, Pen *pen) {
    this->up = up;
    this->pen = pen;
}

void PenTask::startRunning() {
    // Non-blocking: the pen ramps in Pen::update() from loop(); isDone() waits for it.
    if (up) {
        pen->up();
    } else {
        pen->down();
    }
}

bool PenTask::isDone() {
    return !pen->isMoving();
}
