#pragma once
#include <stdint.h>
#include <stdlib.h>

#ifndef MALLOC_CAP_INTERNAL
#define MALLOC_CAP_INTERNAL (1u << 0)
#endif
#ifndef MALLOC_CAP_8BIT
#define MALLOC_CAP_8BIT     (1u << 1)
#endif
#ifndef MALLOC_CAP_SPIRAM
#define MALLOC_CAP_SPIRAM   (1u << 2)
#endif

static inline void* heap_caps_malloc(size_t size, uint32_t) {
    return malloc(size);
}

static inline size_t heap_caps_get_largest_free_block(uint32_t) {
    return 8u * 1024u * 1024u;
}

static inline size_t heap_caps_get_free_size(uint32_t) {
    return 8u * 1024u * 1024u;
}
