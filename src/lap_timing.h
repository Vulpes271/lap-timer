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
enum class Event { None, Started, LapCompleted, SectorCompleted, LapRestarted, Interrupted };

class LapTimer {
 public:
  explicit LapTimer(const TrackConfig& track) : track_(track) {}
  bool running() const { return running_; }
  unsigned int lapNumber() const { return completed_ + (running_ ? 1 : 0); }
  unsigned int completedLaps() const { return completed_; }
  double lastLapMs() const { return lastLapMs_; }
  double bestLapMs() const { return bestLapMs_; }
  double lastCrossingMs() const { return lastCrossingMs_; }
  uint32_t lastCrossingReceivedMs() const { return lastReceivedMs_ - uint32_t(clockMs_ - lastCrossingMs_); }
  const TrackConfig& track() const { return track_; }
  void selectTrack(const TrackConfig& track) { *this = LapTimer(track); }
  unsigned int currentSector() const { return running_ ? nextSplit_ + 1 : 0; }
  double lastSectorMs(unsigned int sector) const { return sector < 3 ? lastSectors_[sector] : 0; }
  bool deltaValid() const { return deltaValid_; }
  double deltaToBestMs() const { return deltaMs_; }
  double currentCumulativeMs() const { return sectorStartedMs_ - lapStartedMs_; }
  void loadReference(double lap, const double* sectors) {
    if (running_) return;
    bestLapMs_ = lap;
    for (unsigned i=0;i<3;++i) bestSectors_[i]=sectors[i];
  }

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
      } else {
        if (running_ && processSplit(east, north, fix.speedKmh, dt)) event = Event::SectorCompleted;
        if (armed_ && previousDistance_ < 0 && distance >= 0 &&
                 fix.speedKmh >= track_.minSpeedKmh && previousSpeed_ >= track_.minSpeedKmh) {
          const double forwardSpeed = (distance - previousDistance_) * 1000.0 / dt;
          const double fraction = -previousDistance_ / (distance - previousDistance_);
          const double crossingAcross = previousAcross_ + fraction * (across - previousAcross_);
          if (forwardSpeed >= track_.minSpeedKmh / 3.6 && std::fabs(crossingAcross) <= track_.halfWidthM) {
            const double crossing = clockMs_ - dt + fraction * dt;
            const double duration = crossing - lapStartedMs_;
            if (!running_ || duration >= track_.minLapMs) {
              if (running_) {
                if (nextSplit_ == track_.splitCount) {
                  currentSectors_[nextSplit_] = crossing - sectorStartedMs_;
                  lastLapMs_ = duration;
                  const bool newBest = bestLapMs_ == 0 || duration < bestLapMs_;
                  if (newBest) bestLapMs_ = duration;
                  for (unsigned int i = 0; i <= track_.splitCount; ++i) {
                    lastSectors_[i] = currentSectors_[i];
                    if (newBest) bestSectors_[i] = currentSectors_[i];
                  }
                  ++completed_;
                  event = Event::LapCompleted;
                } else event = Event::LapRestarted;  // Missed split: restart without recording a lap.
              } else event = Event::Started;
              running_ = true;
              lapStartedMs_ = lastCrossingMs_ = crossing;
              sectorStartedMs_ = crossing;
              nextSplit_ = 0; splitArmed_ = false; deltaValid_ = false;
            }
          }
          // Any directed crossing consumes the latch; require a new approach.
          armed_ = false;
        }
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
  bool processSplit(double east, double north, double speed, uint32_t dt) {
    if (nextSplit_ >= track_.splitCount || nextSplit_ >= 2 || !track_.splits) return false;
    const Gate& gate = track_.splits[nextSplit_];
    double gx, gy;
    local(gate.latitude, gate.longitude, gx, gy);
    const double a = (previousEast_ - gx) * gate.forwardEast + (previousNorth_ - gy) * gate.forwardNorth;
    const double b = (east - gx) * gate.forwardEast + (north - gy) * gate.forwardNorth;
    const double sideA = (previousEast_ - gx) * gate.forwardNorth - (previousNorth_ - gy) * gate.forwardEast;
    const double sideB = (east - gx) * gate.forwardNorth - (north - gy) * gate.forwardEast;
    if (splitArmed_ && a < 0 && b >= 0) {
      splitArmed_ = false;
      const double fraction = -a / (b - a);
      if (speed >= track_.minSpeedKmh && previousSpeed_ >= track_.minSpeedKmh &&
          (b - a) * 1000 / dt >= track_.minSpeedKmh / 3.6 &&
          std::fabs(sideA + fraction * (sideB - sideA)) <= gate.halfWidthM) {
        const double crossing = clockMs_ - dt + fraction * dt;
        currentSectors_[nextSplit_] = crossing - sectorStartedMs_;
        sectorStartedMs_ = crossing;
        ++nextSplit_;
        if (bestLapMs_ > 0) {
          double bestElapsed = 0;
          for (unsigned int i = 0; i < nextSplit_; ++i) bestElapsed += bestSectors_[i];
          deltaMs_ = crossing - lapStartedMs_ - bestElapsed;
          deltaValid_ = true;
        }
        return true;
      }
    }
    if (b <= -gate.rearmDistanceM && std::fabs(sideB) < 100 && speed >= track_.minSpeedKmh)
      splitArmed_ = true;
    return false;
  }
  Event breakContinuity() {
    const bool wasRunning = running_;
    running_ = false; havePrevious_ = false; armed_ = false;
    nextSplit_ = 0; splitArmed_ = false; deltaValid_ = false;
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
  unsigned int nextSplit_ = 0;
  bool splitArmed_ = false, deltaValid_ = false;
  double sectorStartedMs_ = 0, deltaMs_ = 0;
  double currentSectors_[3] = {}, lastSectors_[3] = {}, bestSectors_[3] = {};
};
}
