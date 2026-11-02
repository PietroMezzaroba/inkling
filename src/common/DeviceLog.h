#pragma once

#include <cstddef>

// Firmware log, shared by the reader and the updater:
// - USB serial when a cable is attached (INKLING_SERIAL_LOG);
// - a ring in RTC memory that survives software restarts (not power cuts);
// - optionally a file on the SD card, written and synced line by line, so a
//   crash during boot still leaves its last steps on the card even when the
//   bootloader then rolls back to the other firmware.
namespace devlog {

// Call once at boot, before any log line: keeps the ring after a restart,
// clears it after a cold boot, and records why the chip reset.
void begin(const char* appName);

void printf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

// Starts mirroring to `path` on the SD card (SD must be up): writes this
// boot's lines so far, then every new line, synced. The file is restarted
// once it grows past a few tens of KB.
void attachFile(const char* path);
// Stops mirroring (call once boot is done: no SD writes while reading).
void detachFile();

// Copies the ring, oldest line first, into `out` (NUL-terminated). Returns the
// number of bytes written, excluding the terminator.
size_t snapshot(char* out, size_t cap);

}  // namespace devlog

#define LOG(fmt, ...) devlog::printf(fmt, ##__VA_ARGS__)
