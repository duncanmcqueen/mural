#include "svgselectphase.h"
#include "LittleFS.h"
#include "persist.h"
#include "logger.h"

SvgSelectPhase::SvgSelectPhase(PhaseManager* manager) {
    this->manager = manager;
}

void SvgSelectPhase::handleUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final)
{
    if (!index)
    {
        // The old drawing is about to be overwritten - don't offer it for re-use any more.
        persist.invalidateDrawing();
        if (LittleFS.exists("/commands")) {
            LittleFS.remove("/commands");
        }

        Log.printf("%d bytes total, %d bytes free\n",  LittleFS.totalBytes(), LittleFS.totalBytes() - LittleFS.usedBytes());
        Log.printf("Upload size: %d bytes\n", request->contentLength());

        if (LittleFS.totalBytes() -  LittleFS.usedBytes() < request->contentLength()) {
            Log.println("Not enough space on LittleFS");
            request->send(400, "text/plain", "Not enough space for upload");
            return;
        }

        request->_tempFile = LittleFS.open("/commands", "w");
        Log.println("Upload started");
    }

    if (len && request->_tempFile)
    {
        // stream the incoming chunk to the opened file
        request->_tempFile.write(data, len);
    }

    if (final)
    {
        if (!request->_tempFile) {
            Log.println("Upload failed - file was never opened");
            return;
        }
        request->_tempFile.close();
        Log.println("Upload finished");

        // Promote the preview that the UI uploaded just before the commands.
        if (LittleFS.exists("/preview.txt")) {
            LittleFS.remove("/preview.txt");
        }
        if (LittleFS.exists("/preview.tmp")) {
            LittleFS.rename("/preview.tmp", "/preview.txt");
        }

        String name = request->hasParam("name") ? request->getParam("name")->value() : String("drawing");
        persist.saveDrawing(manager->getMovement()->getTopDistance(), name);

        manager->setDrawingReady(true);
        manager->advance();
    }
}

const char* SvgSelectPhase::getName() {
    return "SvgSelect";
}
