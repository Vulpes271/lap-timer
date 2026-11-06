#include "../src/paddock_state.h"
#include "../src/lap_timing.h"
#include <cassert>
#include <cstdio>
#include <limits>

int main() {
  unsigned checks=0;
  auto check=[&](bool ok){ assert(ok); ++checks; };
  auto fix=[](PaddockState& s,uint32_t now,double meters=0,double speed=0,bool valid=true) {
    s.observe(now,valid,46.0+meters/111320.0,14.0,speed);
  };
  PaddockState jitter;
  for(uint32_t t=0;t<30000;t+=200) fix(jitter,t,(t%400)?1.99:0);
  check(!jitter.stopped);
  fix(jitter,30000,1.99); check(jitter.stopped);
  fix(jitter,30200,0,8); check(!jitter.stopped);
  for(uint32_t t=30400;t<=60200;t+=200) fix(jitter,t);
  check(jitter.stopped);

  PaddockState drift;
  for(uint32_t t=0;t<=40000;t+=200) fix(drift,t,t/10000.0);
  check(!drift.stopped); // Small steps cannot drag the stationary anchor.

  PaddockState gap;
  fix(gap,0); fix(gap,29000); gap.poll(31000); fix(gap,32000);
  check(!gap.stopped);
  for(uint32_t t=32200;t<=62000;t+=200) fix(gap,t);
  check(gap.stopped);
  fix(gap,63000,0,0,false); check(gap.stopped); // No false departure.

  PaddockState invalid;
  fix(invalid,0); fix(invalid,29000,0,0,false); fix(invalid,30000);
  check(!invalid.stopped);
  invalid.observe(60000,true,std::numeric_limits<double>::quiet_NaN(),14,0);
  check(!invalid.stopped);

  PaddockState moving;
  for(uint32_t t=0;t<=40000;t+=200) fix(moving,t,0,20);
  check(!moving.stopped);

  PaddockState rollover;
  const uint32_t start=UINT32_MAX-15000;
  for(uint32_t t=0;t<=29800;t+=200) fix(rollover,start+t);
  fix(rollover,start+29999U); check(!rollover.stopped);
  fix(rollover,start+30000U); check(rollover.stopped);

  const Timing::TrackConfig track={"test","test",46,14,0,1,20,15,10,30000,600000,nullptr,0};
  Timing::LapTimer timer(track);
  auto crossingFix=[](double north,uint32_t t) { return Timing::Fix{46+north/111320.0,14,t,t,90,1,8,true}; };
  timer.process(crossingFix(-20,1000)); timer.process(crossingFix(-2,1800));
  check(timer.process(crossingFix(3,2000))==Timing::Event::Started);
  check(timer.interrupt()==Timing::Event::Interrupted && !timer.running());
  timer.process(crossingFix(-20,3000)); timer.process(crossingFix(-2,3800));
  check(timer.process(crossingFix(3,4000))==Timing::Event::Started && timer.completedLaps()==0);
  std::printf("Paddock state: %u checks passed\n",checks);
}
