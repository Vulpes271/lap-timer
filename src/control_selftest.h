#pragma once
#include <Arduino.h>
#include <memory>
#include "control_storage.h"
#include "terminal_reader.h"
#include "imu_estimate.h"
#include "quick_button.h"
#include "panel_selftest.h"

inline bool runControlSelfTests(Stream& out){
  using namespace Control;
  unsigned passed=0,failed=0;
  auto check=[&](bool condition,const char* name){out.printf("CONTROL TEST %s: %s\n",condition?"PASS":"FAIL",name);condition?++passed:++failed;};
  PressDebouncer button;button.begin(false,0);
  check(!button.update(true,10) && !button.update(true,39) && button.update(true,40),"GPIO32 press requires stable 30 ms");
  check(!button.update(true,500),"holding QUICK does not repeat");
  check(!button.update(false,510) && !button.update(true,520) && !button.update(true,550),"release bounce cannot rearm QUICK");
  button.update(false,600);button.update(false,630);button.update(true,650);
  check(button.update(true,680),"stable release permits the next QUICK press");
  button.begin(true,100);button.update(true,500);button.update(false,600);button.update(false,630);button.update(true,650);
  check(button.update(true,680),"startup-held button waits for release before use");
  button.begin(false,0xFFFFFFE0);button.update(true,0xFFFFFFF0);
  check(button.update(true,14),"button debounce tolerates millis rollover");
  std::unique_ptr<Model> model(new Model());auto& m=*model;
  m.input(Input::Quick,100);check(m.view(100)==View::LastLap,"quick opens LAST LAP");
  m.input(Input::Quick,4000);check(m.view(4000)==View::Rear,"second quick opens rear");
  check(m.view(6000)==View::Rear,"quick extends five second deadline");
  check(m.view(9000)==View::Race,"quick timeout returns base");
  m.base=Base::Camera;m.input(Input::Quick,10000);check(m.view(15000)==View::Camera,"quick returns prior CAMERA base");
  m=Model();m.input(Input::SettingsOn,0);m.input(Input::Right,0);const auto menu=m.menu;
  m.input(Input::Right,1);check(m.menu==menu,"held right does not repeat");
  m.input(Input::Left,2);check(m.menu==menu,"left needs return to CENTER");
  m.input(Input::Center,3);m.input(Input::Left,4);check(m.menu==0,"center rearms direction");
  m.input(Input::Quick,5);m.tap(true,6);check(m.editing && m.draft.base==1 && m.config.base==0,"editing is unconfirmed draft");
  m.input(Input::SettingsOff,7);check(!m.editing && m.config.base==0,"settings off discards unconfirmed edit");
  m.input(Input::SettingsOn,8);m.input(Input::Quick,9);m.tap(true,10);m.input(Input::Quick,11);
  check(m.config.base==1 && m.dirty,"QUICK confirms setting");
  Config c;c.frontMin=2;c.frontMax=3;c.rearMin=2;c.rearMax=3;
  std::strcpy(c.frontId,"SIM-F");std::strcpy(c.rearId,"SIM-R");c.pressureAlarm=true;c.confirmMs=1000;
  check(validConfig(c),"explicit range and identities enable alarms");
  auto invalid=c;invalid.frontMin=4;check(!validConfig(invalid),"inverted pressure range rejected");
  invalid=c;std::strcpy(invalid.rearId,"SIM-F");check(!validConfig(invalid),"same sensor cannot map to both wheels");
  const double original=c.frontMin;pressureValue(original,1);check(c.frontMin==original && std::fabs(pressureValue(1,1)-14.503773773)<1e-8,"unit conversion leaves physical limits unchanged");
  check(std::fabs(speedValue(160.9344,1)-100)<1e-8 && temperatureValue(0,1)==32 && std::fabs(accelValue(1,1)-9.80665)<1e-8,"speed temperature acceleration conversions");
  m=Model();m.config=c;m.setTest(true);
  auto packets=[&](float front,float rear,uint32_t now){m.pressurePacket(true,"SIM-F",front,25,80,m.time(now));m.pressurePacket(true,"SIM-R",rear,25,80,m.time(now));m.poll(now);};
  packets(1.7,3.5,100);m.poll(1500);check(!m.unacknowledged(),"old pressure packet cannot satisfy confirmation");
  packets(1.7,3.5,1600);check(m.view(1600)==View::Alarm,"both wheels trigger on confirmed fresh samples");
  check(m.simulated.alarms[0][0].active && m.simulated.alarms[1][1].active,"front LOW and rear HIGH are independent");
  m.input(Input::SettingsOn,1600);m.input(Input::Quick,1601);
  check(!m.quickActive && !m.unacknowledged() && m.view(1601)==View::Race,"alarm QUICK acknowledges all without opening quick view");
  check(m.settingsOn && m.settingsSuppressed,"alarm ack requires settings OFF ON");
  m.input(Input::SettingsOn,1602);check(m.view(1602)==View::Race,"repeated settings ON remains suppressed");
  packets(1.7,3.5,2000);check(!m.unacknowledged() && m.alertPresent(),"persistent acknowledged deviation stays small indicator");
  auto cosmetic=m.config;cosmetic.theme=2;cosmetic.pressure=1;m.commit(cosmetic);packets(1.7,3.5,2050);
  check(!m.unacknowledged()&&m.alertPresent(),"theme and unit changes preserve acknowledged alarm latches");
  packets(2.5,2.5,2100);packets(2.5,2.5,3200);packets(1.7,2.5,3300);packets(1.7,2.5,4400);
  check(m.unacknowledged(),"stable hysteresis recovery rearms same alarm");
  m.input(Input::Quick,4401);packets(3.5,2.5,4500);packets(3.5,2.5,5600);
  check(m.simulated.alarms[0][1].active && !m.simulated.alarms[0][1].acknowledged && !m.simulated.alarms[0][0].active,"opposite alarm is a new event with correct HIGH label");
  m.simulated.pressure[0].valid=false;m.poll(5700);check(!m.simulated.alarms[0][1].active,"lost sensor is not low/high pressure");
  check(m.pressurePacket(true,"SIM-F",2.5,25,80,5800,42) &&
        !m.pressurePacket(true,"SIM-F",2.5,25,80,5900,42),"duplicate TPMS frame token is not a new sample");
  auto reassigned=m.config;std::strcpy(reassigned.frontId,"OTHER-F");m.commit(reassigned);
  check(!m.simulated.pressure[0].valid,"sensor reassignment clears previous wheel data");
  m.commit(c);
  packets(2.5,2.5,6000);m.poll(126001);check(!m.simulated.pressure[0].fresh(m.time(126001),m.config.staleMs),"TPMS expiry is based on receipt age");
  m=Model();check(!m.testLap(60000,0),"test injection rejected while TEST OFF");
  m.real.session.best.lapMs=70000;m.setTest(true);m.testLap(90000,0);
  check(m.simulated.session.best.lapMs==90000 && m.real.session.best.lapMs==70000,"test BEST cannot contaminate real reference");
  check(m.view(0)==View::Hold && !m.simulated.session.newBest,"first completed lap is reference not record improvement");
  m.testLap(89000,1000);check(m.view(1000)==View::Best,"improved lap opens BEST overlay");
  m.input(Input::SettingsOn,1100);check(m.view(1100)==View::Settings,"settings covers BEST");
  m.input(Input::SettingsOff,7000);check(m.view(7000)==View::Race,"covered BEST expires from original event");
  m.simulated.session.sector(1,29000);check(m.simulated.session.deltaValid && m.simulated.session.delta<0,"sector cumulative delta uses full best lap");
  m.setTest(false);check(!m.simulated.session.best.lapMs && !m.quickActive && m.real.session.best.lapMs==70000,"TEST OFF clears synthetic values and restores real source");
  check(m.segments(10)==0 && m.segments(-11)==1 && m.segments(401)==3,"neutral band and started delta segments");
  check(localDate(2024,2,29,86399000,120)==20240301 && localDate(2026,1,1,0,-60)==20251231,"local reference day handles leap and year rollover");
  check(localDate(2025,2,29,0,0)==0 && localDate(0,1,1,0,0)==0,"invalid GNSS calendar rejected");
  m.day=20261024;m.geometry=123;m.real.session.best.lapMs=60000;
  check(m.setScope(20261025,123) && m.real.session.best.lapMs==0,"day change isolates reference");
  m.real.session.best.lapMs=60000;check(m.setScope(20261025,124)&&!m.real.session.best.lapMs,"geometry change isolates reference");
  const auto geometry=trackHash(Tracks::PomposaLong);auto changed=Tracks::PomposaLong;changed.halfWidthM+=1;
  check(trackHash(changed)!=geometry,"track identity includes gate policy");
  TerminalReader reader;unsigned lines=0,errors=0;char last[192]="";
  auto line=[&](char* value){++lines;std::snprintf(last,sizeof(last),"%s",value);};auto err=[&](const char*){++errors;};
  for(const char* p="switch settings on\r\n";*p;++p)reader.accept(*p,line,err);
  check(lines==1 && !std::strcmp(last,"switch settings on"),"CRLF is one intact command, not legacy letters");
  for(unsigned i=0;i<220;++i)reader.accept('x',line,err);
  reader.accept('\n',line,err);
  reader.accept('t',line,err);reader.accept('\n',line,err);
  check(errors==1 && lines==2 && !std::strcmp(last,"t"),"overflow discards whole line then recovers");
  Config restored;const bool saved=saveConfig(c,"lt-selftest");
  check(saved && loadConfig(restored,"lt-selftest") && restored.frontMin==c.frontMin && !std::strcmp(restored.frontId,c.frontId),"versioned NVS reload in isolated test namespace");
  Imu::Calibration calibration;check(calibration.valid(),"identity IMU rotation is orthonormal");
  calibration.axes[0]={0,1,0};check(!calibration.valid(),"duplicate IMU axes rejected");
  std::unique_ptr<Imu::Estimate> estimate(new Imu::Estimate());estimate->calibration.verified=true;
  Imu::Sample sample;sample.valid=true;sample.ms=1000;sample.us=1000000;sample.accel={0,0,1};
  estimate->process(sample);sample.us+=20000;sample.ms+=20;sample.gyro={90,0,0};estimate->process(sample);
  check(estimate->valid && std::fabs(estimate->rollDegrees()-1.8)<.01,"gyro estimate integrates actual dt independently of display");
  sample.us+=200000;sample.ms+=200;estimate->process(sample);check(!estimate->valid,"IMU timing gap invalidates dynamic estimate");
  sample.us+=20000;sample.ms+=20;sample.valid=false;estimate->process(sample);check(!estimate->valid,"IMU loss remains explicitly invalid");
  *estimate=Imu::Estimate();sample=Imu::Sample();sample.valid=true;sample.ms=1000;sample.us=1000000;sample.accel={0,.5,.8660254038};estimate->process(sample);
  double displayAngle=0;bool raw=false;
  check(estimate->displayAngle(1000,0,displayAngle,raw)&&raw&&std::fabs(displayAngle-30)<.001&&!estimate->valid,
    "uncalibrated CAMERA shows raw 30 degree tilt without validating dynamic estimator");
  check(estimate->displayAngle(1001,10,displayAngle,raw)&&std::fabs(displayAngle-20)<.001,"raw camera display respects existing lean zero");
  check(!estimate->displayAngle(1150,0,displayAngle,raw),"camera angle expires after 150 ms");
  sample.ms+=20;sample.us+=20000;sample.valid=false;estimate->process(sample);
  check(!estimate->displayAngle(sample.ms,0,displayAngle,raw),"lost IMU cannot show stale raw camera angle");
  sample.valid=true;sample.ms+=20;sample.us+=20000;sample.accel={1,0,0};estimate->process(sample);
  check(!estimate->displayAngle(sample.ms,0,displayAngle,raw),"undefined raw roll at pitch axis is rejected");
  estimate->calibration.verified=true;estimate->valid=false;estimate->rawRollValid=true;
  check(!estimate->displayAngle(sample.ms,0,displayAngle,raw),"calibrated invalid dynamic estimate is not silently replaced by raw angle");
  m=Model();auto shortcuts=m.config;shortcuts.quickSlots[0]=3;shortcuts.quickSlots[1]=5;shortcuts.quickSlots[2]=7;m.commit(shortcuts);
  m.input(Input::Quick,100);check(m.view(100)==View::BestLap,"first QUICK uses selected best lap page");
  m.input(Input::Quick,200);m.input(Input::Quick,300);check(m.view(300)==View::MaxDeceleration,"third QUICK uses selected deceleration page");
  m.input(Input::Quick,400);check(m.view(400)==View::BestLap,"configured QUICK pages wrap after three presses");
  m=Model();m.input(Input::SettingsOn,0);m.menu=Quick1;m.input(Input::Quick,1);m.tap(true,2);
  check(m.editing&&m.draft.quickSlots[0]==1&&m.config.quickSlots[0]==0,"quick mapping edit stays unconfirmed");
  m.input(Input::SettingsOff,3);check(m.config.quickSlots[0]==0,"settings OFF discards shortcut draft");
  m.input(Input::SettingsOn,4);m.menu=Background;m.input(Input::Quick,5);m.tap(true,6);m.input(Input::Quick,7);
  check(m.config.theme==1&&!m.editing,"background selection confirms through common menu events");
  auto invalidQuick=m.config;invalidQuick.quickSlots[2]=8;check(!validConfig(invalidQuick),"unknown quick page rejected");
  invalidQuick=m.config;invalidQuick.theme=3;check(!validConfig(invalidQuick),"unknown background rejected");
  m=Model();m.observeSpeed(false,180,true);m.observeSpeed(false,120,true);m.observeSpeed(false,999,false);
  m.observeImu(false,-44,true,.55,true);m.observeImu(false,40,true,-.8,true);m.observeImu(false,90,false,5,false);
  check(m.real.maxima.angle==44&&m.real.maxima.speedKmh==180&&std::fabs(m.real.maxima.accelerationG-.55)<1e-9&&
        std::fabs(m.real.maxima.decelerationG-.8)<1e-9,"session maxima use valid samples, absolute angle and separate braking magnitude");
  m.setTest(true);m.observeSpeed(true,300,true);m.observeImu(true,60,true,1,true);m.setTest(false);
  check(m.real.maxima.speedKmh==180&&!m.simulated.maxima.speedValid,"test maxima cannot overwrite actual session peaks");
  m.setScope(20261025,123);check(!m.real.maxima.angleValid&&!m.real.maxima.speedValid,"scope change resets session maxima");
  StoredConfigV3 old;std::memcpy(static_cast<void*>(&old.config),&c,sizeof(old.config));old.config.version=3;
  old.config.speed=1;old.checksum=hashBytes(&old.config,sizeof(old.config));
  Preferences legacyStore;bool oldSaved=legacyStore.begin("lt-v3-test",false);
  if(oldSaved){oldSaved=legacyStore.putBytes("config",&old,sizeof(old))==sizeof(old);legacyStore.end();}
  Config upgraded;
  check(oldSaved&&loadConfig(upgraded,"lt-v3-test")&&upgraded.version==4&&upgraded.speed==1&&upgraded.frontMin==c.frontMin&&
        !std::strcmp(upgraded.frontId,c.frontId)&&upgraded.quickSlots[0]==0&&upgraded.quickSlots[2]==2&&upgraded.theme==0,
        "NVS v3 migration preserves existing settings and initializes new shortcuts");
  old.checksum^=1;check(!migrateV3(old,upgraded),"corrupt old NVS configuration is not migrated");
  auto persistent=c;persistent.quickSlots[0]=3;persistent.quickSlots[1]=4;persistent.quickSlots[2]=7;persistent.theme=2;
  check(saveConfig(persistent,"lt-selftest")&&loadConfig(restored,"lt-selftest")&&restored.quickSlots[0]==3&&
        restored.quickSlots[2]==7&&restored.theme==2,"new shortcut and theme settings survive NVS reload");
  runPanelSelfTests(m,check);
  out.printf("CONTROL TEST RESULT: %u passed, %u failed\n",passed,failed);return failed==0;
}
