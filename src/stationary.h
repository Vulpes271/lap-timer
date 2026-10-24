#pragma once
#include <Arduino.h>
#include <math.h>

struct RunningStats {
  uint32_t count = 0;
  double mean = 0, m2 = 0, maximum = 0;
  void add(double value) {
    ++count;
    const double delta = value - mean;
    mean += delta / count;
    m2 += delta * (value - mean);
    if (count == 1 || value > maximum) maximum = value;
  }
  double sigma() const { return count > 1 ? sqrt(m2 / (count - 1)) : 0; }
};

struct StationaryStats {
  uint32_t readyAt = 0, imuErrors = 0;
  bool paused = true;
  uint32_t stoppedSeconds = 0;
  RunningStats accelNorm, gyro[3], east, north, speed;
  double originLat = 0, originLon = 0, drift = 0, maxDrift = 0;
  void reset(bool start = true) {
    *this = StationaryStats{};
    paused = !start;
    readyAt = millis() + 10000;
  }
  bool warming() const { return !paused && static_cast<int32_t>(millis() - readyAt) < 0; }
  uint32_t seconds() const { return paused ? stoppedSeconds : warming() ? 0 : (millis() - readyAt) / 1000; }
  void stop() { stoppedSeconds = seconds(); paused = true; }
  void addImu(float ax, float ay, float az, float gx, float gy, float gz) {
    if (paused || warming()) return;
    accelNorm.add(sqrt(ax * ax + ay * ay + az * az));
    gyro[0].add(gx); gyro[1].add(gy); gyro[2].add(gz);
  }
  void addGps(double lat, double lon) {
    if (paused || warming()) return;
    if (!east.count) { originLat = lat; originLon = lon; }
    // Local tangent approximation for a stationary test over a few metres.
    const double e = (lon - originLon) * 111320.0 * cos(originLat * DEG_TO_RAD);
    const double n = (lat - originLat) * 111320.0;
    east.add(e); north.add(n);
    drift = sqrt(e * e + n * n);
    if (drift > maxDrift) maxDrift = drift;
  }
  double spread() const { return hypot(east.sigma(), north.sigma()); }
};
