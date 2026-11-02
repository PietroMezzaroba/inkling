#pragma once

// Force-included into simulator builds (-include): routes InklingCore's
// optional diagnostics to stderr.
#include <cstdio>

#define INKLING_LOG(...)                \
  do {                                  \
    std::fprintf(stderr, "[core] ");    \
    std::fprintf(stderr, __VA_ARGS__);  \
    std::fputc('\n', stderr);           \
  } while (0)
