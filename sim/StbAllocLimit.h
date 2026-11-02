#pragma once

/* Force-included into FreeInkBook's TtfFont.cpp in the simulator: makes any
 * stb_truetype allocation larger than the ESP32-C3 heap can reliably offer
 * fail loudly here instead of only on the device (it crashed there with an
 * upstream stb_truetype asking for ~56 KB in one block per glyph). */
#include <stdio.h>
#include <stdlib.h>

/* 16 KB, not the device's real ~30 KB: host pointers are 64-bit, so stb picks
 * different chunk sizes here (stock: 800 x 32 B on the host vs 2000 x 28 B on
 * the device). With the patched header no allocation comes near this. */
#define INKLING_SIM_STB_MAX_ALLOC (16 * 1024)

static inline void* inkling_sim_stbtt_malloc(size_t n) {
  if (n > INKLING_SIM_STB_MAX_ALLOC) {
    fprintf(stderr, "[sim] stb_truetype malloc of %zu bytes: too big for the ESP32-C3 heap\n", n);
    abort();
  }
  return malloc(n);
}

#define STBTT_malloc(x, u) ((void)(u), inkling_sim_stbtt_malloc(x))
#define STBTT_free(x, u) ((void)(u), free(x))
