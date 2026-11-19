#ifndef Diagnostics_h
#define Diagnostics_h
#include <ESPAsyncWebServer.h>
#include "movement.h"
#include "pen.h"
#include "runner.h"
#include "display.h"
#include "controller.h"

#define MURAL_FW_VERSION "1.1-ux.3"

// Registers troubleshooting endpoints:
//   GET  /diagnostics        JSON snapshot (system, servo, motors, storage)
//   GET  /log                recent log lines (text)
//   POST /servo              action=up|down|angle|release|cycle|stop [value=]
//   POST /servoSettings      up, down, min, max, speed, releaseWhenUp
//   POST /diagSettings       logDrawingMoves=0|1, webWhileDrawing=0|1
//   POST /uploadPreview      stores the drawing preview next to the commands
//   GET  /savedPreview       returns the stored preview
void registerDiagnosticsRoutes(AsyncWebServer *server, Movement *movement, Pen *pen, Runner *runner,
                               Controller *controller, Display *display);
#endif
