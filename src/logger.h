#ifndef Logger_h
#define Logger_h
#include <Arduino.h>

// Drop-in replacement for Serial printing: everything written to Log goes to the
// serial port as before, and is also kept in a RAM ring buffer so it can be read
// from the browser (/log) without a USB cable attached.
class EventLog : public Print {
    private:
    static const size_t CAPACITY = 8192;
    char buffer[CAPACITY];
    size_t head = 0;        // next write position
    bool wrapped = false;
    bool atLineStart = true;
    portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
    void putChar(char c);
    void putTimestamp();
    public:
    size_t write(uint8_t c) override;
    size_t write(const uint8_t *data, size_t size) override;
    using Print::write;
    // Returns the buffered log, oldest line first.
    String dump();
};

extern EventLog Log;
#endif
