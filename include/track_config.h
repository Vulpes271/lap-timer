#pragma once

namespace Timing {
struct TrackConfig {
  const char* name;
  const char* sourceName;
  double latitude, longitude;
  double forwardEast, forwardNorth;
  double halfWidthM;
  double rearmDistanceM;
  double minSpeedKmh;
  unsigned int minLapMs, maxLapMs;
};
}
