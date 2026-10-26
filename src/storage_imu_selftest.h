#pragma once
#include <Arduino.h>
#include <memory>
#include "control_model.h"
#include "imu_calibration.h"

// Synthetic tests: no SD erase, NVS writes, or changes to the live calibration.
inline bool runStorageImuSelfTests(Stream& out){
  unsigned passed=0,failed=0;
  auto check=[&](bool ok,const char* name){out.printf("SD/IMU TEST %s: %s\n",ok?"PASS":"FAIL",name);ok?++passed:++failed;};
  using namespace Imu;
  StableCapture window;int result=0;
  for(unsigned i=0;i<=100;++i){const double n=std::sin(i*1.7);Sample s;s.valid=true;s.us=1000+i*20000;s.accel={.018*n,.012*n,1+.012*n};s.gyro={.8*n,.6*n,.4*n};result=window.feed(s);}
  check(result==1,"stationary sensor noise accepted over two seconds");
  window.reset();
  for(unsigned i=0;i<=100;++i){const double a=i*.15*.017453292519943295;Sample s;s.valid=true;s.us=1000+i*20000;s.accel={0,std::sin(a),std::cos(a)};s.gyro={7.5,0,0};result=window.feed(s);}
  check(result==-1,"real orientation drift rejected");
  window.reset();
  for(unsigned i=0;i<=100;++i){Sample s;s.valid=true;s.us=1000+i*20000;s.accel={0,0,1};s.gyro={4,0,0};result=window.feed(s,Vec(),true);}
  check(result==-1,"rotation above known bias rejected");
  window.reset();Sample s;s.valid=true;s.us=1000;s.accel={0,0,1};window.feed(s);s.us=2001000;window.feed(s);
  check(window.percent()==0,"sample gap restarts stability window");
  s.valid=false;window.feed(s);check(window.percent()==0,"lost sensor cannot complete capture");
  const Vec z=Vec(1,2,3).unit(),x=Vec(2,-1,0).unit(),y=z.cross(x).unit();
  Pose upright{z,{.4,-.2,.1}},left{z*std::cos(.3)+y*std::sin(.3),upright.gyro},right{z*std::cos(.5)-y*std::sin(.5),upright.gyro};
  Calibration calibration;char error[80];
  check(threePoseCalibration(upright,left,right,calibration,error,sizeof(error)),"three poses accept arbitrary sensor mounting and unequal tilts");
  check(calibration.axes[0].dot(x)>.999999&&calibration.axes[1].dot(y)>.999999&&calibration.axes[2].dot(z)>.999999,"motorcycle axes recovered");
  check(calibration.rotate(left.gravity).y>0&&calibration.rotate(right.gravity).y<0,"left and right lean signs preserved");
  check((calibration.bias-upright.gyro).norm()<1e-9&&calibration.verified,"upright gyro bias retained");
  Calibration previous=calibration;
  check(!threePoseCalibration(upright,left,left,calibration,error,sizeof(error)),"same-side lean rejected");
  check((calibration.axes[0]-previous.axes[0]).norm()<1e-9,"failed geometry leaves previous calibration unchanged");
  check(!threePoseCalibration(upright,Pose{z,upright.gyro},right,calibration,error,sizeof(error)),"insufficient tilt rejected");
  Pose skew{z*std::cos(.4)+x*std::sin(.4),upright.gyro};
  check(!threePoseCalibration(upright,left,skew,calibration,error,sizeof(error)),"inconsistent roll plane rejected");
  GuidedCalibration wizard;uint32_t tick=1000;wizard.begin();
  auto capture=[&](const Pose& pose){wizard.advance();for(unsigned i=0;i<=100;++i){Sample sample;sample.valid=true;sample.us=tick;sample.ms=tick/1000;sample.accel=pose.gravity;sample.gyro=pose.gyro;wizard.process(sample);tick+=20000;}};
  auto waitOk=[&](){tick+=900000;Sample sample;sample.ms=tick/1000;wizard.process(sample);};
  capture(upright);check(wizard.stage==GuidedCalibration::UprightOk,"upright gets explicit OK");waitOk();
  check(wizard.stage==GuidedCalibration::Left,"next instruction requests left lean");capture(left);waitOk();
  check(wizard.stage==GuidedCalibration::Right,"next instruction requests right lean");capture(right);
  check(wizard.stage==GuidedCalibration::RightOk,"all three poses required before save");waitOk();
  check(wizard.stage==GuidedCalibration::SaveReady&&wizard.candidate.valid(),"complete wizard produces valid candidate");wizard.saved(true);
  check(wizard.stage==GuidedCalibration::Done,"successful save completes wizard");wizard.begin();wizard.cancel();
  check(!wizard.active(),"cancel leaves live calibration untouched");
  check(Sd::formatAllowed(false,false,false,false,false,false),"format allowed only with stopped real acquisition");
  for(unsigned i=0;i<6;++i){bool f[6]={};f[i]=true;check(!Sd::formatAllowed(f[0],f[1],f[2],f[3],f[4],f[5]),"format safety condition blocks erase");}
  std::unique_ptr<Control::Model> holder(new Control::Model());auto& m=*holder;
  m.openTree(1);m.menuDepth=1;m.menu=Control::SdFormat;m.treeLong(2);
  check(m.editing&&m.editRow==0&&m.action==0,"format editor defaults to BACK");m.treeShort(3);
  check(!m.editing&&m.action==0,"confirming BACK never requests erase");m.treeLong(4);m.editRow=1;m.treeShort(5);
  check(m.action==0&&!m.editing,"NO never requests erase");m.treeLong(6);m.editRow=2;m.treeShort(7);
  check(m.action==Control::SdFormat&&m.specialPage==Control::SdFormat,"explicit ERASE selection requests format (not executed by test)");
  m.action=0;m.specialPage=Control::SdFiles;m.fileFirst=0;m.filePage.count=5;m.filePage.total=12;m.fileCursor=6;m.specialSelect();
  check(m.fileFirst==5&&m.filesLoading&&m.action==Control::SdFiles,"file listing next page");
  m.filesLoading=false;m.fileCursor=6;m.specialSelect();check(m.fileFirst==0,"file listing previous page");
  std::snprintf(m.filePage.entries[0].name,sizeof(m.filePage.entries[0].name),"/20261025/session/s000001.csv");char label[80];m.fileRowLabel(1,label,sizeof(label));
  check(!std::strcmp(label,"s000001.csv"),"filename stays visible without long directory prefix");
  m.filesLoading=false;m.filePage=Sd::Page();m.fileCursor=0;m.specialSelect();check(m.specialPage==0,"empty or absent card retains BACK navigation");
  out.printf("SD/IMU SELFTEST: %u passed, %u failed (no card erased, no calibration saved)\n",passed,failed);return failed==0;
}
