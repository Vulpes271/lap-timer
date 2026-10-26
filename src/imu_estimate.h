#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include "control_model.h"
#include "imu_calibration.h"

namespace Imu {
inline bool save(const Calibration& c){
  if(!c.valid())return false;
  Preferences p;if(!p.begin("lap-imu",false))return false;
  const auto hash=Control::hashBytes(&c,sizeof(c));
  bool ok=p.putBytes("rotation",&c,sizeof(c))==sizeof(c);ok=p.putUInt("hash",hash)==sizeof(hash)&&ok;p.end();return ok;
}
inline bool load(Calibration& c){
  Preferences p;if(!p.begin("lap-imu",true))return false;Calibration next;
  bool ok=p.getBytesLength("rotation")==sizeof(next)&&p.getBytes("rotation",&next,sizeof(next))==sizeof(next)&&
    p.getUInt("hash",0)==Control::hashBytes(&next,sizeof(next))&&next.valid();p.end();if(ok)c=next;return ok;
}
class Estimate {
 public:
  Calibration calibration;
  GuidedCalibration guided;
  double roll=0,pitch=0,longitudinalG=0;
  double rawRoll=0;
  bool rawRollValid=false;
  bool valid=false,aid=false;
  uint32_t at=0,gaps=0;
  const char* quality="UNCALIBRATED experimental";
  unsigned wizard=0;bool uprightReady=false,leftReady=false;
  char message[96]="";
  void startCapture(unsigned stage){wizard=stage;count=0;sumA=Vec();sumG=Vec();sumG2=0;sumNorm=0;sumNorm2=0;captureAt=0;std::snprintf(message,sizeof(message),"Capture %s: remain still 2 seconds",stage==1?"UPRIGHT":"LEFT");}
  bool forward(Vec knownSensorForward){
    if(!uprightReady||!leftReady){std::snprintf(message,sizeof(message),"Capture upright and left first");return false;}
    Calibration next=candidate;
    if(next.axes[0].dot(knownSensorForward.unit())<.7){std::snprintf(message,sizeof(message),"Forward axis inconsistent with known-left tilt; verify mount");return false;}
    next.verified=true;if(!next.valid()||!save(next))return false;calibration=next;initialized=false;
    std::snprintf(message,sizeof(message),"Rotation and gyro bias SAVED; dynamic result experimental");return true;
  }
  bool manual(Vec x,Vec y,Vec z){if(!uprightReady)return false;Calibration next=candidate;next.axes[0]=x;next.axes[1]=y;next.axes[2]=z;next.verified=true;
    if(!next.valid()||!save(next))return false;
    calibration=next;initialized=false;return true;}
  // GPS velocity vector uses both speed AND course. UTC spacing is authoritative.
  // Reception clock supplies an approximate monotonic anchor; no PPS precision claim.
  void navigation(uint32_t utc,uint32_t received,double speed,double course,bool good){
    if(!good||!std::isfinite(speed)||!std::isfinite(course)){navValid=false;aid=false;return;}
    const double radians=course*.017453292519943295;
    Vec velocity(speed/3.6*std::sin(radians),speed/3.6*std::cos(radians),0);
    if(navValid){
      const uint32_t dt=(utc+86400000-navUtc)%86400000;
      if(dt>=100 && dt<=1000){
        const Vec acceleration=(velocity-navVelocity)*(1000.0/dt);
        // Rotate horizontal EN acceleration into current forward/left course frame.
        navAcceleration={acceleration.x*std::sin(radians)+acceleration.y*std::cos(radians),
                         -acceleration.x*std::cos(radians)+acceleration.y*std::sin(radians),0};
        navAidAt=received-dt/2;navAid=true;
      }else if(dt>1000)navAid=false;
    }
    navSpeed=speed;navUtc=utc;navAt=received;navVelocity=velocity;navValid=true;
  }
  void process(const Sample& sample){
    at=sample.ms;
    guided.process(sample);
    if(!sample.valid){valid=false;rawRollValid=false;quality="IMU LOST";return;}
    capture(sample);
    const Vec a=calibration.rotate(sample.accel),g=calibration.rotate(sample.gyro-calibration.bias);
    rawRollValid=std::isfinite(a.norm())&&a.norm()>.2&&std::hypot(a.y,a.z)>.05;
    if(rawRollValid)rawRoll=std::atan2(a.y,a.z)*57.29577951308232;
    const double dt=previousUs?uint32_t(sample.us-previousUs)*1e-6:0;previousUs=sample.us;
    if(!initialized){roll=std::atan2(a.y,a.z);pitch=std::atan2(-a.x,std::hypot(a.y,a.z));initialized=true;}
    if(dt<=0||dt>.1){++gaps;valid=false;quality="DT GAP experimental";return;}
    const double factor=.017453292519943295;
    // Euler body-rate kinematics: body gz is NOT treated as vertical yaw rate.
    const double sr=std::sin(roll),cr=std::cos(roll),cp=std::cos(pitch);
    if(std::fabs(cp)<.15){valid=false;quality="ATTITUDE LIMIT experimental";return;}
    roll+=(g.x+sr*std::tan(pitch)*g.y+cr*std::tan(pitch)*g.z)*factor*dt;
    pitch+=(cr*g.y-sr*g.z)*factor*dt;
    aid=navValid&&navAid&&uint32_t(at-navAidAt)<350&&uint32_t(at-navAt)<500;
    Vec corrected=a;
    if(aid){
      // Approximate course-aligned horizontal aiding projected through attitude.
      // Ignores vertical road acceleration/slip; explicitly experimental.
      const double ax=navAcceleration.x/9.80665,ay=navAcceleration.y/9.80665;
      const Vec translation(std::cos(pitch)*ax,
        std::sin(roll)*std::sin(pitch)*ax+std::cos(roll)*ay,
        std::cos(roll)*std::sin(pitch)*ax-std::sin(roll)*ay);
      corrected=a-translation;
    }
    const bool staticAid=navValid&&navSpeed<3&&uint32_t(at-navAt)<500&&g.norm()<8;
    if((aid||staticAid)&&std::fabs(corrected.norm()-1)<.12){
      const double measured=std::atan2(corrected.y,corrected.z);
      double difference=measured-roll;while(difference>PI)difference-=2*PI;while(difference<-PI)difference+=2*PI;
      roll+=difference*std::fmin(.03,dt*.5);
      pitch+=(std::atan2(-corrected.x,std::hypot(corrected.y,corrected.z))-pitch)*std::fmin(.03,dt*.5);
    }
    longitudinalG=a.x+std::sin(pitch);
    valid=calibration.verified;
    quality=!valid?"UNCALIBRATED experimental":aid?"GNSS APPROX experimental":staticAid?"STATIC experimental":"GYRO ONLY experimental";
  }
  double rollDegrees()const{return roll*57.29577951308232;}
  void installCalibration(const Calibration& next){calibration=next;initialized=false;previousUs=0;valid=false;}
  bool displayAngle(uint32_t now,double zero,double& degrees,bool& raw)const{
    raw=false;
    if(uint32_t(now-at)>=150)return false;
    if(valid)degrees=rollDegrees()-zero;
    else if(!calibration.verified&&rawRollValid){degrees=rawRoll-zero;raw=true;}
    else return false;
    if(!std::isfinite(degrees))return false;
    degrees=std::remainder(degrees,360.0);return true;
  }
 private:
  uint32_t previousUs=0,navUtc=0,navAt=0,navAidAt=0,captureAt=0;
  bool initialized=false,navValid=false,navAid=false;
  Vec navVelocity,navAcceleration;double navSpeed=0;
  unsigned count=0;Vec sumA,sumG;double sumG2=0,sumNorm=0,sumNorm2=0;
  Calibration candidate;
  void capture(const Sample& s){
    if(!wizard)return;
    if(!captureAt)captureAt=s.us;
    ++count;sumA=sumA+s.accel;sumG=sumG+s.gyro;sumG2+=s.gyro.dot(s.gyro);
    sumNorm+=s.accel.norm();sumNorm2+=s.accel.dot(s.accel);
    if(uint32_t(s.us-captureAt)<2000000)return;
    const Vec meanA=sumA*(1.0/count),meanG=sumG*(1.0/count);
    const double varianceG=sumG2/count-meanG.dot(meanG);
    const double varianceA=sumNorm2/count-(sumNorm/count)*(sumNorm/count);
    const unsigned stage=wizard;wizard=0;
    if(count<75||varianceG>.25||varianceA>.0004||std::fabs(meanA.norm()-1)>.1||meanG.norm()>8){std::snprintf(message,sizeof(message),"REJECTED unstable capture");return;}
    if(stage==1){candidate=calibration;candidate.bias=meanG;candidate.axes[2]=meanA.unit();uprightReady=true;leftReady=false;std::snprintf(message,sizeof(message),"Upright captured; lean LEFT 10..70 degrees, then imu left");}
    else {
      if(!uprightReady){std::snprintf(message,sizeof(message),"Upright capture required first");return;}
      const Vec lateral=meanA.unit()-candidate.axes[2]*meanA.unit().dot(candidate.axes[2]);
      const double angle=std::acos(std::fmax(-1.0,std::fmin(1.0,meanA.unit().dot(candidate.axes[2]))))*57.2957795;
      if(angle<10||angle>70){std::snprintf(message,sizeof(message),"REJECTED: left tilt must be 10..70 degrees");return;}
      candidate.axes[1]=lateral.unit();candidate.axes[0]=candidate.axes[1].cross(candidate.axes[2]).unit();leftReady=true;
      std::snprintf(message,sizeof(message),"Left captured; verify sensor forward axis: imu forward x+|x-|y+|y-|z+|z-");
    }
  }
};
inline bool axis(const char* text,Vec& v){
  if(!text||std::strlen(text)!=2||(text[1]!='+'&&text[1]!='-'))return false;
  const double sign=text[1]=='+'?1:-1;v=Vec();
  if(text[0]=='x')v.x=sign;else if(text[0]=='y')v.y=sign;else if(text[0]=='z')v.z=sign;else return false;return true;
}
}
