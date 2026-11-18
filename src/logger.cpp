#include "logger.h"

EventLog Log;

void EventLog::putChar(char c) {
    buffer[head] = c;
    head = (head + 1) % CAPACITY;
    if (head == 0) {
        wrapped = true;
    }
}

void EventLog::putTimestamp() {
    char stamp[16];
    unsigned long ms = millis();
    int n = snprintf(stamp, sizeof(stamp), "[%6lu.%03lu] ", ms / 1000, ms % 1000);
    for (int i = 0; i < n; i++) {
        putChar(stamp[i]);
    }
}

size_t EventLog::write(uint8_t c) {
    return write(&c, 1);
}

size_t EventLog::write(const uint8_t *data, size_t size) {
    Serial.write(data, size);

    portENTER_CRITICAL(&mux);
    for (size_t i = 0; i < size; i++) {
        char c = (char)data[i];
        if (c == '\r') {
            continue;
        }
        if (atLineStart) {
            putTimestamp();
            atLineStart = false;
        }
        putChar(c);
        if (c == '\n') {
            atLineStart = true;
        }
    }
    portEXIT_CRITICAL(&mux);
    return size;
}

String EventLog::dump() {
    // Copy out under the lock, build the String outside it (allocation can't happen in a critical section).
    char *copy = (char *)malloc(CAPACITY + 1);
    if (!copy) {
        return String("(not enough memory to read log)");
    }

    size_t length;
    portENTER_CRITICAL(&mux);
    if (wrapped) {
        size_t tail = CAPACITY - head;
        memcpy(copy, buffer + head, tail);
        memcpy(copy + tail, buffer, head);
        length = CAPACITY;
    } else {
        memcpy(copy, buffer, head);
        length = head;
    }
    bool skipPartialLine = wrapped;
    portEXIT_CRITICAL(&mux);

    copy[length] = '\0';
    char *start = copy;
    if (skipPartialLine) {
        // The oldest line was partially overwritten; start from the next full line.
        char *newline = strchr(copy, '\n');
        if (newline) {
            start = newline + 1;
        }
    }
    String result(start);
    free(copy);
    return result;
}
