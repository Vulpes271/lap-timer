#pragma once
#include <cmath>
#include <cstdint>

// Fixed anchor: small GPS steps must not accumulate into a moving anchor.
class PaddockState {
 public:
  bool stopped = false;
  void observe(uint32_t now, bool valid, double lat, double lon, double speed) {
    if (!valid || !std::isfinite(lat) || !std::isfinite(lon) || !std::isfinite(speed)) {
      anchored_ = false;
      return;  // Losing GPS never starts or closes a riding session.
    }
    if (anchored_ && uint32_t(now - lastFix_) > 1500) anchored_ = false;
    lastFix_ = now;
    const double east = (lon - lon_) * 111320.0 * std::cos(lat * 0.017453292519943295);
    const double north = (lat - lat_) * 111320.0;
    if (speed > 5.0 || !anchored_ || std::hypot(east, north) > 2.0) {
      lat_ = lat; lon_ = lon; since_ = now; anchored_ = true;
      // Position noise alone does not announce departure from the pits.
      if (speed > 5.0) stopped = false;
    } else if (uint32_t(now - since_) >= 30000) {
      stopped = true;
    }
  }
  void poll(uint32_t now) {
    if (anchored_ && uint32_t(now - lastFix_) > 1500) anchored_ = false;
  }
 private:
  bool anchored_ = false;
  uint32_t since_ = 0, lastFix_ = 0;
  double lat_ = 0, lon_ = 0;
};
