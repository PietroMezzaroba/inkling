#pragma once

#include <FreeInkUICore.h>
#include <render/PageRenderer.h>

#include <cstdint>

namespace inkling {

using freeink::ui::Orientation;

// Physical layout of the X3 in each orientation (see the transforms in
// FreeInkUI's DisplayTarget):
//   Portrait                  0°   front keys at the bottom (factory default)
//   LandscapeClockwise        90°  device turned clockwise: front keys on the LEFT
//   PortraitInverted          180° front keys at the top
//   LandscapeCounterClockwise 270° device turned counter-clockwise: keys on the RIGHT
inline bool isLandscape(Orientation o) {
  return o == Orientation::LandscapeClockwise || o == Orientation::LandscapeCounterClockwise;
}

// Page rotation matching the UI orientation: FreeInkBook's FrameRotation uses
// the same panel transforms as DisplayTarget (None = panel native).
inline freeink::book::FrameRotation frameRotationFor(Orientation o) {
  switch (o) {
    case Orientation::PortraitInverted:
      return freeink::book::FrameRotation::PortraitInverted;
    case Orientation::LandscapeClockwise:
      return freeink::book::FrameRotation::UpsideDown;
    case Orientation::LandscapeCounterClockwise:
      return freeink::book::FrameRotation::None;
    case Orientation::Portrait:
    default:
      return freeink::book::FrameRotation::Portrait;
  }
}

const char* orientationName(Orientation o);

// Turns accelerometer samples (in g) into a settled orientation.
//
// Portrait reads ay ~ -1 g with the device upright (the convention the 0°/180°
// auto-rotation has always used on the X3). The landscape side is NOT yet
// verified on hardware: if "Auto 4 lati" picks the upside-down landscape, flip
// kLandscapeSign (the diagnostic screen shows the live X value).
class OrientationTracker {
 public:
  static constexpr float kLandscapeSign = 1.0f;
  static constexpr float kTiltThreshold = 0.55f;  // g on the dominant axis
  static constexpr float kAxisMargin = 0.20f;     // dominant must beat the other axis by this
  static constexpr float kFlatZ = 0.80f;          // |az| above this: lying flat, keep current
  static constexpr uint8_t kSettleSamples = 4;    // consecutive samples (~800 ms at 5 Hz)

  // One-shot classification. Returns false when the reading is ambiguous
  // (flat on a table, diagonal, or landscape not allowed).
  static bool classify(float ax, float ay, float az, bool allowLandscape, Orientation& out);

  // Debounced: returns true (and sets `out`) once a new orientation, different
  // from `current`, has been read kSettleSamples times in a row.
  bool update(float ax, float ay, float az, bool allowLandscape, Orientation current, Orientation& out);
  void reset() { hold_ = 0; }

 private:
  Orientation candidate_ = Orientation::Portrait;
  uint8_t hold_ = 0;
};

}  // namespace inkling
