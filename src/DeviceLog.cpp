#include "DeviceLog.h"

#include <Arduino.h>
#include <esp_attr.h>
#include <esp_system.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace devlog {

namespace {

constexpr uint32_t kMagic = 0x474F4C49;  // "ILOG"
constexpr size_t kRingSize = 3072;

// RTC_NOINIT: left untouched by restarts, garbage after a power cut (hence
// the magic and the bounds check on `head`).
struct Ring {
  uint32_t magic;
  uint32_t head;     // next write position
  uint32_t wrapped;  // 1 once the ring has filled up
  char data[kRingSize];
};
RTC_NOINIT_ATTR Ring ring;

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
}

}  // namespace

void begin() {
  const esp_reset_reason_t reason = esp_reset_reason();
  const bool survives = reason != ESP_RST_POWERON && reason != ESP_RST_BROWNOUT;
  if (!survives || ring.magic != kMagic || ring.head >= kRingSize) {
    ring.magic = kMagic;
    ring.head = 0;
    ring.wrapped = 0;
  }
  printf("---- boot, reset reason: %s", resetReasonName(reason));
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
#if INKLING_SERIAL_LOG
  Serial.write(reinterpret_cast<const uint8_t*>(line), len + 1);
#endif
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
