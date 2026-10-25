#pragma once
#include <Arduino.h>
#include <cstdlib>
#include "control_storage.h"
#include "terminal_reader.h"
#include "analysis_logger.h"
#include "imu_estimate.h"
#include "lap_timing.h"
#include "panel_navigation.h"

class TerminalApp {
 public:
  Control::Model model;
  AnalysisLogger logger;
  Imu::Estimate imu;
  bool legacyView=false,verbose=false,legacyImmediate=false;
  uint32_t rawImuDrops=0;
  PanelNavigation panel;
  PanelMotion panelMotion;
  PanelState panelState;
  uint32_t panelDrops=0;
  void panelInput(PanelState state,uint32_t at){panelState=state;panelResult(panel.update(state,at,model,panelMotion),at);}
  void panelResult(PanelAction action,uint32_t at){
    if(action==PanelAction::None)return;
    legacyView=false;
    if(action==PanelAction::Green)quick(at);
    Serial.printf("PANEL %s event_ms=%lu\n",action==PanelAction::Green?"GREEN SHORT":
      action==PanelAction::Entered?"SETTINGS ENTER":action==PanelAction::Exited?"SETTINGS EXIT":action==PanelAction::Menu?"MENU HOLD":action==PanelAction::Step?"MENU STEP":"HOLD IGNORED (entry not armed, moving, active lap or alarm)",(unsigned long)at);
  }
  void (*legacy)(char)=nullptr;
  void (*changeTrack)(unsigned)=nullptr;
  bool (*zeroImu)()=nullptr;
  float (*leanZero)()=nullptr;
  void (*selftest)()=nullptr;
  void (*calibrationChanged)()=nullptr;
  bool (*reloadReference)()=nullptr;
  void quick(uint32_t pressedAt) {
    legacyView=false;
    model.input(Control::Input::Quick,pressedAt);
  }
  void begin(bool sdReady) {
    const bool restored=Control::loadConfig(model.config);
    model.base=Control::Base(model.config.base);model.draft=model.config;
    Imu::load(imu.calibration);logger.begin(sdReady);
    model.geometry=Control::trackHash(*Tracks::Catalog[model.config.track]);
    applied=model.config;
    Serial.printf("TERMINAL v1: %s, settings=%s, USB=115200. Enter help + Enter.\n",sdReady?"SD OK":"SD unavailable",restored?"NVS":"defaults");
    if(model.config.logging)startLog();
  }
  void poll() {
    for(unsigned budget=0;budget<128 && Serial.available();++budget) {
      const char byte=Serial.read();
      if(legacyImmediate && byte!='\r' && byte!='\n') {
        if(byte=='!'){legacyImmediate=false;Serial.println("OK line mode; all commands require Enter");}
        else if(legacy)legacy(byte);
      } else reader.accept(byte,[this](char* line){command(line);},[](const char* error){Serial.printf("ERR %s\n",error);});
    }
    panelResult(panel.tick(millis(),model,panelMotion),millis());
    model.poll(millis());
    recordState();
    if(model.dirty)apply();
    if(model.action){
      const auto action=model.action;model.action=0;
      if(action==Control::ImuUpright)imu.startCapture(1);
      else if(action==Control::ImuLeft)imu.startCapture(2);
      else if(action==Control::ImuForward){
        Imu::Vec axis;static const char* axes[]={"x+","x-","y+","y-","z+","z-"};Imu::axis(axes[model.forwardAxis%6],axis);
        if(imu.forward(axis)){if(calibrationChanged)calibrationChanged();}
        else {std::snprintf(model.error,sizeof(model.error),"%s",imu.message);model.menuConfirmed=false;}
      }
      else if(action==Control::ImuZero && zeroImu){if(!zeroImu()){std::snprintf(model.error,sizeof(model.error),"ZERO REJECTED: CALIBRATE AND HOLD STILL");model.menuConfirmed=false;}}
      else if(action==Control::SdStatus)status();
    }
    if(imu.message[0] && std::strcmp(lastImuMessage,imu.message)){
      Serial.printf("IMU: %s\n",imu.message);std::snprintf(lastImuMessage,sizeof(lastImuMessage),"%s",imu.message);
    }
  }
  void onTiming(Timing::Event event, Timing::LapTimer& timer,uint32_t at) {
    auto& s=model.real.session;
    if(event==Timing::Event::Started || event==Timing::Event::LapRestarted) {s.nextSplit=0;s.deltaValid=false;s.lapAt=at;}
    if(event==Timing::Event::Interrupted){s.nextSplit=0;s.deltaValid=false;}
    if(event==Timing::Event::SectorCompleted){
      s.sector(timer.currentSector()-1,timer.currentCumulativeMs());
    }
    if(event==Timing::Event::LapCompleted){
      const double oldBest=s.best.lapMs;
      double sectors[3];for(unsigned i=0;i<3;++i)sectors[i]=timer.lastSectorMs(i);
      if(s.finish(timer.lastLapMs(),sectors,timer.lastCrossingReceivedMs()) && (oldBest==0||s.best.lapMs<oldBest))logger.best(model.day,model.geometry,s.best);
    }
    if(event!=Timing::Event::None && model.config.logging){
      char info[256];std::snprintf(info,sizeof(info),"kind=%u,gps_cross_ms=%.3f,lap_ms=%.3f,s1=%.3f,s2=%.3f,s3=%.3f,best_ms=%.3f,delta=%.3f",
        unsigned(event),timer.lastCrossingMs(),timer.lastLapMs(),s.lastSectors[0],s.lastSectors[1],s.lastSectors[2],s.best.lapMs,s.delta);
      // Real acquisition continues in TEST, but real rows never enter a TEST file.
      logger.event(false,at,"TIMING",info);
    }
  }
  void logImu(const Imu::Sample& sample) {
    if(!model.config.logging || model.testMode)return;
    char line[448];std::snprintf(line,sizeof(line),"IMU,%lu,%lu,%u,%.5f,%.5f,%.5f,%.4f,%.4f,%.4f,%.3f,%.4f,%u,%s\n",
      (unsigned long)sample.ms,(unsigned long)sample.us,sample.valid,sample.accel.x,sample.accel.y,sample.accel.z,
      sample.gyro.x,sample.gyro.y,sample.gyro.z,imu.rollDegrees(),imu.longitudinalG,imu.valid,imu.quality);logger.line(false,line);
  }
  bool ingestPressure(bool test,const char* id,float bar,float temperatureC,int battery,uint32_t received,uint32_t token=0){
    const bool accepted=model.pressurePacket(test,id,bar,temperatureC,battery,received,token);
    if(accepted && model.config.logging && test==model.testMode){
      char line[256];std::snprintf(line,sizeof(line),"TPMS,%lu,%s,%.5f,%.3f,%d,%lu,0,1\n",(unsigned long)received,id,bar,temperatureC,battery,(unsigned long)token);logger.line(test,line);
    }
    return accepted;
  }
  void status() {
    const auto& s=model.source().session;const auto& c=model.config;
    Serial.printf("STATUS source=%s view=%u base=%s settings=%u suppressed=%u editing=%u menu=%u:%s direction=%d day=%lu geometry=%08lx lap=%u last=%.3f best=%.3f delta=%s%.3f\n",
      model.testMode?"TEST":"REAL",unsigned(model.view(millis())),model.base==Control::Base::Race?"RACE":"CAMERA",
      model.settingsOn,model.settingsSuppressed,model.editing,model.menu,Control::menuName(model.menu),model.direction,
      (unsigned long)model.day,(unsigned long)model.geometry,s.laps,s.lastMs,s.best.lapMs,s.deltaValid?"":"INVALID:",s.delta);
    Serial.printf("SETTINGS base=%u speed=%u pressure=%u temperature=%u accel=%u track=%u hold=%u delta=%u alarm=%u front=%.3f/%.3f rear=%.3f/%.3f hysteresis=%.3f confirm=%lu stale=%lu front_id=%s rear_id=%s utc_offset=%d logging=%u\n",
      c.base,c.speed,c.pressure,c.temperature,c.accel,c.track,c.holdSeconds,c.deltaIndex,c.pressureAlarm,c.frontMin,c.frontMax,c.rearMin,c.rearMax,
      c.hysteresis,(unsigned long)c.confirmMs,(unsigned long)c.staleMs,c.frontId,c.rearId,c.utcOffsetMinutes,c.logging);
    Serial.printf("QUICK SETTINGS quick1=%u quick2=%u quick3=%u theme=%u schema=%lu\n",c.quickSlots[0],c.quickSlots[1],c.quickSlots[2],c.theme,(unsigned long)c.version);
    Serial.printf("PANEL green=%u red=%u armed=%u left=%u right=%u dropped=%lu lap_block=%u speed_fresh=%u speed_kmh=%.2f\n",
      panelState.green,panelState.red,panel.redArmed(),panelState.left,panelState.right,(unsigned long)panelDrops,
      panelMotion.lapRunning,panelMotion.speedFresh,panelMotion.speedKmh);
    Serial.printf("MENU TREE active=%u depth=%u category=%u:%s row=%u confirmed=%u editor_row=%u choices=%u numeric=%u range_part=%u\n",model.treeMenu,model.menuDepth,
      model.menuCategory,Control::categoryName(model.menuCategory),model.menuRow,model.menuConfirmed,model.editRow,model.choiceCount(),model.numericEditor(),model.rangePart);
    if(model.treeMenu&&model.editing){char value[80];if(model.numericEditor())model.numericValue(value,sizeof(value));else model.choiceLabel(model.editRow,value,sizeof(value));Serial.printf("EDITOR value=%s\n",value);}
    const auto& peaks=model.source().maxima;
    Serial.printf("MAXIMA angle_valid=%u angle_deg=%.3f speed_valid=%u speed_kmh=%.3f accel_valid=%u acceleration_g=%.4f deceleration_g=%.4f\n",
      peaks.angleValid,peaks.angle,peaks.speedValid,peaks.speedKmh,peaks.accelerationValid,peaks.accelerationG,peaks.decelerationG);
    double cameraAngle=0;bool cameraRaw=false;const bool cameraValid=imu.displayAngle(millis(),leanZero?leanZero():0,cameraAngle,cameraRaw);
    Serial.printf("CAMERA angle_valid=%u raw=%u angle_deg=%.3f\n",cameraValid,cameraRaw,cameraAngle);
    for(unsigned i=0;i<2;++i){const auto& p=model.source().pressure[i];
      const bool fresh=p.fresh(model.time(millis()),c.staleMs);char bar[24]="--",temperature[24]="--",battery[12]="--",age[16]="--";
      if(fresh){std::snprintf(bar,sizeof(bar),"%.3f",p.bar);if(std::isfinite(p.temperatureC))std::snprintf(temperature,sizeof(temperature),"%.2f",p.temperatureC);if(p.battery>=0&&p.battery<=100)std::snprintf(battery,sizeof(battery),"%d",p.battery);}
      if(p.sequence)std::snprintf(age,sizeof(age),"%lu",(unsigned long)(model.time(millis())-p.at));
      Serial.printf("TPMS %c id=%s status=%s bar=%s temperature_C=%s battery=%s age=%s alarm_low=%u/%u alarm_high=%u/%u\n",i?'R':'F',p.id,
        fresh?"FRESH":"SENSOR LOST",bar,temperature,battery,age,
        model.source().alarms[i][0].active,model.source().alarms[i][0].acknowledged,model.source().alarms[i][1].active,model.source().alarms[i][1].acknowledged);
    }
    Serial.printf("SD mounted=%u active=%u file=%s rows=%lu dropped=%lu errors=%lu max_us=%lu; IMU=%s, roll=%.2f, long_g=%.4f, imu_dropped=%lu\n",logger.ready,logger.active,logger.path,
      (unsigned long)logger.written,(unsigned long)logger.dropped,(unsigned long)logger.errors,(unsigned long)logger.maxWriteUs,imu.quality,imu.rollDegrees(),imu.longitudinalG,(unsigned long)rawImuDrops);
    if(model.settingsOn){char value[96];model.menuValue(model.menu,model.editing?model.draft:c,value,sizeof(value));Serial.printf("MENU %s %s = %s\n",model.editing?"EDIT":"SELECT",Control::menuName(model.menu),value);}
  }
  void command(char* line) {
    char* words[14]={};unsigned count=0;char* context=nullptr;
    for(char* token=strtok_r(line," \t",&context);token && count<14;token=strtok_r(nullptr," \t",&context))words[count++]=token;
    if(!count)return;
    if(count==1 && std::strlen(words[0])==1 && legacy && std::strchr("tvknsprqbgzcNSQGZCTVPBRK123456789\x1b",words[0][0])){
      legacy(words[0][0]);return;
    }
    if(equal(words[0],"help") && count==1){help();return;}
    if(equal(words[0],"status") && count==1){status();return;}
    if(equal(words[0],"selftest") && count==1){if(selftest)selftest();return;}
    if(equal(words[0],"legacy") && count==2 && equal(words[1],"on")){legacyImmediate=true;Serial.println("OK immediate legacy mode; ! returns line mode");return;}
    if(equal(words[0],"verbose") && count==2 && onOff(words[1],verbose)){Serial.printf("OK verbose=%u\n",verbose);return;}
    bool ok=false;error[0]=0;
    if(equal(words[0],"key") && count==2){
      if(equal(words[1],"quick")||equal(words[1],"green")){quick(millis());ok=true;}
      else if(equal(words[1],"left")||equal(words[1],"right")){model.tap(equal(words[1],"right"),millis());ok=true;}
    }else if(equal(words[0],"button") && count==3){
      auto next=panelState;
      if(equal(words[1],"red")){bool value=next.red;if(onOff(words[2],value)){next.red=value;ok=true;}}
      else if(equal(words[1],"green")){
        if(equal(words[2],"down")){next.green=true;ok=true;}
        if(equal(words[2],"up")){next.green=false;ok=true;}
      }else if(equal(words[1],"direction")){
        if(equal(words[2],"left")){next.left=true;next.right=false;ok=true;}
        if(equal(words[2],"right")){next.right=true;next.left=false;ok=true;}
        if(equal(words[2],"center")){next.right=next.left=false;ok=true;}
      }
      if(ok)panelInput(next,millis());
    }else if(equal(words[0],"switch") && count==3){
      if(equal(words[1],"settings")){
        if(equal(words[2],"on")){model.input(Control::Input::SettingsOn,millis());ok=true;}
        if(equal(words[2],"off")){model.input(Control::Input::SettingsOff,millis());ok=true;}
      }else if(equal(words[1],"direction")){
        const char* choices[]={"left","center","right"};const Control::Input events[]={Control::Input::Left,Control::Input::Center,Control::Input::Right};
        for(unsigned i=0;i<3;++i)if(equal(words[2],choices[i])){model.input(events[i],millis());ok=true;}
      }
    }else if(equal(words[0],"screen") && count==2){
      if(equal(words[1],"race")||equal(words[1],"camera")){model.base=equal(words[1],"camera")?Control::Base::Camera:Control::Base::Race;model.quickActive=false;ok=true;}
    }else if(equal(words[0],"set")){ok=set(words,count);}
    else if(equal(words[0],"max")&&count==2&&equal(words[1],"reset")){model.source().maxima=Control::Maxima();ok=true;}
    else if(equal(words[0],"settings") && count==2){
      if(equal(words[1],"show")){status();return;}
      if(equal(words[1],"reload")){Control::Config config;if(Control::loadConfig(config)){ok=model.commit(config);}}
    }else if(equal(words[0],"test")){ok=test(words,count);}
    else if(equal(words[0],"sd") && count==2){
      if(equal(words[1],"status")){status();return;}
      if(equal(words[1],"readback")){logger.diagnostic();ok=logger.ready;}
      if(equal(words[1],"selftest")){logger.testReference();ok=logger.ready;}
      if(equal(words[1],"inspect")){logger.inspect();ok=logger.ready;}
    }else if(equal(words[0],"reference") && count==2 && equal(words[1],"reload")){ok=reloadReference&&reloadReference();}
    else if(equal(words[0],"imu")){ok=imuCommand(words,count);}
    if(ok){legacyView=false;model.poll(millis());if(model.dirty)apply();recordState();Serial.printf("OK source=%s view=%u settings=%u editing=%u menu=%u\n",model.testMode?"TEST":"REAL",unsigned(model.view(millis())),model.settingsOn,model.editing,model.menu);}
    else Serial.printf("ERR %s\n",error[0]?error:model.error[0]?model.error:"unknown command, invalid argument or TEST OFF; enter help");
  }
  void startLog() {
    char metadata[448];const auto& c=model.config;
    std::snprintf(metadata,sizeof(metadata),"track=%s,geometry=%08lx,day=%lu,offset_min=%d,source=%s,hold=%u,delta=%u,alarm=%u,F=%.3f:%.3f,R=%.3f:%.3f,hys=%.3f,confirm=%lu,stale=%lu,ids=%s:%s,units=%u:%u:%u:%u",
      Tracks::Catalog[c.track]->name,(unsigned long)model.geometry,(unsigned long)model.day,c.utcOffsetMinutes,model.testMode?"TEST":"REAL",c.holdSeconds,c.deltaIndex,c.pressureAlarm,c.frontMin,c.frontMax,c.rearMin,c.rearMax,c.hysteresis,
      (unsigned long)c.confirmMs,(unsigned long)c.staleMs,c.frontId,c.rearId,c.speed,c.pressure,c.temperature,c.accel);
    logger.start(model.testMode,metadata);
    const auto& track=*Tracks::Catalog[c.track];
    for(unsigned i=0;i<=track.splitCount;++i){
      const Timing::Gate gate=i?track.splits[i-1]:Timing::Gate{track.latitude,track.longitude,track.forwardEast,track.forwardNorth,track.halfWidthM,track.rearmDistanceM};
      std::snprintf(metadata,sizeof(metadata),"line=%u,lat=%.9f,lon=%.9f,east=%.12f,north=%.12f,half_width=%.3f,rearm=%.3f",i,gate.latitude,gate.longitude,gate.forwardEast,gate.forwardNorth,gate.halfWidthM,gate.rearmDistanceM);
      logger.event(model.testMode,model.time(millis()),"GEOMETRY",metadata);
    }
    const auto& cal=imu.calibration;
    std::snprintf(metadata,sizeof(metadata),"verified=%u,lean_zero=%.3f,bias=%.5f:%.5f:%.5f,R=%.6f:%.6f:%.6f:%.6f:%.6f:%.6f:%.6f:%.6f:%.6f",cal.verified,leanZero?leanZero():0,cal.bias.x,cal.bias.y,cal.bias.z,
      cal.axes[0].x,cal.axes[0].y,cal.axes[0].z,cal.axes[1].x,cal.axes[1].y,cal.axes[1].z,cal.axes[2].x,cal.axes[2].y,cal.axes[2].z);
    logger.event(model.testMode,model.time(millis()),"CALIBRATION",metadata);
    std::snprintf(metadata,sizeof(metadata),"quick1=%s,quick2=%s,quick3=%s,theme=%u,schema=%lu",
      Control::quickToken(c.quickSlots[0]),Control::quickToken(c.quickSlots[1]),Control::quickToken(c.quickSlots[2]),c.theme,(unsigned long)c.version);
    logger.event(model.testMode,model.time(millis()),"QUICK_SETTINGS",metadata);
  }
 private:
  TerminalReader reader;Control::Config applied;
  char error[96]="",lastImuMessage[96]="";
  uint32_t previousAlarms=0,previousSensors=0;
  uint32_t alarmState()const{uint32_t state=0;for(unsigned wheel=0;wheel<2;++wheel)for(unsigned kind=0;kind<2;++kind){const auto& a=model.source().alarms[wheel][kind];state|=uint32_t(a.active|(a.acknowledged<<1))<<((wheel*2+kind)*2);}return state;}
  void recordState(){
    const uint32_t alarms=alarmState();uint32_t sensors=0;
    for(unsigned i=0;i<2;++i)if(model.source().pressure[i].fresh(model.time(millis()),model.config.staleMs))sensors|=1U<<i;
    if(model.config.logging){char detail[48];
      if(alarms!=previousAlarms){std::snprintf(detail,sizeof(detail),"state=%08lx",(unsigned long)alarms);logger.event(model.testMode,model.time(millis()),"ALARM_STATE",detail);}
      if(sensors!=previousSensors){std::snprintf(detail,sizeof(detail),"fresh_mask=%lu",(unsigned long)sensors);logger.event(model.testMode,model.time(millis()),"SENSOR_STATE",detail);}
    }
    previousAlarms=alarms;previousSensors=sensors;
  }
  static bool equal(const char* a,const char* b){return std::strcmp(a,b)==0;}
  static bool onOff(const char* text,bool& target){if(equal(text,"on")){target=true;return true;}if(equal(text,"off")){target=false;return true;}return false;}
  static bool number(const char* text,double& value){char* end=nullptr;value=std::strtod(text,&end);return end && *end==0 && end!=text && std::isfinite(value);}
  void apply(){
    const auto previous=applied;const auto& next=model.config;
    const bool saved=Control::saveConfig(next);
    model.dirty=false;applied=next;
    if(previous.track!=next.track && changeTrack)changeTrack(next.track);
    if(previous.utcOffsetMinutes!=next.utcOffsetMinutes && changeTrack)changeTrack(next.track);
    if(next.logging){logger.stop();startLog();}else if(previous.logging)logger.stop();
    Serial.printf("NVS: %s\n",saved?"SAVED":"FAILED (runtime setting active, not persisted)");
  }
  bool set(char** w,unsigned n){
    if(n<3)return false;
    auto c=model.config;double value=0;const bool numeric=number(w[2],value);
    if(equal(w[1],"front_range")||equal(w[1],"rear_range")){
      double upper;if(n!=4||!numeric||!number(w[3],upper))return false;
      if(equal(w[1],"front_range")){c.frontMin=value;c.frontMax=upper;}else{c.rearMin=value;c.rearMax=upper;}
    }else if(n!=3)return false;
    else if(equal(w[1],"quick1")||equal(w[1],"quick2")||equal(w[1],"quick3")){
      bool found=false;for(unsigned i=0;i<unsigned(Control::QuickPage::Count);++i)if(equal(w[2],Control::quickToken(i))){c.quickSlots[w[1][5]-'1']=i;found=true;}
      if(!found)return false;
    }else if(equal(w[1],"background")){
      const char* themes[]={"black","white","blue"};bool found=false;
      for(unsigned i=0;i<3;++i)if(equal(w[2],themes[i])){c.theme=i;found=true;}
      if(!found)return false;
    }
    else if(equal(w[1],"front_id")||equal(w[1],"rear_id")){
      if(std::strlen(w[2])>=sizeof(c.frontId))return false;
      std::snprintf(equal(w[1],"front_id")?c.frontId:c.rearId,sizeof(c.frontId),"%s",equal(w[2],"none")?"":w[2]);
    }else if(equal(w[1],"alarm")){if(!onOff(w[2],c.pressureAlarm))return false;}
    else if(equal(w[1],"logging")){if(!onOff(w[2],c.logging))return false;}
    else if(equal(w[1],"base")){if(!equal(w[2],"race")&&!equal(w[2],"camera"))return false;c.base=equal(w[2],"camera");}
    else if(equal(w[1],"speed")){if(!equal(w[2],"kmh")&&!equal(w[2],"mph"))return false;c.speed=equal(w[2],"mph");}
    else if(equal(w[1],"pressure")){const char* unit[]={"bar","psi","kpa"};bool found=false;for(unsigned i=0;i<3;++i)if(equal(w[2],unit[i])){c.pressure=i;found=true;}if(!found)return false;}
    else if(equal(w[1],"temperature")){if(!equal(w[2],"c")&&!equal(w[2],"f"))return false;c.temperature=equal(w[2],"f");}
    else if(equal(w[1],"accel")){if(!equal(w[2],"g")&&!equal(w[2],"ms2"))return false;c.accel=equal(w[2],"ms2");}
    else if(equal(w[1],"delta")){const double values[]={.1,.2,.5,1};bool found=false;for(unsigned i=0;i<4;++i)if(numeric&&std::fabs(value-values[i])<1e-6){c.deltaIndex=i;found=true;}if(!found)return false;}
    else if(equal(w[1],"hold")){if(!numeric||value<5||value>15||std::floor(value)!=value)return false;c.holdSeconds=value;}
    else if(equal(w[1],"track")){if(!numeric||value<1||value>Tracks::Count||std::floor(value)!=value)return false;c.track=value-1;}
    else if(equal(w[1],"hysteresis")){if(!numeric)return false;c.hysteresis=value;}
    else if(equal(w[1],"confirm_ms")){if(!numeric||value<0||value>10000||std::floor(value)!=value)return false;c.confirmMs=value;}
    else if(equal(w[1],"stale_ms")){if(!numeric||value<1000||value>600000||std::floor(value)!=value)return false;c.staleMs=value;}
    else if(equal(w[1],"utc_offset")){if(!numeric||value<-720||value>840||std::floor(value)!=value)return false;c.utcOffsetMinutes=value;}
    else return false;
    return model.commit(c);
  }
  bool test(char** w,unsigned n){
    if(n==2 && (equal(w[1],"on")||equal(w[1],"off"))){
      model.setTest(equal(w[1],"on"));logger.fault=0;if(model.config.logging){logger.stop();startLog();}return true;
    }
    if(!model.testMode)return false;
    if(n==2 && equal(w[1],"reset")){model.setTest(true);if(model.config.logging){logger.stop();startLog();}return true;}
    double value;
    if(n==5&&equal(w[1],"motion")){
      double speed,acceleration;
      if(!number(w[2],value)||!number(w[3],speed)||!number(w[4],acceleration)||speed<0)return false;
      model.observeSpeed(true,speed,true);model.observeImu(true,value,true,acceleration,true);return true;
    }
    if(n==3 && equal(w[1],"tick") && number(w[2],value) && value>=0 && value<=600000 && std::floor(value)==value){model.testOffset+=uint32_t(value);return true;}
    if(n==3 && equal(w[1],"lap") && number(w[2],value)){
      const bool ok=model.testLap(value,millis());if(ok&&model.config.logging)logger.event(true,model.time(millis()),"LAP",w[2]);return ok;
    }
    if(n==4 && equal(w[1],"sector")){
      double sector;if(!number(w[2],sector)||!number(w[3],value)||std::floor(sector)!=sector||sector<1||sector>2)return false;
      const bool ok=model.simulated.session.sector(sector,value);if(ok&&model.config.logging)logger.event(true,model.time(millis()),"SECTOR",w[3]);return ok;
    }
    if(n==4 && equal(w[1],"pressure") && (equal(w[2],"front")||equal(w[2],"rear")) && number(w[3],value)){
      const bool front=equal(w[2],"front");const auto& c=model.config;
      return ingestPressure(true,front?c.frontId:c.rearId,value,25,100,model.time(millis()));
    }
    if(n==4 && equal(w[1],"sensor") && equal(w[3],"lost") && (equal(w[2],"front")||equal(w[2],"rear"))){model.simulated.pressure[equal(w[2],"front")?0:1].valid=false;return true;}
    if(n==3 && equal(w[1],"sd")){
      if(equal(w[2],"fail"))logger.fault=1;else if(equal(w[2],"slow"))logger.fault=2;else if(equal(w[2],"normal"))logger.fault=0;else return false;return true;
    }
    return false;
  }
  bool imuCommand(char** w,unsigned n){
    if(n==2 && equal(w[1],"status")){Serial.printf("IMU %s: %s\n",imu.quality,imu.message);return true;}
    if(n==2 && equal(w[1],"upright")){imu.startCapture(1);return true;}
    if(n==2 && equal(w[1],"left")){imu.startCapture(2);return true;}
    if(n==2 && equal(w[1],"zero"))return zeroImu&&zeroImu();
    Imu::Vec x,y,z;
    if(n==3 && equal(w[1],"forward") && Imu::axis(w[2],x)){const bool ok=imu.forward(x);if(ok&&calibrationChanged)calibrationChanged();return ok;}
    if(n==5 && equal(w[1],"mount") && Imu::axis(w[2],x)&&Imu::axis(w[3],y)&&Imu::axis(w[4],z)){const bool ok=imu.manual(x,y,z);if(ok&&calibrationChanged)calibrationChanged();return ok;}
    return false;
  }
  static void help(){Serial.println(
    "TERMINAL v1 - every command ends with Enter, USB 115200\n"
    "help | status | selftest | verbose on/off | legacy on (! exits)\n"
    "key green/quick/left/right | switch settings on/off | switch direction left/center/right (direct diagnostic)\n"
    "button red on/off | button green down/up | button direction left/center/right (guarded panel)\n"
    "screen race/camera | settings show/reload\n"
    "set base race/camera | set speed kmh/mph | set pressure bar/psi/kpa\n"
    "set temperature c/f | set accel g/ms2 | set track 1 | set hold 5..15\n"
    "set quick1/quick2/quick3 last_lap/rear_pressure/front_pressure/best_lap/max_angle/max_speed/max_acceleration/max_deceleration\n"
    "set background black/white/blue | max reset (current source/session peaks)\n"
    "set delta 0.1/0.2/0.5/1.0 | set front_range MIN MAX | set rear_range MIN MAX (bar)\n"
    "set front_id ID | set rear_id ID (none clears) | set alarm on/off\n"
    "set hysteresis BAR | set confirm_ms 0..10000 | set stale_ms 1000..600000\n"
    "set utc_offset MINUTES (-720..840) | set logging on/off\n"
    "test on/off/reset | test pressure front/rear BAR | test sensor front/rear lost\n"
    "test lap MILLISECONDS | test sector 1/2 CUMULATIVE_MS | test tick MILLISECONDS\n"
    "test motion ANGLE_DEG SPEED_KMH ACCEL_G (isolated simulated maxima)\n"
    "test sd fail/slow/normal | sd status/readback/selftest/inspect | reference reload\n"
    "imu upright | imu left | imu forward x+/x-/y+/y-/z+/z- | imu mount x+ y+ z+ | imu zero/status\n"
    "Legacy one-letter commands t,v,n,s,q,b,k,g,z,c,r,p also require Enter.\n"
    "TEST is isolated; TPMS BLE is not connected; IMU dynamic estimate is EXPERIMENTAL.");}
};
