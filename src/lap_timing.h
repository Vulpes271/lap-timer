#pragma once

#include <cmath>
#include <cstdint>
#include "track_config.h"

namespace Timing {
struct Fix {
  double latitude, longitude;
  uint32_t utcMs, receivedMs;
  double speedKmh, hdop;
  unsigned int satellites;
  bool valid;
};
enum class Event { None, Started, LapCompleted, Interrupted };

class LapTimer {
 public:
  explicit LapTimer(const TrackConfig& track) : track_(track) {}
  bool running() const { return running_; }
  unsigned int lapNumber() const { return completed_ + (running_ ? 1 : 0); }
  unsigned int completedLaps() const { return completed_; }
  double lastLapMs() const { return lastLapMs_; }
  double bestLapMs() const { return bestLapMs_; }
  double lastCrossingMs() const { return lastCrossingMs_; }
  const TrackConfig& track() const { return track_; }

  void local(double latitude, double longitude, double& east, double& north) const {
    constexpr double radians = 0.017453292519943295;
    east = (longitude - track_.longitude) * 111320.0 * std::cos(track_.latitude * radians);
    north = (latitude - track_.latitude) * 111320.0;
  }

  Event process(const Fix& fix) {
    if (!fix.valid || !std::isfinite(fix.latitude) || !std::isfinite(fix.longitude) ||
        !std::isfinite(fix.speedKmh) || !std::isfinite(fix.hdop) ||
        fix.satellites < 4 || fix.hdop <= 0 || fix.hdop > 3 || fix.utcMs >= dayMs) {
      return breakContinuity();
    }
    double east, north;
    local(fix.latitude, fix.longitude, east, north);
    if (std::hypot(east, north) > 1500) return breakContinuity();
    const double distance = east * track_.forwardEast + north * track_.forwardNorth;
    const double across = east * track_.forwardNorth - north * track_.forwardEast;
    uint32_t dt = 0;
    bool interrupted = false;
    if (!haveClock_) {
      clockMs_ = fix.utcMs;
      haveClock_ = true;
    } else {
      dt = (fix.utcMs + dayMs - previousUtc_) % dayMs;
      if (dt == 0) return Event::None;  // Duplicate navigation epoch.
      if (dt > 1000) interrupted = breakContinuity() == Event::Interrupted;
      clockMs_ += dt;
    }
    previousUtc_ = fix.utcMs;
    lastReceivedMs_ = fix.receivedMs;
    if (running_ && clockMs_ - lapStartedMs_ > track_.maxLapMs)
      interrupted = breakContinuity() == Event::Interrupted;

    Event event = interrupted ? Event::Interrupted : Event::None;
    if (havePrevious_) {
      const double step = std::hypot(east - previousEast_, north - previousNorth_);
      // Reject jumps >90 m/s, including a 5 m allowance for position noise.
      if (step > 90.0 * dt / 1000.0 + 5.0) {
        event = breakContinuity();
      } else if (armed_ && previousDistance_ < 0 && distance >= 0 &&
                 fix.speedKmh >= track_.minSpeedKmh && previousSpeed_ >= track_.minSpeedKmh) {
        const double forwardSpeed = (distance - previousDistance_) * 1000.0 / dt;
        const double fraction = -previousDistance_ / (distance - previousDistance_);
        const double crossingAcross = previousAcross_ + fraction * (across - previousAcross_);
        if (forwardSpeed >= track_.minSpeedKmh / 3.6 && std::fabs(crossingAcross) <= track_.halfWidthM) {
          const double crossing = clockMs_ - dt + fraction * dt;
          const double duration = crossing - lapStartedMs_;
          if (!running_ || duration >= track_.minLapMs) {
            if (running_) {
              lastLapMs_ = duration;
              if (bestLapMs_ == 0 || duration < bestLapMs_) bestLapMs_ = duration;
              ++completed_;
              event = Event::LapCompleted;
            } else event = Event::Started;
            running_ = true;
            lapStartedMs_ = lastCrossingMs_ = crossing;
          }
        }
        // Any directed crossing consumes the latch; require a new approach.
        armed_ = false;
      }
    }
    if (distance <= -track_.rearmDistanceM && std::fabs(across) < 300 &&
        fix.speedKmh >= track_.minSpeedKmh) armed_ = true;
    previousEast_ = east; previousNorth_ = north;
    previousDistance_ = distance; previousAcross_ = across;
    previousSpeed_ = fix.speedKmh;
    havePrevious_ = true;
    return event;
  }

  Event poll(uint32_t now) {
    if (havePrevious_ && static_cast<uint32_t>(now - lastReceivedMs_) > 1500)
      return breakContinuity();
    return Event::None;
  }

  uint32_t elapsedMs(uint32_t now) const {
    if (!running_) return 0;
    const uint32_t age = now - lastReceivedMs_;
    if (age > 1500) return 0;
    // Display extrapolation only; finish/lap times always use GPS epochs.
    return static_cast<uint32_t>(clockMs_ - lapStartedMs_ + age);
  }

 private:
  Event breakContinuity() {
    const bool wasRunning = running_;
    running_ = false; havePrevious_ = false; armed_ = false;
    return wasRunning ? Event::Interrupted : Event::None;
  }
  static constexpr uint32_t dayMs = 86400000;
  TrackConfig track_;
  bool running_ = false, armed_ = false, havePrevious_ = false, haveClock_ = false;
  uint32_t previousUtc_ = 0, lastReceivedMs_ = 0;
  uint64_t clockMs_ = 0;
  unsigned int completed_ = 0;
  double previousEast_ = 0, previousNorth_ = 0, previousDistance_ = 0;
  double previousAcross_ = 0, previousSpeed_ = 0;
  double lapStartedMs_ = 0, lastCrossingMs_ = 0, lastLapMs_ = 0, bestLapMs_ = 0;
};
}
