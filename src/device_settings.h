#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <math.h>

class DeviceSettings {
 public:
  float leanZeroDegrees = 0;
  bool greenPreview = false;  // Enable explicitly with G; timing starts neutral.

  void begin() {
    Preferences preferences;
    if (preferences.begin("lap-timer", true)) {
      const float stored = preferences.getFloat("lean-zero", 0);
      if (isfinite(stored) && fabsf(stored) <= 180) leanZeroDegrees = stored;
      preferences.end();
    }
  }

  bool setLeanZero(float degrees) {
    if (!isfinite(degrees) || fabsf(degrees) > 180) return false;
    Preferences preferences;
    if (!preferences.begin("lap-timer", false)) return false;
    const bool saved = preferences.putFloat("lean-zero", degrees) == sizeof(float);
    preferences.end();
    if (saved) leanZeroDegrees = degrees;
    return saved;
  }

  float correctedLean(float rawDegrees) const {
    float result = rawDegrees - leanZeroDegrees;
    if (result > 180) result -= 360;
    if (result < -180) result += 360;
    return result;
  }
};
