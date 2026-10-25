#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <initializer_list>
#include "track_catalog.h"

namespace Control {
inline bool expired(uint32_t now, uint32_t since, uint32_t duration) { return uint32_t(now - since) >= duration; }
enum class Base : uint8_t { Race, Camera };
enum class View : uint8_t { Race, Camera, Settings, LastLap, Rear, Front, Hold, Best, Alarm,
  BestLap, MaxAngle, MaxSpeed, MaxAcceleration, MaxDeceleration };
enum class QuickPage : uint8_t { LastLap, Rear, Front, BestLap, MaxAngle, MaxSpeed, MaxAcceleration, MaxDeceleration, Count };
inline const char* quickName(unsigned page){
  static const char* names[]={"LAST LAP","REAR PRESSURE","FRONT PRESSURE","BEST LAP","MAX ANGLE","MAX SPEED","MAX ACCELERATION","MAX DECELERATION"};
  return page<unsigned(QuickPage::Count)?names[page]:"?";
}
inline const char* quickToken(unsigned page){
  static const char* names[]={"last_lap","rear_pressure","front_pressure","best_lap","max_angle","max_speed","max_acceleration","max_deceleration"};
  return page<unsigned(QuickPage::Count)?names[page]:"?";
}
inline View quickView(unsigned page){
  static const View views[]={View::LastLap,View::Rear,View::Front,View::BestLap,View::MaxAngle,View::MaxSpeed,View::MaxAcceleration,View::MaxDeceleration};
  return page<unsigned(QuickPage::Count)?views[page]:View::LastLap;
}
inline bool isQuickView(View view){return view==View::LastLap||view==View::Rear||view==View::Front||view>=View::BestLap;}
enum class Input : uint8_t { Quick, SettingsOn, SettingsOff, Left, Center, Right };
struct Config {
  uint32_t version = 4;
  uint8_t base = 0, speed = 0, pressure = 0, temperature = 0, accel = 0, track = 0;
  uint8_t holdSeconds = 5, deltaIndex = 1;
  bool pressureAlarm = false, logging = false;
  float frontMin = 0, frontMax = 0, rearMin = 0, rearMax = 0;
  float hysteresis = .05f;
  uint32_t confirmMs = 1000, staleMs = 120000;
  int16_t utcOffsetMinutes = 120;
  char frontId[24] = "", rearId[24] = "";
  uint8_t quickSlots[3] = {0,1,2};
  uint8_t theme = 0;  // Black / white / dark blue.
};
inline double pressureValue(double bar, unsigned unit) { return unit == 1 ? bar * 14.503773773 : unit == 2 ? bar * 100 : bar; }
inline const char* pressureUnit(unsigned unit) { return unit == 1 ? "psi" : unit == 2 ? "kPa" : "bar"; }
inline double speedValue(double kmh, unsigned unit) { return unit ? kmh / 1.609344 : kmh; }
inline double temperatureValue(double c, unsigned unit) { return unit ? c * 1.8 + 32 : c; }
inline double accelValue(double g, unsigned unit) { return unit ? g * 9.80665 : g; }
inline bool rangeValid(float low, float high) { return std::isfinite(low) && std::isfinite(high) && low > 0 && high > low && high <= 20; }
inline bool identifierValid(const char* id){
  for(unsigned i=0;i<24;++i){const char c=id[i];if(!c)return true;
    if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c==':'||c=='-'||c=='_'||c=='.'))return false;
  }return false;
}
inline bool alarmReady(const Config& c) {
  return rangeValid(c.frontMin, c.frontMax) && rangeValid(c.rearMin, c.rearMax) &&
    c.frontId[0] && c.rearId[0] && std::strcmp(c.frontId, c.rearId) &&
    c.hysteresis * 2 < c.frontMax - c.frontMin && c.hysteresis * 2 < c.rearMax - c.rearMin;
}
inline bool validConfig(const Config& c) {
  const bool front = (c.frontMin == 0 && c.frontMax == 0) || rangeValid(c.frontMin, c.frontMax);
  const bool rear = (c.rearMin == 0 && c.rearMax == 0) || rangeValid(c.rearMin, c.rearMax);
  return c.version == 4 && c.theme<3 && c.quickSlots[0]<unsigned(QuickPage::Count) &&
    c.quickSlots[1]<unsigned(QuickPage::Count) && c.quickSlots[2]<unsigned(QuickPage::Count) &&
    c.base <= 1 && c.speed <= 1 && c.pressure <= 2 && c.temperature <= 1 &&
    c.accel <= 1 && c.track < Tracks::Count && c.holdSeconds >= 5 && c.holdSeconds <= 15 &&
    c.deltaIndex <= 3 && front && rear && std::isfinite(c.hysteresis) && c.hysteresis >= 0 &&
    c.hysteresis <= 1 && c.confirmMs <= 10000 && c.staleMs >= 1000 && c.staleMs <= 600000 &&
    c.utcOffsetMinutes >= -720 && c.utcOffsetMinutes <= 840 &&
    identifierValid(c.frontId) && identifierValid(c.rearId) &&
    (!c.pressureAlarm || alarmReady(c));
}
inline uint32_t hashBytes(const void* ptr, unsigned size, uint32_t hash = 2166136261UL) {
  const auto* bytes = static_cast<const uint8_t*>(ptr);
  for (unsigned i = 0; i < size; ++i) { hash ^= bytes[i]; hash *= 16777619UL; }
  return hash;
}
inline uint32_t trackHash(const Timing::TrackConfig& c) {
  // Explicit geometry, including policy and derived sectors; independent of pointers/padding.
  uint32_t h = hashBytes(c.sourceName, std::strlen(c.sourceName));
  const double data[] = {c.latitude,c.longitude,c.forwardEast,c.forwardNorth,c.halfWidthM,
    c.rearmDistanceM,c.minSpeedKmh,double(c.minLapMs),double(c.maxLapMs),double(c.splitCount)};
  h = hashBytes(data, sizeof(data), h);
  for (unsigned i = 0; i < c.splitCount; ++i) {
    const auto& g = c.splits[i];
    const double gate[] = {g.latitude,g.longitude,g.forwardEast,g.forwardNorth,g.halfWidthM,g.rearmDistanceM};
    h = hashBytes(gate, sizeof(gate), h);
  }
  return h;
}
// GNSS UTC calendar -> local date; fixed offset, no hidden DST rules.
inline uint32_t localDate(int year, unsigned month, unsigned day, uint32_t utcMs, int offset) {
  if (year < 2020 || year > 2099 || month < 1 || month > 12 || utcMs >= 86400000) return 0;
  const auto monthDays = [](int y, unsigned m) -> unsigned {
    const unsigned days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    return days[m-1] + (m == 2 && y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
  };
  if (day < 1 || day > monthDays(year,month)) return 0;
  const int64_t local = int64_t(utcMs) + int64_t(offset) * 60000;
  if (local < 0) { if (--day == 0) { if (--month == 0) { month = 12; --year; } day = monthDays(year,month); } }
  if (local >= 86400000) { if (++day > monthDays(year,month)) { day = 1; if (++month == 13) { month = 1; ++year; } } }
  return year * 10000 + month * 100 + day;
}

struct Reference { double lapMs = 0, sectors[3] = {}; };
struct Session {
  Reference best;
  double lastMs = 0, lastSectors[3] = {}, cumulative[2] = {}, delta = 0;
  unsigned int laps = 0, nextSplit = 0;
  bool deltaValid = false, finishDeltaValid = false, newBest = false;
  double finishDelta = 0;
  uint32_t lapAt = 0, holdAt = 0;
  bool hold = false;
  bool sector(unsigned index, double elapsed) {
    if (index != nextSplit + 1 || index > 2 || !std::isfinite(elapsed) || elapsed <= 0 ||
        (index == 2 && elapsed <= cumulative[0])) return false;
    cumulative[nextSplit++] = elapsed;
    if (best.lapMs > 0) {
      double reference = best.sectors[0] + (index == 2 ? best.sectors[1] : 0);
      delta = elapsed - reference; deltaValid = true;
    }
    return true;
  }
  bool finish(double elapsed, const double* sectors, uint32_t now) {
    if (!std::isfinite(elapsed) || elapsed < 30000 || elapsed > 600000) return false;
    double sum=0;
    for(unsigned i=0;i<3;++i){if(!std::isfinite(sectors[i])||sectors[i]<=0)return false;sum+=sectors[i];}
    if(std::fabs(sum-elapsed)>.01)return false;
    lastMs = elapsed;
    for (unsigned i = 0; i < 3; ++i) lastSectors[i] = sectors[i];
    const double oldBest = best.lapMs;
    finishDeltaValid = oldBest > 0;
    finishDelta = elapsed - oldBest;
    newBest = oldBest > 0 && elapsed < oldBest;
    if (oldBest == 0 || elapsed < oldBest) {
      best.lapMs = elapsed;
      for (unsigned i = 0; i < 3; ++i) best.sectors[i] = sectors[i];
    }
    ++laps; nextSplit = 0; deltaValid = false;
    lapAt = holdAt = now; hold = true;
    return true;
  }
};
struct Pressure {
  char id[24] = "";
  float bar = 0, temperatureC = NAN;
  int battery = -1;
  uint32_t at = 0, sequence = 0;
  uint32_t packetToken = 0;
  bool valid = false;
  bool fresh(uint32_t now, uint32_t timeout) const { return valid && !expired(now, at, timeout); }
};
struct Alarm {
  bool armed = true, active = false, acknowledged = false, pending = false, recovering = false;
  uint32_t pendingAt = 0, recoveryAt = 0;
};
struct Maxima {
  double angle=0,speedKmh=0,accelerationG=0,decelerationG=0;
  bool angleValid=false,speedValid=false,accelerationValid=false;
  void clearImu(){angle=accelerationG=decelerationG=0;angleValid=accelerationValid=false;}
};
struct Source { Session session; Pressure pressure[2]; Alarm alarms[2][2]; uint32_t handled[2] = {}; Maxima maxima; };
enum Menu : unsigned { DefaultScreen, Quick1, Quick2, Quick3, Background, SpeedUnit, PressureUnit, TemperatureUnit, AccelUnit, Track,
  LapHold, DeltaStep, AlarmEnabled, FrontRange, RearRange, Hysteresis, ConfirmTime,
  SensorTimeout, FrontId, RearId, ImuUpright, ImuLeft, ImuForward, ImuZero,
  TimeOffset, Logging, SdStatus, Back, MenuCount };
inline const char* menuName(unsigned item) {
  static const char* names[] = {"DEFAULT SCREEN","GREEN 1","GREEN 2","GREEN 3","BACKGROUND","SPEED UNIT","PRESSURE UNIT","TEMPERATURE UNIT","ACCEL UNIT",
    "TRACK","LAP HOLD","DELTA PER SEGMENT","PRESSURE ALARM","FRONT MIN/MAX","REAR MIN/MAX","HYSTERESIS",
    "ALARM CONFIRM","SENSOR TIMEOUT","FRONT SENSOR ID","REAR SENSOR ID","IMU UPRIGHT","IMU LEFT",
    "IMU FORWARD","IMU ZERO","UTC OFFSET","SD LOGGING","SD STATUS","NAZAJ"};
  return item < MenuCount ? names[item] : "?";
}
enum class Category : uint8_t { Display, Calibration, Units, Track, Tpms, Storage, Back, Count };
inline const char* categoryName(unsigned category){
  static const char* names[]={"DISPLAY","CALIBRATION","UNITS","TRACK","TPMS","STORAGE","BACK"};
  return category<unsigned(Category::Count)?names[category]:"?";
}
inline unsigned categoryItem(unsigned category,unsigned row){
  static const unsigned display[]={DefaultScreen,Quick1,Quick2,Quick3,Background,LapHold,DeltaStep,Back};
  static const unsigned calibration[]={ImuUpright,ImuLeft,ImuForward,ImuZero,Back};
  static const unsigned units[]={SpeedUnit,PressureUnit,TemperatureUnit,AccelUnit,TimeOffset,Back};
  static const unsigned track[]={Track,Back};
  static const unsigned tpms[]={AlarmEnabled,FrontRange,RearRange,Hysteresis,ConfirmTime,SensorTimeout,FrontId,RearId,Back};
  static const unsigned storage[]={Logging,SdStatus,Back};
  switch(Category(category)){
    case Category::Display:return row<8?display[row]:Back;
    case Category::Calibration:return row<5?calibration[row]:Back;
    case Category::Units:return row<6?units[row]:Back;
    case Category::Track:return row<2?track[row]:Back;
    case Category::Tpms:return row<9?tpms[row]:Back;
    case Category::Storage:return row<3?storage[row]:Back;
    default:return Back;
  }
}
inline unsigned categorySize(unsigned category){static const unsigned sizes[]={8,5,6,2,9,3,1};return category<7?sizes[category]:1;}
class Model {
 public:
  Config config, draft;
  Source real, simulated;
  bool testMode = false, settingsOn = false, settingsSuppressed = false, editing = false, dirty = false;
  bool quickActive = false;
  bool treeMenu=false,menuConfirmed=false;
  unsigned menuDepth=0,menuCategory=0,menuRow=0,forwardAxis=0,draftForwardAxis=0,editRow=0;
  unsigned quickIndex = 0, menu = 0, rangePart = 0;
  Base base = Base::Race;
  int direction = 0;
  uint32_t quickAt = 0, testOffset = 0, day = 0, geometry = 0, rejected = 0;
  unsigned action = 0;  // UI adapter consumes calibration/SD actions.
  char error[96] = "";
  unsigned treeCount()const{return menuDepth?categorySize(menuCategory):unsigned(Category::Count);}
  const char* treeLabel(unsigned row)const{
    if(!menuDepth)return categoryName(row);
    const auto item=categoryItem(menuCategory,row);return item==Back?"BACK":menuName(item);
  }
  void openTree(uint32_t actual){
    input(Input::SettingsOn,actual);treeMenu=true;menuDepth=menuCategory=menuRow=0;
    menuConfirmed=false;editing=false;menu=DefaultScreen;error[0]=0;
  }
  void treeShort(uint32_t actual){
    if(editing){treeLong(actual);return;}
    if(!menuDepth){
      if(menuRow==unsigned(Category::Back)){settingsSuppressed=true;base=Base::Race;quickActive=false;source().session.hold=false;return;}
      menuCategory=menuRow;menuDepth=1;menuRow=0;menu=categoryItem(menuCategory,0);
    }else if(menu==Back){menuDepth=0;menuRow=menuCategory;menu=DefaultScreen;}
    else {treeLong(actual);return;}
    menuConfirmed=false;error[0]=0;(void)actual;
  }
  void treeLong(uint32_t actual){
    if(!menuDepth||menu==Back){treeShort(actual);return;}
    if(!editing){draft=config;draftForwardAxis=forwardAxis;editing=true;rangePart=0;editRow=choiceIndex(draft);menuConfirmed=false;error[0]=0;return;}
    if(choiceCount() && editRow==choiceCount()){editing=false;menuConfirmed=false;error[0]=0;return;}
    if((menu==FrontRange||menu==RearRange)&&rangePart++==0)return;
    if((menu>=ImuUpright&&menu<=ImuZero)||menu==SdStatus){if(menu==ImuForward)forwardAxis=draftForwardAxis;action=menu;editing=false;menuConfirmed=true;return;}
    if(commit(draft))menuConfirmed=true;
  }
  bool numericEditor()const{
    return menu==LapHold||menu==FrontRange||menu==RearRange||menu==Hysteresis||menu==ConfirmTime||menu==SensorTimeout||menu==TimeOffset;
  }
  unsigned choiceCount()const{
    switch(menu){
      case DefaultScreen:case SpeedUnit:case TemperatureUnit:case AccelUnit:case AlarmEnabled:case Logging:return 2;
      case FrontId:return config.frontId[0]&&std::strcmp(config.frontId,"SIM-F")?3:2;
      case RearId:return config.rearId[0]&&std::strcmp(config.rearId,"SIM-R")?3:2;
      case Quick1:case Quick2:case Quick3:return unsigned(QuickPage::Count);
      case Background:case PressureUnit:return 3;
      case Track:return Tracks::Count;
      case DeltaStep:return 4;
      case ImuForward:return 6;
      case ImuUpright:case ImuLeft:case ImuZero:case SdStatus:return 1;
      default:return 0;
    }
  }
  unsigned choiceIndex(const Config& c)const{
    switch(menu){
      case DefaultScreen:return c.base;
      case Quick1:case Quick2:case Quick3:return c.quickSlots[menu-Quick1];
      case Background:return c.theme;case SpeedUnit:return c.speed;case PressureUnit:return c.pressure;
      case TemperatureUnit:return c.temperature;case AccelUnit:return c.accel;case Track:return c.track;
      case DeltaStep:return c.deltaIndex;case AlarmEnabled:return c.pressureAlarm;case Logging:return c.logging;
      case FrontId:return !c.frontId[0]?0:!std::strcmp(c.frontId,"SIM-F")?1:2;
      case RearId:return !c.rearId[0]?0:!std::strcmp(c.rearId,"SIM-R")?1:2;case ImuForward:return forwardAxis;
      default:return 0;
    }
  }
  void choiceLabel(unsigned row,char* out,size_t size)const{
    if(row==choiceCount()){std::snprintf(out,size,"BACK");return;}
    const char* label="START";
    switch(menu){
      case DefaultScreen:label=row?"CAMERA":"RACE";break;
      case Quick1:case Quick2:case Quick3:label=quickName(row);break;
      case Background:{static const char* names[]={"BLACK","WHITE","DARK BLUE"};label=names[row%3];break;}
      case SpeedUnit:label=row?"MPH":"KM/H";break;
      case PressureUnit:label=pressureUnit(row);break;
      case TemperatureUnit:label=row?"FAHRENHEIT":"CELSIUS";break;
      case AccelUnit:label=row?"M/S2":"G";break;
      case Track:label=Tracks::Catalog[row%Tracks::Count]->name;break;
      case DeltaStep:{static const char* names[]={"0.1 S","0.2 S","0.5 S","1.0 S"};label=names[row%4];break;}
      case AlarmEnabled:case Logging:label=row?"ON":"OFF";break;
      case FrontId:label=row==2?config.frontId:row?"SIM-F":"UNASSIGNED";break;
      case RearId:label=row==2?config.rearId:row?"SIM-R":"UNASSIGNED";break;
      case ImuForward:{static const char* names[]={"X+","X-","Y+","Y-","Z+","Z-"};label=names[row%6];break;}
      default:break;
    }
    std::snprintf(out,size,"%s",label);
  }
  void setChoice(unsigned row){
    switch(menu){
      case DefaultScreen:draft.base=row;break;
      case Quick1:case Quick2:case Quick3:draft.quickSlots[menu-Quick1]=row;break;
      case Background:draft.theme=row;break;case SpeedUnit:draft.speed=row;break;case PressureUnit:draft.pressure=row;break;
      case TemperatureUnit:draft.temperature=row;break;case AccelUnit:draft.accel=row;break;case Track:draft.track=row;break;
      case DeltaStep:draft.deltaIndex=row;break;case AlarmEnabled:draft.pressureAlarm=row;break;case Logging:draft.logging=row;break;
      case FrontId:std::snprintf(draft.frontId,sizeof(draft.frontId),"%s",row==2?config.frontId:row?"SIM-F":"");break;
      case RearId:std::snprintf(draft.rearId,sizeof(draft.rearId),"%s",row==2?config.rearId:row?"SIM-R":"");break;
      case ImuForward:draftForwardAxis=row;break;default:break;
    }
  }
  void numericValue(char* out,size_t size)const{
    if(menu==FrontRange||menu==RearRange){
      const double value=menu==FrontRange?(rangePart?draft.frontMax:draft.frontMin):(rangePart?draft.rearMax:draft.rearMin);
      std::snprintf(out,size,"%.2f %s",pressureValue(value,draft.pressure),pressureUnit(draft.pressure));
    }else menuValue(menu,draft,out,size);
  }
  Source& source() { return testMode ? simulated : real; }
  const Source& source() const { return testMode ? simulated : real; }
  uint32_t time(uint32_t actual) const { return actual + (testMode ? testOffset : 0); }
  double deltaStepMs() const { const double values[] = {100,200,500,1000}; return values[config.deltaIndex]; }
  unsigned segments(double delta) const { return std::fabs(delta) <= 10 ? 0 : unsigned(std::fmin(3.0,std::ceil(std::fabs(delta) / deltaStepMs()))); }
  bool unacknowledged() const {
    for (const auto& wheel : source().alarms) for (const auto& alarm : wheel)
      if (alarm.active && !alarm.acknowledged) return true;
    return false;
  }
  bool alertPresent() const {
    for (const auto& wheel : source().alarms) for (const auto& alarm : wheel) if (alarm.active) return true;
    return false;
  }
  View view(uint32_t actual) const {
    const uint32_t now = time(actual);
    if (unacknowledged()) return View::Alarm;
    if (settingsOn && !settingsSuppressed) return View::Settings;
    if (quickActive && !expired(now,quickAt,5000)) return quickView(config.quickSlots[quickIndex]);
    const auto& s = source().session;
    if (s.hold && !expired(now,s.holdAt,uint32_t(config.holdSeconds)*1000)) return s.newBest ? View::Best : View::Hold;
    return base == Base::Race ? View::Race : View::Camera;
  }
  bool commit(const Config& next) {
    if (!validConfig(next)) { std::snprintf(error,sizeof(error),"Invalid setting/range/IDs; MIN < MAX and alarm requires assignments"); return false; }
    const bool changedIds[]={std::strcmp(config.frontId,next.frontId)!=0,std::strcmp(config.rearId,next.rearId)!=0};
    const bool alarmChanged=changedIds[0]||changedIds[1]||config.pressureAlarm!=next.pressureAlarm||
      config.frontMin!=next.frontMin||config.frontMax!=next.frontMax||config.rearMin!=next.rearMin||config.rearMax!=next.rearMax||
      config.hysteresis!=next.hysteresis||config.confirmMs!=next.confirmMs||config.staleMs!=next.staleMs;
    config = next; dirty = true; error[0] = 0; base = Base(config.base);
    draft=config;editing=false;
    // Cosmetic/unit changes must preserve acknowledged alarms and their latches.
    if(alarmChanged)for (auto* s : {&real,&simulated}) {
      for (auto& wheel : s->alarms) for (auto& alarm : wheel) alarm = Alarm();
      for (unsigned i = 0; i < 2; ++i) {if(changedIds[i])s->pressure[i]=Pressure();s->handled[i] = s->pressure[i].sequence;}
    }
    return true;
  }
  void input(Input event, uint32_t actual) {
    const uint32_t now = time(actual);
    if (event == Input::SettingsOn) {
      if (!settingsOn) { settingsOn = true; settingsSuppressed = false; editing = false; menu = 0; treeMenu=false;menuConfirmed=false; }
      return;
    }
    if (event == Input::SettingsOff) { settingsOn = false; settingsSuppressed = false; editing = false; treeMenu=false;menuConfirmed=false;return; }
    if (event == Input::Center) { direction = 0; return; }
    if (event == Input::Left || event == Input::Right) {
      const int next = event == Input::Left ? -1 : 1;
      const bool edge = direction == 0; direction = next;
      if (!edge) return;
      if (settingsOn && !settingsSuppressed) {
        menuConfirmed=false;error[0]=0;
        if (editing && treeMenu && choiceCount()){
          editRow=(editRow+choiceCount()+1+next)%(choiceCount()+1);
          if(editRow<choiceCount())setChoice(editRow);
        }else if (editing) adjust(next);
        else if(treeMenu){menuRow=(menuRow+treeCount()+next)%treeCount();menu=menuDepth?categoryItem(menuCategory,menuRow):DefaultScreen;}
        else menu = (menu + MenuCount + next) % MenuCount;
      } else { base = next < 0 ? Base::Race : Base::Camera; quickActive = false; }
      return;
    }
    if (event != Input::Quick) return;
    if (unacknowledged()) {
      for (auto& wheel : source().alarms) for (auto& alarm : wheel) if (alarm.active) alarm.acknowledged = true;
      quickActive = false; editing = false; base = Base::Race; source().session.hold = false;
      if (settingsOn) settingsSuppressed = true;
      return;
    }
    if (settingsOn && !settingsSuppressed) {
      if(treeMenu){treeShort(actual);return;}
      if (menu == Back) { settingsSuppressed = true; editing = false; return; }
      if (menu >= ImuUpright && menu <= ImuZero) { action = menu; return; }
      if (menu == SdStatus) { action = SdStatus; return; }
      if (!editing) { draft = config; editing = true; rangePart = 0; }
      else if ((menu == FrontRange || menu == RearRange) && rangePart++ == 0) return;
      else if (commit(draft)) editing = false;
      return;
    }
    if (!quickActive || expired(now,quickAt,5000)) quickIndex = 0;
    else quickIndex = (quickIndex + 1) % 3;
    quickActive = true; quickAt = now;
  }
  void tap(bool right, uint32_t now) { input(Input::Center,now); input(right ? Input::Right : Input::Left,now); input(Input::Center,now); }
  void setTest(bool enabled) {
    testMode = enabled; simulated = Source(); testOffset = 0;
    quickActive = false; editing = false; settingsOn = false; settingsSuppressed = false;
    base = Base(config.base); direction = 0;
  }
  bool testLap(double ms, uint32_t actual) {
    if (!testMode) { ++rejected; return false; }
    auto& s = simulated.session;
    double parts[3] = {ms/3,ms/3,ms/3};
    if (s.nextSplit == 2) { parts[0]=s.cumulative[0];parts[1]=s.cumulative[1]-parts[0];parts[2]=ms-s.cumulative[1]; }
    if (parts[2] <= 0) return false;
    return s.finish(ms,parts,time(actual));
  }
  bool pressurePacket(bool simulatedPacket, const char* id, float bar, float temperature, int battery, uint32_t received, uint32_t packetToken=0) {
    if (!id || !std::isfinite(bar) || bar < 0 || bar > 20 || (simulatedPacket && !testMode)) return false;
    auto& target = simulatedPacket ? simulated : real;
    const char* ids[] = {config.frontId,config.rearId};
    for (unsigned i = 0; i < 2; ++i) {
      if (!ids[i][0] || std::strcmp(ids[i],id)) continue;
      auto& p = target.pressure[i];
      if((packetToken && p.packetToken==packetToken)||(p.valid && int32_t(received-p.at)<0))return false;
      std::snprintf(p.id,sizeof(p.id),"%s",id);p.bar=bar;p.temperatureC=temperature;p.battery=battery;
      p.at=received;p.valid=true;p.packetToken=packetToken;++p.sequence; return true;
    }
    return false;
  }
  void poll(uint32_t actual) {
    auto& data = source(); const uint32_t now = time(actual);
    for (unsigned wheel = 0; wheel < 2; ++wheel) {
      const auto& p = data.pressure[wheel];
      if (!p.fresh(now,config.staleMs)) {
        for (auto& a : data.alarms[wheel]) { a.active = false; a.pending = false; a.recovering = false; }
        continue;
      }
      if (data.handled[wheel] == p.sequence) continue;
      data.handled[wheel] = p.sequence;
      if (!config.pressureAlarm) continue;
      const double low = wheel == 0 ? config.frontMin : config.rearMin;
      const double high = wheel == 0 ? config.frontMax : config.rearMax;
      for (unsigned kind = 0; kind < 2; ++kind) {
        auto& a = data.alarms[wheel][kind];
        const bool outside = kind == 0 ? p.bar < low : p.bar > high;
        const bool recovered = p.bar >= low + config.hysteresis && p.bar <= high - config.hysteresis;
        if (outside && a.armed) {
          if (!a.pending) { a.pending=true;a.pendingAt=p.at; }
          if (expired(p.at,a.pendingAt,config.confirmMs)) {
            a.active=true;a.acknowledged=false;a.armed=false;a.pending=false;
            auto& opposite=data.alarms[wheel][1-kind];opposite.active=false;opposite.pending=false;
          }
        } else if (!outside) a.pending=false;
        if (recovered) {
          if (!a.recovering) { a.recovering=true;a.recoveryAt=p.at; }
          if (expired(p.at,a.recoveryAt,config.confirmMs)) { a=Alarm(); }
        } else a.recovering=false;
      }
    }
  }
  bool setScope(uint32_t newDay, uint32_t newGeometry) {
    if (day == newDay && geometry == newGeometry) return false;
    day=newDay;geometry=newGeometry;real.session=Session();real.maxima=Maxima();return true;
  }
  void observeSpeed(bool test,double kmh,bool valid){
    if(!valid||!std::isfinite(kmh)||kmh<0)return;
    auto& m=(test?simulated:real).maxima;m.speedKmh=std::fmax(m.speedKmh,kmh);m.speedValid=true;
  }
  void observeImu(bool test,double angle,bool angleValid,double accelerationG,bool accelerationValid){
    auto& m=(test?simulated:real).maxima;
    if(angleValid&&std::isfinite(angle)){m.angle=std::fmax(m.angle,std::fabs(angle));m.angleValid=true;}
    if(accelerationValid&&std::isfinite(accelerationG)){
      m.accelerationG=std::fmax(m.accelerationG,accelerationG);m.decelerationG=std::fmax(m.decelerationG,-accelerationG);m.accelerationValid=true;
    }
  }
  void clearImuMaxima(){real.maxima.clearImu();simulated.maxima.clearImu();}
  void menuValue(unsigned item, const Config& c, char* out, size_t size) const {
    switch (item) {
      case DefaultScreen: std::snprintf(out,size,"%s",c.base ? "CAMERA" : "RACE");break;
      case Quick1: case Quick2: case Quick3: std::snprintf(out,size,"%s",quickName(c.quickSlots[item-Quick1]));break;
      case Background: {const char* names[]={"BLACK","WHITE","DARK BLUE"};std::snprintf(out,size,"%s",names[c.theme]);break;}
      case SpeedUnit: std::snprintf(out,size,"%s",c.speed ? "mph" : "km/h");break;
      case PressureUnit: std::snprintf(out,size,"%s",pressureUnit(c.pressure));break;
      case TemperatureUnit: std::snprintf(out,size,"%s",c.temperature ? "F" : "C");break;
      case AccelUnit: std::snprintf(out,size,"%s",c.accel ? "m/s2" : "g");break;
      case Track: std::snprintf(out,size,"%s",Tracks::Catalog[c.track]->name);break;
      case LapHold: std::snprintf(out,size,"%u s",c.holdSeconds);break;
      case DeltaStep: { const char* steps[]={"0.1 s","0.2 s","0.5 s","1.0 s"};std::snprintf(out,size,"%s",steps[c.deltaIndex]);break; }
      case AlarmEnabled: std::snprintf(out,size,"%s",c.pressureAlarm ? "ON" : "OFF");break;
      case FrontRange: case RearRange: std::snprintf(out,size,"%.2f / %.2f %s",pressureValue(item==FrontRange?c.frontMin:c.rearMin,c.pressure),pressureValue(item==FrontRange?c.frontMax:c.rearMax,c.pressure),pressureUnit(c.pressure));break;
      case Hysteresis: std::snprintf(out,size,"%.2f bar",c.hysteresis);break;
      case ConfirmTime: std::snprintf(out,size,"%lu ms",(unsigned long)c.confirmMs);break;
      case SensorTimeout: std::snprintf(out,size,"%lu s",(unsigned long)c.staleMs/1000);break;
      case FrontId: case RearId: std::snprintf(out,size,"%s",item==FrontId?c.frontId:c.rearId);break;
      case TimeOffset: std::snprintf(out,size,"UTC %+.2f h",c.utcOffsetMinutes/60.0);break;
      case Logging: std::snprintf(out,size,"%s",c.logging?"ON":"OFF");break;
      case ImuForward:{static const char* axes[]={"X+","X-","Y+","Y-","Z+","Z-"};std::snprintf(out,size,"FORWARD AXIS %s",axes[(editing?draftForwardAxis:forwardAxis)%6]);break;}
      case ImuUpright: std::snprintf(out,size,"CAPTURE UPRIGHT / HOLD STILL 2 S");break;
      case ImuLeft: std::snprintf(out,size,"CAPTURE LEFT / LEAN 10..70");break;
      case ImuZero: std::snprintf(out,size,"SET CURRENT POSITION TO ZERO");break;
      case SdStatus: std::snprintf(out,size,"SHOW SD STATUS IN TERMINAL");break;
      default: std::snprintf(out,size,"HOLD GREEN TO EDIT");break;
    }
  }
 private:
  void adjust(int delta) {
    auto cycle = [delta](uint8_t& field,unsigned count) { field=(field+count+delta)%count; };
    switch(menu) {
      case DefaultScreen: cycle(draft.base,2);break;
      case Quick1: case Quick2: case Quick3: cycle(draft.quickSlots[menu-Quick1],unsigned(QuickPage::Count));break;
      case Background: cycle(draft.theme,3);break;
      case SpeedUnit: cycle(draft.speed,2);break;
      case PressureUnit: cycle(draft.pressure,3);break;
      case TemperatureUnit: cycle(draft.temperature,2);break;
      case AccelUnit: cycle(draft.accel,2);break;
      case Track: cycle(draft.track,Tracks::Count);break;
      case LapHold: draft.holdSeconds=uint8_t(std::fmax(5,std::fmin(15,draft.holdSeconds+delta)));break;
      case DeltaStep: cycle(draft.deltaIndex,4);break;
      case AlarmEnabled: draft.pressureAlarm=!draft.pressureAlarm;break;
      case FrontRange: case RearRange: {
        float& value=menu==FrontRange?(rangePart?draft.frontMax:draft.frontMin):(rangePart?draft.rearMax:draft.rearMin);
        value=std::fmax(0.0f,std::fmin(20.0f,value+delta*.05f));break;
      }
      case Hysteresis: draft.hysteresis=std::fmax(0.0f,std::fmin(1.0f,draft.hysteresis+delta*.01f));break;
      case ConfirmTime: draft.confirmMs=uint32_t(std::fmax(0.0,std::fmin(10000.0,double(draft.confirmMs)+delta*100)));break;
      case SensorTimeout: draft.staleMs=uint32_t(std::fmax(1000.0,std::fmin(600000.0,double(draft.staleMs)+delta*1000)));break;
      case FrontId: std::snprintf(draft.frontId,sizeof(draft.frontId),"%s",delta>0?"SIM-F":"");break;
      case RearId: std::snprintf(draft.rearId,sizeof(draft.rearId),"%s",delta>0?"SIM-R":"");break;
      case TimeOffset: draft.utcOffsetMinutes=int16_t(std::fmax(-720,std::fmin(840,draft.utcOffsetMinutes+delta*15)));break;
      case Logging: draft.logging=!draft.logging;break;
      case ImuForward: draftForwardAxis=(draftForwardAxis+6+delta)%6;break;
      default:break;
    }
  }
};
}
