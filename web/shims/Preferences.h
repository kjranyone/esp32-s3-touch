#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <emscripten.h>

class Preferences {
    char ns_[32] = {};
public:
    bool begin(const char* name, bool /*readOnly*/ = false) {
        if (!name) return false;
        strncpy(ns_, name, sizeof(ns_) - 1);
        ns_[sizeof(ns_) - 1] = '\0';
        return true;
    }
    void end() {}

    size_t putBytes(const char* key, const void* value, size_t len) {
        if (!key || !value || len == 0) return 0;
        int ok = EM_ASM_INT({
            try {
                var ns = UTF8ToString($0);
                var k = UTF8ToString($1);
                var ptr = $2;
                var n = $3;
                var hex = "";
                for (var i = 0; i < n; i++) {
                    var b = HEAPU8[ptr + i];
                    hex += (b < 16 ? "0" : "") + b.toString(16);
                }
                localStorage.setItem('esp32_nvs:' + ns + ':' + k, hex);
                return n;
            } catch (e) {
                return 0;
            }
        }, ns_, key, value, (int)len);
        return (size_t)ok;
    }

    size_t getBytes(const char* key, void* buf, size_t maxLen) {
        if (!key || !buf || maxLen == 0) return 0;
        int got = EM_ASM_INT({
            try {
                var ns = UTF8ToString($0);
                var k = UTF8ToString($1);
                var ptr = $2;
                var maxN = $3;
                var hex = localStorage.getItem('esp32_nvs:' + ns + ':' + k);
                if (!hex || (hex.length % 2) !== 0) return 0;
                var n = Math.min(maxN, hex.length >> 1);
                for (var i = 0; i < n; i++) {
                    HEAPU8[ptr + i] = parseInt(hex.substr(i * 2, 2), 16);
                }
                return n;
            } catch (e) {
                return 0;
            }
        }, ns_, key, buf, (int)maxLen);
        return (size_t)got;
    }
};
