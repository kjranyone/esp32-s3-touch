#pragma once
#include <stdint.h>
#include <emscripten.h>

static inline uint32_t esp_random() {
    return (uint32_t)EM_ASM_INT({
        if (typeof crypto !== 'undefined' && crypto.getRandomValues) {
            var a = new Uint32Array(1);
            crypto.getRandomValues(a);
            return a[0] | 0;
        }
        return (Math.random() * 0xFFFFFFFF) | 0;
    });
}
