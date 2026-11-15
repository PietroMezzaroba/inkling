#include "inkling/Orientation.h"

#include <cmath>

namespace inkling {

const char* orientationName(Orientation o) {
  switch (o) {
    case Orientation::Portrait:
      return "0";
    case Orientation::LandscapeClockwise:
      return "90";
    case Orientation::PortraitInverted:
      return "180";
    case Orientation::LandscapeCounterClockwise:
      return "270";
  }
  return "?";
}

bool OrientationTracker::classify(float ax, float ay, float az, bool allowLandscape, Orientation& out) {
  if (std::fabs(az) > kFlatZ) return false;
  const float absX = std::fabs(ax);
  const float absY = std::fabs(ay);
  if (absY >= kTiltThreshold && absY > absX + kAxisMargin) {
    out = ay < 0.0f ? Orientation::Portrait : Orientation::PortraitInverted;
    return true;
  }
  if (allowLandscape && absX >= kTiltThreshold && absX > absY + kAxisMargin) {
    const float x = ax * kLandscapeSign;
    out = x < 0.0f ? Orientation::LandscapeClockwise : Orientation::LandscapeCounterClockwise;
    return true;
  }
  return false;
}

bool OrientationTracker::update(float ax, float ay, float az, bool allowLandscape, Orientation current,
                                Orientation& out) {
  Orientation detected;
  if (!classify(ax, ay, az, allowLandscape, detected) || detected == current) {
    hold_ = 0;
    return false;
  }
  if (hold_ == 0 || detected != candidate_) {
    candidate_ = detected;
    hold_ = 1;
    return false;
  }
  if (++hold_ < kSettleSamples) return false;
  hold_ = 0;
  out = detected;
  return true;
}

}  // namespace inkling
