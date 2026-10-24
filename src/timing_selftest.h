#pragma once
#include <Arduino.h>
#include "lap_timing.h"

inline bool runTimingSelfTests(Stream& output) {
  const Timing::TrackConfig track = {"test", "synthetic", 44.710284, 12.214867,
                                     0, 1, 20, 15, 10, 30000, 600000, nullptr, 0};
  unsigned int passed = 0, failed = 0;
  auto check = [&](bool result, const char* name) {
    output.printf("TIMING TEST %s: %s\n", result ? "PASS" : "FAIL", name);
    result ? ++passed : ++failed;
  };
  auto fix = [&](double distance, double across, uint32_t utc, double speed = 90.0) {
    return Timing::Fix{track.latitude + distance / 111320.0,
      track.longitude + across / (111320.0 * std::cos(track.latitude * 0.017453292519943295)),
      utc, utc, speed, 1.0, 8, true};
  };
  auto approach = [&](Timing::LapTimer& timer, double across = 0, double speed = 90.0) {
    timer.process(fix(-20, across, 1000, speed));
    timer.process(fix(-2, across, 1800, speed));
    return timer.process(fix(3, across, 2000, speed));
  };
  Timing::LapTimer linear(track);
  check(approach(linear) == Timing::Event::Started, "directed crossing starts LAP 1");
  check(std::fabs(linear.lastCrossingMs() - 1880.0) < 0.01, "2m/5m interpolation gives 80ms");
  check(linear.lapNumber() == 1 && linear.completedLaps() == 0, "first crossing is not a completed lap");
  linear.process(fix(-2, 0, 2200));
  linear.process(fix(3, 0, 2400));
  check(linear.completedLaps() == 0, "near-line jitter cannot create a lap");
  check(linear.poll(4000) == Timing::Event::Interrupted && !linear.running(), "GPS silence invalidates active lap");
  Timing::LapTimer outside(track);
  check(approach(outside, 25) == Timing::Event::None && !outside.running(), "crossing beyond finite gate rejected");
  Timing::LapTimer stopped(track);
  check(approach(stopped, 0, 0) == Timing::Event::None && !stopped.running(), "stationary GPS cannot start timer");
  Timing::LapTimer reverse(track);
  reverse.process(fix(20, 0, 1000)); reverse.process(fix(2, 0, 1800));
  check(reverse.process(fix(-3, 0, 2000)) == Timing::Event::None && !reverse.running(), "reverse crossing rejected");
  Timing::LapTimer gap(track);
  gap.process(fix(-20, 0, 1000)); gap.process(fix(-2, 0, 1800));
  check(gap.process(fix(3, 0, 4000)) != Timing::Event::Started, "missing GPS samples are not bridged");
  Timing::LapTimer midnight(track);
  midnight.process(fix(-20, 0, 86399000)); midnight.process(fix(-2, 0, 86399800));
  check(midnight.process(fix(3, 0, 0)) == Timing::Event::Started &&
        std::fabs(midnight.lastCrossingMs() - 86399880.0) < 0.01, "UTC midnight unwrap");
  Timing::LapTimer poor(track);
  auto bad = fix(-20, 0, 1000); bad.hdop = 9;
  poor.process(bad); poor.process(fix(-2, 0, 1800));
  check(poor.process(fix(3, 0, 2000)) != Timing::Event::Started, "poor fix cannot arm crossing");
  Timing::LapTimer oval(track);
  // Two full continuous synthetic laps, with a second crossing outside the gate.
  for (int i = -15; i <= 601; ++i) {
    const double angle = i * 0.2 * 6.283185307179586 / 60.0;
    oval.process(fix(100 * std::sin(angle), 100 * (std::cos(angle) - 1), 100000 + i * 200, 40));
  }
  check(oval.completedLaps() == 2 && oval.lapNumber() == 3, "two full laps counted, outside crossings ignored");
  check(std::fabs(oval.lastLapMs() - 60000) < 0.1 && std::fabs(oval.bestLapMs() - 60000) < 0.1,
        "last and best lap use GPS epoch time");
  const unsigned int previous = oval.lapNumber();
  oval.process(fix(0, 0, 220200, 40));
  check(oval.lapNumber() == previous, "duplicate UTC epoch ignored");
  Timing::Gate splits[2];
  for (int i = 0; i < 2; ++i) {
    const double theta = (i + 1) * 6.283185307179586 / 3;
    const auto point = fix(100 * std::sin(theta), 100 * (std::cos(theta) - 1), 0);
    splits[i] = {point.latitude, point.longitude, -std::sin(theta), std::cos(theta), 20, 15};
  }
  auto sectorTrack = track;
  sectorTrack.splits = splits; sectorTrack.splitCount = 2;
  Timing::LapTimer sectors(sectorTrack);
  auto drive = [&](int first, int last, double period, uint32_t base) {
    for (int i = first; i <= last; ++i) {
      const double theta = i * .2 * 6.283185307179586 / period;
      sectors.process(fix(100 * std::sin(theta), 100 * (std::cos(theta) - 1), base + i * 200, 40));
    }
  };
  drive(-15, 301, 60, 100000);
  check(sectors.completedLaps() == 1, "ordered sector gates permit a complete lap");
  check(std::fabs(sectors.lastSectorMs(0) - 20000) < 1 &&
        std::fabs(sectors.lastSectorMs(1) - 20000) < 1 &&
        std::fabs(sectors.lastSectorMs(2) - 20000) < 1, "three sectors sum to lap time");
  Timing::LapTimer slower = sectors;
  for (int i = 2; i <= 106; ++i) {
    const double theta = i * .2 * 6.283185307179586 / 63;
    slower.process(fix(100 * std::sin(theta), 100 * (std::cos(theta) - 1), 160000 + i * 200, 40));
  }
  check(slower.deltaValid() && slower.deltaToBestMs() > 0, "slower sector produces positive red delta");
  // Start second lap continuously, then accelerate: split comparison must turn green.
  drive(2, 70, 60, 160000);
  // Remaining angle reaches first split early by increasing angular progress.
  for (int i = 71; i <= 101; ++i) {
    const double theta = (i + 5) * .2 * 6.283185307179586 / 60;
    sectors.process(fix(100 * std::sin(theta), 100 * (std::cos(theta) - 1), 160000 + i * 200, 40));
  }
  check(sectors.deltaValid() && sectors.deltaToBestMs() < 0, "sector comparison uses best lap reference");
  Timing::LapTimer missed(sectorTrack);
  for (int i = -15; i <= 301; ++i) {
    const double theta = i * .2 * 6.283185307179586 / 60;
    // Miss the first gate by moving beyond its finite width.
    const double offset = i > 70 && i < 130 ? 60 * std::sin((i - 70) * 3.141592653589793 / 60) : 0;
    missed.process(fix(100 * std::sin(theta), 100 * (std::cos(theta) - 1) + offset, 100000 + i * 200, 40));
  }
  check(missed.completedLaps() == 0, "missing sector prevents recording an incomplete lap");
  sectors.selectTrack(track);
  check(!sectors.running() && sectors.completedLaps() == 0 && !sectors.deltaValid(), "changing circuit resets timing and reference");
  output.printf("TIMING TEST RESULT: %u passed, %u failed\n", passed, failed);
  return failed == 0;
}
