#pragma once

#include <cstdint>

namespace inkling {

// Snapshot of the hardware as seen by the firmware. The board layer fills it on
// the device; the simulator fills it with fake values. Screens only read it.
struct DeviceStatus {
  const char* deviceName = "Xteink X3";
  const char* panelName = "?";

  bool clockValid = false;
  uint16_t year = 0;
  uint8_t month = 0;
  uint8_t day = 0;
  uint8_t hour = 0;
  uint8_t minute = 0;

  int16_t batteryPercent = -1;  // -1 = unknown
  bool charging = false;

  bool sdReady = false;

  bool imuReady = false;
  float ax = 0, ay = 0, az = 0;  // acceleration, g

  const char* lastButton = nullptr;  // nullptr until the first press
  uint32_t buttonPresses = 0;

  uint32_t freeHeap = 0;      // bytes, 0 = unknown (simulator)
  uint32_t largestBlock = 0;  // biggest single allocation still possible

  const char* otherFirmware = "";  // app in the other flash slot, "" if none
  const char* notice = nullptr;    // one-line message, e.g. a failed SD update
};

}  // namespace inkling
