#pragma once
#include "quick_button.h"

template<class Check> inline void runPanelSelfTests(Control::Model& m,Check check){
  using namespace Control;
  LevelDebouncer level;level.begin(false,0);
  check(!level.update(true,10)&&!level.update(true,39)&&level.update(true,40),"panel contact needs stable 30 ms");
  check(!level.update(false,50)&&level.update(false,80),"panel release needs stable 30 ms");
  PanelNavigation nav;PanelState s;PanelMotion motion;uint32_t now=0;
  auto reset=[&](PanelState initial=PanelState()){
    m=Model();s=initial;motion=PanelMotion();now=0;nav.begin(s,now);
  };
  auto update=[&](uint32_t at){const auto a=nav.update(s,at,m,motion);if(a==PanelAction::Green)m.input(Input::Quick,at);return a;};
  auto dir=[&](int direction){s.left=direction<0||direction==2;s.right=direction>0;return update(now+=50);};
  auto shortGreen=[&](){s.green=true;update(now+=50);s.green=false;return update(now+=50);};
  auto longGreen=[&](){s.green=true;update(now+=50);const auto a=nav.tick(now+=2000,m,motion);s.green=false;update(now+=50);return a;};
  auto redOn=[&](){s.red=true;update(now+=50);};auto redOff=[&](){s.red=false;update(now+=50);};
  auto move=[&](bool right){dir(right?1:-1);shortGreen();dir(0);};
  auto enter=[&](){redOn();return longGreen();};
  auto setting=[&](unsigned category,unsigned item){m.menuCategory=category;m.menuDepth=1;for(unsigned row=1;row<categorySize(category);++row)if(categoryItem(category,row)==item){m.menuRow=row;break;}m.menu=item;};
  reset();s.green=true;update(10);check(!m.quickActive,"center green waits for release before short action");s.green=false;update(100);
  check(m.view(100)==View::LastLap,"normal green still opens quick review");
  reset();dir(1);shortGreen();check(m.view(now)==View::LastLap&&m.base==Base::Race,"direction outside settings does not hijack green review");
  reset();redOn();check(!m.settingsOn&&nav.redArmed(),"red alone only arms entry");
  s.green=true;update(now+=50);check(nav.tick(now+1999,m,motion)==PanelAction::None,"entry requires full two second hold");
  check(nav.tick(now+=2000,m,motion)==PanelAction::Entered&&m.treeMenu&&m.menuDepth==0,"center green hold opens categories");s.green=false;update(now+=50);
  check(!m.editing&&m.menuRow==0,"release after entry hold is consumed");
  check(!std::strcmp(m.treeLabel(0),"BACK")&&!std::strcmp(m.treeLabel(1),"DISPLAY"),"BACK is first root row");
  reset();enter();setting(4,FrontRange);m.config.frontMin=2;m.config.frontMax=3;shortGreen();move(true);shortGreen();
  check(m.editing&&m.numericEditor()&&m.rangePart==0,"pressure opens a dedicated MIN value editor");
  dir(1);s.green=true;check(update(now+=50)==PanelAction::Step&&std::fabs(m.draft.frontMin-2.05)<.0001,"numeric step occurs immediately on green press");
  const uint32_t press=now;check(nav.tick(press+599,m,motion)==PanelAction::None,"numeric repeat waits 600 ms");
  check(nav.tick(press+600,m,motion)==PanelAction::Step&&std::fabs(m.draft.frontMin-2.10)<.0001,"numeric hold repeats after delay");
  check(nav.tick(press+679,m,motion)==PanelAction::None&&nav.tick(press+680,m,motion)==PanelAction::Step&&std::fabs(m.draft.frontMin-2.15)<.0001,"pressure repeat interval is 80 ms");
  nav.tick(press+2680,m,motion);check(std::fabs(m.draft.frontMin-2.20)<.0001,"late poll adds one step without catch-up burst");
  now=press+2730;s.green=false;update(now);check(m.editing&&m.config.frontMin==2&&std::fabs(m.draft.frontMin-2.20)<.0001,"release after repeat neither adds step nor saves");
  dir(0);shortGreen();check(m.editing&&m.rangePart==1&&m.config.frontMin==2,"pressure MIN confirmation advances to MAX without partial save");
  move(false);shortGreen();check(!m.editing&&m.menuConfirmed&&std::fabs(m.config.frontMin-2.20)<.0001&&std::fabs(m.config.frontMax-2.95)<.0001,"MAX confirmation saves valid pair and returns to list");
  reset();enter();setting(4,FrontRange);shortGreen();move(true);shortGreen();dir(1);s.green=true;update(now+=50);const uint32_t fastPress=now;
  for(uint32_t elapsed=600;elapsed<=3640;elapsed+=80)nav.tick(fastPress+elapsed,m,motion);
  check(std::fabs(m.draft.frontMin-2.0)<.0001&&m.config.frontMin==0,"pressure reaches 2 bar from zero within 3.64 seconds without saving");
  reset();enter();setting(4,FrontRange);shortGreen();move(true);shortGreen();dir(1);s.green=true;update(now+=50);const uint32_t jitterPress=now;
  nav.tick(jitterPress+615,m,motion);nav.tick(jitterPress+695,m,motion);
  check(nav.tick(jitterPress+760,m,motion)==PanelAction::Step&&std::fabs(m.draft.frontMin-.20)<.0001,
    "render jitter does not accumulate into slower pressure repeat cadence");
  reset();enter();setting(4,FrontRange);shortGreen();move(true);shortGreen();m.draft.frontMin=19.99;move(true);check(m.draft.frontMin==20,"pressure increment clamps at 20 bar");m.draft.frontMin=.01;move(false);check(m.draft.frontMin==0,"pressure decrement clamps at zero");
  dir(1);s.green=true;update(now+=50);const float stopped=m.draft.frontMin;dir(-1);nav.tick(now+=1000,m,motion);s.green=false;update(now+=50);
  check(m.draft.frontMin==stopped&&m.editing,"direction change cancels repeat until a fresh green press");
  dir(2);shortGreen();check(m.draft.frontMin==stopped&&m.editing,"both directions closed neither steps nor confirms");
  dir(0);redOff();check(!m.settingsOn&&!m.editing&&m.config.frontMin==0,"red OFF exits and discards numeric draft");
  reset();enter();setting(0,LapHold);shortGreen();move(true);shortGreen();m.draft.holdSeconds=15;move(true);check(m.draft.holdSeconds==15,"numeric hold duration respects upper bound");
  dir(-1);s.green=true;update(now+=50);motion.speedFresh=true;motion.speedKmh=20;nav.tick(now+=1000,m,motion);
  check(!m.settingsOn&&!m.editing&&m.config.holdSeconds==5&&!nav.redArmed(),"motion cancels repeat, discards draft and closes menu");
  reset();enter();setting(0,LapHold);shortGreen();move(true);shortGreen();dir(1);s.green=true;update(0xFFFFFF00);
  check(nav.tick(344,m,motion)==PanelAction::Step&&m.draft.holdSeconds==7,"numeric repeat delay tolerates millis rollover");
  check(nav.tick(593,m,motion)==PanelAction::None&&nav.tick(594,m,motion)==PanelAction::Step&&m.draft.holdSeconds==8,"repeat cadence tolerates millis rollover");
  reset();enter();setting(0,LapHold);shortGreen();move(true);shortGreen();dir(1);s.green=true;update(now+=50);m.real.alarms[0][0].active=true;const auto alarmValue=m.draft.holdSeconds;nav.tick(now+=1000,m,motion);
  check(m.draft.holdSeconds==alarmValue,"new alarm stops numeric repeat");s.green=false;update(now+=50);shortGreen();check(!m.unacknowledged()&&!m.quickActive,"green still acknowledges alarm with direction held");
  reset();enter();shortGreen();nav.tick(now,m,motion);check(m.settingsSuppressed&&!nav.redArmed(),"root BACK consumes red arming");check(longGreen()==PanelAction::Blocked,"red still ON cannot reopen after BACK");redOff();check(enter()==PanelAction::Entered,"red OFF ON allows fresh entry");
  reset({true,true,false,false});s.green=false;update(10);s.green=true;update(20);check(nav.tick(2020,m,motion)==PanelAction::Blocked,"startup-held red requires OFF ON");
  reset({true,false,false,false});s.green=false;update(10);check(!m.quickActive,"startup-held green requires release");
  reset();dir(1);redOn();check(longGreen()==PanelAction::Blocked,"menu entry requires direction CENTER");
  reset();s.green=true;update(10);s.red=true;update(20);check(nav.tick(2010,m,motion)==PanelAction::Blocked,"green held before red cannot enter");
  reset();motion.speedFresh=true;motion.speedKmh=6;check(enter()==PanelAction::Blocked,"fresh speed over 5 kmh blocks entry");
  reset();motion.lapRunning=true;check(enter()==PanelAction::Blocked,"active real lap blocks entry");
  reset();motion.speedFresh=false;motion.speedKmh=100;check(enter()==PanelAction::Entered,"missing GPS permits deliberate center red plus hold entry");
  reset();m.real.alarms[0][0].active=true;check(enter()==PanelAction::Blocked,"alarm cannot be covered by menu entry");
}
