#pragma once
#include "control_model.h"

// Electrical levels have already passed the 30 ms acquisition debounce.
struct PanelState { bool green=false, red=false, left=false, right=false; };
struct PanelMotion { bool lapRunning=false, speedFresh=false; double speedKmh=0; };
enum class PanelAction : uint8_t { None, Green, Entered, Exited, Blocked, Menu, Step };

class PanelNavigation {
 public:
  bool redArmed() const{return armed;}
  bool redOn() const{return state.red;}
  bool greenDown() const{return state.green;}
  void begin(PanelState initial,uint32_t now){
    state=initial;redReady=!initial.red;armed=false;greenReady=!initial.green;
    tracking=longUsed=entryCandidate=repeat=false;gestureDirection=0;pressedAt=repeatAt=now;
  }
  PanelAction update(PanelState next,uint32_t at,Control::Model& m,const PanelMotion& motion){
    PanelAction result=PanelAction::None;
    if(unsafe(motion)){
      entryCandidate=false;
      if(menuOpen(m)){leave(m,at);armed=redReady=false;tracking=repeat=false;longUsed=true;result=PanelAction::Exited;}
    }
    const auto before=state;state=next;
    if(before.red!=next.red){
      if(!next.red){
        redReady=true;armed=false;entryCandidate=false;
        if(m.settingsOn){leave(m,at);longUsed=true;tracking=repeat=false;result=PanelAction::Exited;}
      }else if(redReady){armed=true;redReady=false;}
    }
    // A held press may never become a confirm by moving the direction to CENTER.
    if(tracking && before.green && direction(next)!=direction(before)){
      repeat=false;longUsed=true;entryCandidate=false;
    }
    if(before.green!=next.green){
      if(next.green&&greenReady){
        tracking=true;longUsed=false;repeat=false;gestureDirection=0;pressedAt=at;repeatAt=at+600;
        entryCandidate=armed&&next.red&&direction(next)==0&&!unsafe(motion)&&!m.unacknowledged();
        if(menuOpen(m)&&!m.unacknowledged()&&direction(next)!=0){
          gestureDirection=direction(next);longUsed=true;entryCandidate=false;
          if(gestureDirection!=2){
            m.tap(gestureDirection>0,at);
            repeat=m.treeMenu&&m.numericAdjusting();
            result=PanelAction::Step;
          }
        }
      }else if(!next.green){
        if(tracking&&!gestureDirection&&!longUsed&&uint32_t(at-pressedAt)>=2000)result=hold(m,at,motion);
        if(tracking&&!gestureDirection&&!longUsed)result=PanelAction::Green;
        greenReady=true;tracking=repeat=false;
      }
    }
    return result;
  }
  PanelAction tick(uint32_t now,Control::Model& m,const PanelMotion& motion){
    if(m.settingsSuppressed&&state.red){armed=redReady=entryCandidate=false;}
    if(m.unacknowledged())repeat=false;
    if(unsafe(motion)){
      entryCandidate=false;repeat=false;
      if(menuOpen(m)){leave(m,now);armed=redReady=false;longUsed=true;tracking=false;return PanelAction::Exited;}
    }
    if(tracking&&state.green&&repeat&&menuOpen(m)&&!m.unacknowledged()&&m.numericAdjusting()&&
       direction(state)==gestureDirection&&int32_t(now-repeatAt)>=0){
      // No catch-up burst after a display/SD stall: at most one step per tick.
      m.tap(gestureDirection>0,now);
      const uint32_t interval=repeatInterval(m);
      // Keep cadence through ordinary render jitter; discard backlog after a stall.
      repeatAt=uint32_t(now-repeatAt)>=interval?now+interval:repeatAt+interval;
      return PanelAction::Step;
    }
    if(tracking&&state.green&&!gestureDirection&&!longUsed&&uint32_t(now-pressedAt)>=2000)return hold(m,now,motion);
    return PanelAction::None;
  }
 private:
  PanelState state;
  bool redReady=true,armed=false,greenReady=true,tracking=false,longUsed=false,entryCandidate=false,repeat=false;
  int gestureDirection=0;
  uint32_t pressedAt=0,repeatAt=0;
  static int direction(const PanelState& s){return s.left?(s.right?2:-1):(s.right?1:0);}
  static bool menuOpen(const Control::Model& m){return m.settingsOn&&!m.settingsSuppressed;}
  static bool unsafe(const PanelMotion& m){return m.lapRunning||(m.speedFresh&&std::isfinite(m.speedKmh)&&m.speedKmh>5);}
  static uint32_t repeatInterval(const Control::Model& m){return m.menu==Control::FrontRange||m.menu==Control::RearRange?80:250;}
  static void leave(Control::Model& m,uint32_t now){
    m.input(Control::Input::SettingsOff,now);m.base=Control::Base::Race;
    m.quickActive=false;m.source().session.hold=false;m.direction=0;
  }
  PanelAction hold(Control::Model& m,uint32_t now,const PanelMotion& motion){
    longUsed=true;
    if(m.unacknowledged())return PanelAction::Blocked;
    if(menuOpen(m)){
      if(m.treeMenu){m.treeLong(now);return PanelAction::Menu;}
      leave(m,now);armed=redReady=false;return PanelAction::Exited;
    }
    if(entryCandidate&&armed&&state.red&&direction(state)==0&&!unsafe(motion)&&!m.unacknowledged()){
      m.openTree(now);m.quickActive=false;m.direction=0;return PanelAction::Entered;
    }
    return PanelAction::Blocked;
  }
};
