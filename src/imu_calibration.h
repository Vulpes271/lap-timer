#pragma once
#include <cmath>
#include <cstdint>
#include <cstdio>
namespace Imu {
struct Vec {
  double x=0,y=0,z=0;
  Vec()=default;Vec(double a,double b,double c):x(a),y(b),z(c){}
  Vec operator+(Vec v) const{return {x+v.x,y+v.y,z+v.z};}
  Vec operator-(Vec v) const{return {x-v.x,y-v.y,z-v.z};}
  Vec operator*(double a) const{return {x*a,y*a,z*a};}
  double dot(Vec v) const{return x*v.x+y*v.y+z*v.z;}
  Vec cross(Vec v)const{return {y*v.z-z*v.y,z*v.x-x*v.z,x*v.y-y*v.x};}
  double norm()const{return std::sqrt(dot(*this));}
  Vec unit()const{const double n=norm();return n>1e-9?*this*(1/n):Vec();}
};
struct Calibration {
  uint32_t version=1;
  Vec axes[3]={{1,0,0},{0,1,0},{0,0,1}};
  Vec bias;
  bool verified=false;
  bool valid()const{
    for(const auto& a:axes)if(!std::isfinite(a.x)||!std::isfinite(a.y)||!std::isfinite(a.z)||std::fabs(a.norm()-1)>.01)return false;
    return version==1 && std::fabs(axes[0].dot(axes[1]))<.01 && std::fabs(axes[0].dot(axes[2]))<.01 &&
      std::fabs(axes[1].dot(axes[2]))<.01 && axes[0].cross(axes[1]).dot(axes[2])>.99 &&
      std::isfinite(bias.norm()) && bias.norm()<20;
  }
  Vec rotate(Vec v)const{return {axes[0].dot(v),axes[1].dot(v),axes[2].dot(v)};}
};
struct Sample {uint32_t us=0,ms=0;Vec accel,gyro;bool valid=false;};

inline double angleBetween(Vec a,Vec b){return std::acos(std::fmax(-1.0,std::fmin(1.0,a.unit().dot(b.unit()))))*57.29577951308232;}
struct Pose {Vec gravity,gyro;};
// Statistics tolerate sensor noise but reject an orientation change across the window.
class StableCapture {
 public:
  Pose pose;
  double gyroNoise=0,accelNoise=0,driftDegrees=0;
  char reason[64]="";
  void reset(){count=0;started=false;sumA=sumG=firstA=lastA=Vec();sumG2=sumN=sumN2=0;progress=0;}
  unsigned percent()const{return progress;}
  int feed(const Sample& s,Vec bias=Vec(),bool knownBias=false){
    if(!s.valid||!std::isfinite(s.accel.norm())||!std::isfinite(s.gyro.norm())){reset();std::snprintf(reason,sizeof(reason),"IMU DATA LOST");return 0;}
    if(started&&uint32_t(s.us-lastUs)>120000){reset();std::snprintf(reason,sizeof(reason),"SAMPLE GAP - HOLD POSITION");}
    if(!started){at=s.us;started=true;}
    lastUs=s.us;++count;sumA=sumA+s.accel;sumG=sumG+s.gyro;sumG2+=s.gyro.dot(s.gyro);
    const double norm=s.accel.norm();sumN+=norm;sumN2+=norm*norm;
    // Smooth the beginning/end direction so electrical noise is not treated as motion.
    if(count<=15)firstA=firstA+s.accel;
    lastA=count==1?s.accel:lastA*.85+s.accel*.15;
    const uint32_t elapsed=s.us-at;progress=unsigned(std::fmin(100.0,elapsed/20000.0));
    if(elapsed<2000000)return 0;
    pose.gravity=sumA*(1.0/count);pose.gyro=sumG*(1.0/count);
    gyroNoise=std::sqrt(std::fmax(0.0,sumG2/count-pose.gyro.dot(pose.gyro)));
    accelNoise=std::sqrt(std::fmax(0.0,sumN2/count-(sumN/count)*(sumN/count)));
    driftDegrees=angleBetween(firstA,lastA);
    const double rotation=(pose.gyro-(knownBias?bias:Vec())).norm();
    const bool good=count>=60&&gyroNoise<=3.0&&accelNoise<=.06&&driftDegrees<=3.0&&
      std::fabs(pose.gravity.norm()-1)<=.18&&rotation<=(knownBias?3.0:8.0);
    if(good){std::snprintf(reason,sizeof(reason),"OK");return 1;}
    std::snprintf(reason,sizeof(reason),"%s",count<60?"TOO FEW SAMPLES":driftDegrees>3?"POSITION CHANGED":rotation>(knownBias?3:8)?"ROTATION DETECTED":gyroNoise>3?"GYRO MOVEMENT":accelNoise>.06?"ACCELERATION DETECTED":"CHECK SENSOR / GRAVITY");
    reset();return -1;
  }
 private:
  uint32_t at=0,lastUs=0;unsigned count=0,progress=0;bool started=false;
  Vec sumA,sumG,firstA,lastA;double sumG2=0,sumN=0,sumN2=0;
};
inline bool threePoseCalibration(const Pose& upright,const Pose& left,const Pose& right,Calibration& result,char* error,size_t size){
  const Vec z=upright.gravity.unit(),l=left.gravity.unit(),r=right.gravity.unit();
  const double la=angleBetween(z,l),ra=angleBetween(z,r);
  const Vec lp=l-z*l.dot(z),rp=r-z*r.dot(z);
  if(la<5||la>70||ra<5||ra>70){std::snprintf(error,size,"LEAN 5 TO 70 DEGREES EACH SIDE");return false;}
  if(lp.unit().dot(rp.unit())>-.90){std::snprintf(error,size,"RIGHT MUST BE OPPOSITE TO LEFT");return false;}
  const Vec y=(lp.unit()-rp.unit()).unit(),x=y.cross(z).unit();
  Calibration next;next.axes[0]=x;next.axes[1]=z.cross(x).unit();next.axes[2]=z;
  next.bias=upright.gyro;next.verified=true;
  if(!next.valid()){std::snprintf(error,size,"INVALID SENSOR AXES");return false;}
  result=next;error[0]=0;return true;
}
class GuidedCalibration {
 public:
  enum Stage : uint8_t { Idle, Upright, MeasureUpright, UprightOk, Left, MeasureLeft, LeftOk, Right, MeasureRight, RightOk, SaveReady, Done, Failed };
  Stage stage=Idle;StableCapture window;Calibration candidate;
  char detail[80]="";
  bool active()const{return stage!=Idle;}
  bool measuring()const{return stage==MeasureUpright||stage==MeasureLeft||stage==MeasureRight;}
  void begin(){stage=Upright;window.reset();detail[0]=0;}
  void cancel(){stage=Idle;window.reset();}
  void advance(){
    if(stage==Upright||stage==Left||stage==Right){stage=Stage(unsigned(stage)+1);window.reset();detail[0]=0;}
  }
  const char* title()const{
    if(stage<=UprightOk)return "1 / 3  MOTOR UPRIGHT";
    if(stage<=LeftOk)return "2 / 3  LEAN MOTOR LEFT";
    if(stage<=RightOk)return "3 / 3  LEAN MOTOR RIGHT";
    return stage==Done?"CALIBRATION OK":stage==Failed?"CALIBRATION NOT SAVED":"SAVING CALIBRATION";
  }
  const char* instruction()const{
    if(measuring())return "HOLD POSITION - SMALL NOISE IS OK";
    if(stage==UprightOk||stage==LeftOk||stage==RightOk)return "OK";
    if(stage==Done)return "AXES AND ZERO SAVED";
    if(stage==Failed)return "PREVIOUS CALIBRATION KEPT";
    return stage==Upright?"MOTOR STRAIGHT - GREEN TO MEASURE":"LEAN 5 TO 70 DEGREES - GREEN TO MEASURE";
  }
  void saved(bool success){stage=success?Done:Failed;}
  void process(const Sample& sample){
    if(stage==UprightOk||stage==LeftOk||stage==RightOk){
      if(uint32_t(sample.ms-okAt)>=900)stage=stage==UprightOk?Left:stage==LeftOk?Right:SaveReady;
      return;
    }
    if(!measuring())return;
    const int result=window.feed(sample,upright.gyro,stage!=MeasureUpright);
    if(result<0){std::snprintf(detail,sizeof(detail),"%s - RETRYING",window.reason);return;}
    if(!result)return;
    if(stage==MeasureUpright){upright=window.pose;stage=UprightOk;}
    else if(stage==MeasureLeft){
      const double angle=angleBetween(upright.gravity,window.pose.gravity);
      if(angle<5||angle>70){stage=Left;std::snprintf(detail,sizeof(detail),"LEAN FURTHER LEFT: 5 TO 70 DEGREES");return;}
      left=window.pose;stage=LeftOk;
    }else{
      if(!threePoseCalibration(upright,left,window.pose,candidate,detail,sizeof(detail))){stage=Right;return;}
      stage=RightOk;
    }
    detail[0]=0;okAt=sample.ms;
  }
 private:
  Pose upright,left;uint32_t okAt=0;
};
}
