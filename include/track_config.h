#pragma once

namespace Timing {
struct Gate {
  double latitude, longitude, forwardEast, forwardNorth;
  double halfWidthM, rearmDistanceM;
};
struct TrackConfig {
  const char* name;
  const char* sourceName;
  double latitude, longitude;
  double forwardEast, forwardNorth;
  double halfWidthM;
  double rearmDistanceM;
  double minSpeedKmh;
  unsigned int minLapMs, maxLapMs;
  const Gate* splits;
  unsigned int splitCount;
};
}
