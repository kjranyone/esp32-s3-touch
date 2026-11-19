#pragma once
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <emscripten.h>

extern "C" {
void web_serial_emit(const char* text);
}

class WebSerial {
public:
    void begin(unsigned long) {}
    void println() { web_serial_emit("\n"); }
    void println(const char* s) {
        if (s) web_serial_emit(s);
        web_serial_emit("\n");
    }
    void print(const char* s) {
        if (s) web_serial_emit(s);
    }
    int printf(const char* fmt, ...) __attribute__((format(printf, 2, 3))) {
        char buf[1024];
        va_list ap;
        va_start(ap, fmt);
        int n = vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        web_serial_emit(buf);
        return n;
    }
};

extern WebSerial Serial;

static inline unsigned long millis() {
    return (unsigned long)emscripten_get_now();
}

static inline unsigned long micros() {
    return (unsigned long)(emscripten_get_now() * 1000.0);
}

static inline void delay(unsigned long) {}
