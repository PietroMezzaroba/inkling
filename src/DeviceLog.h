#pragma once

#include <cstddef>

// Firmware log: USB serial when a cable is attached, plus a ring buffer in RTC
// memory that survives software restarts (not power cuts). The Wi-Fi update
// mode serves it over HTTP, so logs from the last reading session can be read
// without a cable.
namespace devlog {

// Call once at boot, before any log line: keeps the ring after a restart,
// clears it after a cold boot, and records why the chip reset.
void begin();

void printf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

// Copies the ring, oldest line first, into `out` (NUL-terminated). Returns the
// number of bytes written, excluding the terminator.
size_t snapshot(char* out, size_t cap);

}  // namespace devlog

#define LOG(fmt, ...) devlog::printf(fmt, ##__VA_ARGS__)
