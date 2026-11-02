#include "DeviceLog.h"

#include <Arduino.h>
#include <SDCardManager.h>
#include <esp_attr.h>
#include <esp_system.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace devlog {

namespace {

constexpr uint32_t kMagic = 0x474F4C49;  // "ILOG"
constexpr size_t kRingSize = 3072;
constexpr size_t kMaxFileBytes = 32 * 1024;

// RTC_NOINIT: left untouched by restarts, garbage after a power cut (hence
// the magic and the bounds check on `head`).
struct Ring {
  uint32_t magic;
  uint32_t head;     // next write position
  uint32_t wrapped;  // 1 once the ring has filled up
  char data[kRingSize];
};
RTC_NOINIT_ATTR Ring ring;

// Ring position where this boot's lines start, and how many bytes this boot
// has written (to replay them into the file without the previous session's).
uint32_t bootStart = 0;
size_t bootBytes = 0;
// The previous session ended in a crash or watchdog: its lines are still in
// the ring and are the most valuable thing to put on the card.
bool previousCrashed = false;

FsFile file;
bool fileOpen = false;

const char* resetReasonName(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON: return "power-on";
    case ESP_RST_SW: return "software restart";
    case ESP_RST_PANIC: return "PANIC (crash)";
    case ESP_RST_INT_WDT: return "interrupt watchdog";
    case ESP_RST_TASK_WDT: return "task watchdog";
    case ESP_RST_WDT: return "watchdog";
    case ESP_RST_DEEPSLEEP: return "deep sleep wake";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_USB: return "usb";
    default: return "other";
  }
}

void append(const char* s, size_t n) {
  for (size_t i = 0; i < n; ++i) {
    ring.data[ring.head] = s[i];
    if (++ring.head >= kRingSize) {
      ring.head = 0;
      ring.wrapped = 1;
    }
  }
  bootBytes += n;
}

void writeFile(const char* s, size_t n) {
  if (!fileOpen) return;
  file.write(reinterpret_cast<const uint8_t*>(s), n);
  file.sync();  // a crash right after must not lose this line
}

}  // namespace

void begin(const char* appName) {
  const esp_reset_reason_t reason = esp_reset_reason();
  const bool survives = reason != ESP_RST_POWERON && reason != ESP_RST_BROWNOUT;
  if (!survives || ring.magic != kMagic || ring.head >= kRingSize) {
    ring.magic = kMagic;
    ring.head = 0;
    ring.wrapped = 0;
  }
  bootStart = ring.head;
  bootBytes = 0;
  previousCrashed = reason == ESP_RST_PANIC || reason == ESP_RST_INT_WDT || reason == ESP_RST_TASK_WDT ||
                    reason == ESP_RST_WDT;
  printf("---- %s %s boot, reset reason: %s", appName,
#ifdef INKLING_VERSION
         INKLING_VERSION,
#else
         "dev",
#endif
         resetReasonName(reason));
}

void printf(const char* fmt, ...) {
  char line[160];
  const int prefix = snprintf(line, sizeof(line), "[%lu] ", millis());
  va_list args;
  va_start(args, fmt);
  vsnprintf(line + prefix, sizeof(line) - prefix - 1, fmt, args);
  va_end(args);
  const size_t len = strlen(line);
  line[len] = '\n';
  append(line, len + 1);
  writeFile(line, len + 1);
#if INKLING_SERIAL_LOG
  Serial.write(reinterpret_cast<const uint8_t*>(line), len + 1);
#endif
}

// InklingCore's INKLING_LOG hook (see platformio.ini): same log as the firmware.
extern "C" void inkling_core_log(const char* fmt, ...) {
  char line[140];
  va_list args;
  va_start(args, fmt);
  vsnprintf(line, sizeof(line), fmt, args);
  va_end(args);
  printf("core: %s", line);
}

void attachFile(const char* path) {
  if (fileOpen || !SdMan.ready()) return;
  {
    FsFile probe = SdMan.open(path, O_RDONLY);
    if (probe && probe.fileSize() > kMaxFileBytes) {
      probe.close();
      SdMan.remove(path);
    }
  }
  file = SdMan.open(path, O_WRONLY | O_CREAT | O_APPEND);
  if (!file) return;
  fileOpen = true;

  if (previousCrashed) {
    // The whole ring: the crashed session's last lines, then this boot's.
    static const char kHeader[] = "==== previous session crashed; ring follows ====\n";
    file.write(reinterpret_cast<const uint8_t*>(kHeader), sizeof(kHeader) - 1);
    if (ring.wrapped) file.write(reinterpret_cast<const uint8_t*>(ring.data + ring.head), kRingSize - ring.head);
    file.write(reinterpret_cast<const uint8_t*>(ring.data), ring.head);
    file.sync();
    return;
  }

  // Replay this boot's lines logged before the SD card was up.
  const size_t n = bootBytes < kRingSize ? bootBytes : kRingSize;
  const size_t start = (bootStart + (bootBytes - n)) % kRingSize;
  if (start + n <= kRingSize) {
    file.write(reinterpret_cast<const uint8_t*>(ring.data + start), n);
  } else {
    file.write(reinterpret_cast<const uint8_t*>(ring.data + start), kRingSize - start);
    file.write(reinterpret_cast<const uint8_t*>(ring.data), n - (kRingSize - start));
  }
  file.sync();
}

void detachFile() {
  if (!fileOpen) return;
  file.close();
  fileOpen = false;
}

size_t snapshot(char* out, size_t cap) {
  if (cap == 0) return 0;
  size_t n = 0;
  auto put = [&](const char* s, size_t len) {
    const size_t take = len < cap - 1 - n ? len : cap - 1 - n;
    memcpy(out + n, s, take);
    n += take;
  };
  if (ring.wrapped) put(ring.data + ring.head, kRingSize - ring.head);
  put(ring.data, ring.head);
  out[n] = '\0';
  return n;
}

}  // namespace devlog
