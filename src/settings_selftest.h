#pragma once
#include <Arduino.h>
#include <memory>
#include "control_model.h"
#include "panel_navigation.h"

// Focused checks for the settings cleanup. No hardware, SD or NVS writes.
inline bool runSettingsSelfTests(Stream& out){
  using namespace Control;
  unsigned passed=0,failed=0;
  auto check=[&](bool ok,const char* name){out.printf("SETTINGS %s: %s\n",ok?"PASS":"FAIL",name);ok?++passed:++failed;};
  std::unique_ptr<Model> holder(new Model());auto& m=*holder;
  auto click=[&](){m.input(Input::Quick,10);};
  auto open=[&](Category category,unsigned item){
    m=Model();m.openTree(1);
    for(unsigned i=0;i<=unsigned(category);++i)m.tap(true,2);
    click();
    for(unsigned row=1;row<m.treeCount();++row){m.tap(true,3);if(m.menu==item)break;}
    click();
  };
  m.openTree(0);check(m.menuRow==0&&!std::strcmp(m.treeLabel(0),"BACK"),"root opens with BACK selected");click();
  check(m.settingsSuppressed&&m.view(1)==View::Race,"immediate root BACK exits settings");
  bool backFirst=true,returns=true;
  for(unsigned category=0;category<6;++category){
    m=Model();m.openTree(0);for(unsigned i=0;i<=category;++i)m.tap(true,1);click();
    backFirst&=m.menuDepth==1&&m.menuCategory==category&&m.menuRow==0&&m.menu==Back;
    click();returns&=m.menuDepth==0&&m.menuRow==category+1;
  }
  check(backFirst,"all six categories open on BACK");check(returns,"return selects the originating category");
  open(Category::Display,QuickInfoMenu);check(m.menuDepth==2&&m.menuCategory==unsigned(Category::QuickInfo)&&m.menu==Back,"QUICK INFO is a submenu with BACK first");
  click();check(m.menuDepth==1&&m.menu==QuickInfoMenu&&m.menuRow==2,"nested BACK restores QUICK INFO selection");
  click();m.tap(true,4);click();
  check(m.menu==Quick1&&m.editing&&m.editRow==0&&!std::strcmp(menuName(Quick1),"QUICK INFO 1"),"quick choice editor opens on BACK with unambiguous name");
  const Config before=m.config;char label[80];m.choiceLabel(0,label,sizeof(label));
  check(!std::strcmp(label,"BACK")&&m.config.quickSlots[0]==before.quickSlots[0],"BACK is separate from stored quick value");
  m.tap(false,5);check(m.editRow==m.choiceCount()&&m.draft.quickSlots[0]==before.quickSlots[0],"highlighting an option does not mutate config or draft");
  m.tap(true,6);click();check(!m.editing&&!std::memcmp(&m.config,&before,sizeof(before)),"BACK cancels shortcut selection without changing config");
  click();m.tap(true,7);m.tap(true,8);click();
  check(m.config.quickSlots[0]==unsigned(QuickPage::Rear)&&m.menu==Quick1&&!m.editing,"choice row maps to the correct quick value and returns to parent");
  m.input(Input::SettingsOff,20);m.input(Input::Quick,30);
  check(m.view(30)==View::Rear&&m.view(5030)==View::Race,"quick mapping keeps five-second return");
  open(Category::Display,Background);m.tap(true,1);m.tap(true,2);click();
  check(m.config.theme==1&&!m.editing,"choice offset correctly saves WHITE");
  open(Category::Units,SpeedUnit);m.config.speed=1;m.draft=m.config;click();
  check(m.config.speed==1&&!m.editing,"BACK does not overwrite a non-default unit");
  open(Category::Track,Track);click();check(m.config.track==0&&!m.editing,"track BACK leaves stored track unchanged");
  open(Category::Tpms,FrontRange);check(m.editing&&!m.numericActive&&m.editRow==0,"numeric editor opens on BACK before value adjustment");click();
  check(!m.editing&&m.config.frontMin==0,"numeric BACK cancels immediately");
  open(Category::Tpms,FrontRange);m.tap(true,1);click();
  check(m.numericAdjusting(),"explicit EDIT VALUE starts numeric adjustment");
  PanelNavigation panel;PanelState state;state.red=true;state.right=true;panel.begin(state,0);PanelMotion motion;
  state.green=true;panel.update(state,100,m,motion);
  for(uint32_t elapsed=600;elapsed<=3640;elapsed+=80)panel.tick(100+elapsed,m,motion);
  check(std::fabs(m.draft.frontMin-2)<1e-5&&m.config.frontMin==0,"pressure repeat still reaches 2 bar in 3.64 seconds without saving");
  m.input(Input::SettingsOff,4000);check(!m.numericActive&&m.config.frontMin==0,"RED OFF still discards numeric draft");
  open(Category::Storage,SdFiles);check(m.fileCursor==0&&m.specialPage==SdFiles,"SD files opens on BACK even while loading");click();
  check(m.specialPage==0&&m.menu==SdFiles,"SD BACK works before listing completes");
  open(Category::Storage,SdFiles);m.filesLoading=false;m.filePage.count=5;m.filePage.total=12;
  m.fileCursor=6;click();check(m.fileFirst==5&&m.fileCursor==0&&m.action==SdFiles,"next file page resets selection to BACK");
  open(Category::Calibration,ImuWizard);click();
  check(m.specialPage==0&&m.action==ImuWizardCancel,"wizard BACK requests safe cancellation");
  open(Category::Storage,SdFormat);click();check(m.action==0&&!m.editing,"format BACK never requests erase");
  click();m.tap(true,1);click();check(m.action==0&&!m.editing,"format NO keeps card intact");
  click();m.tap(true,1);m.tap(true,2);click();check(m.action==SdFormat,"format still requires explicit ERASE choice");
  open(Category::Display,LapDeltaMenu);check(m.menu==Back&&m.menuCategory==unsigned(Category::LapDelta),"LAP DELTA groups the single indicator setting");
  m.tap(true,1);click();m.tap(true,2);m.tap(true,3);m.tap(true,4);click();
  check(m.config.deltaIndex==2&&m.deltaStepMs()==500,"SECONDS PER LIGHT maps to existing deltaIndex");
  bool boundaries=true;
  for(double sign:{-1.0,1.0})for(double step:{100.0,500.0}){
    for(unsigned level=1;level<=3;++level){const double ms=level*step;
      boundaries&=deltaLights(sign*ms,step)==level&&deltaLights(sign*(ms-.001),step)==level-1;
    }
    boundaries&=deltaLights(sign*(4*step),step)==3&&deltaLights(sign*.99*step,step)==0;
  }
  check(boundaries,"positive and negative light thresholds use floor with no rounding");
  m.real.session.deltaValid=true;
  check(m.segments(1000)==deltaLights(1000,500)&&!deltaLights(500,500,false),"model and display share light calculation; invalid reference stays neutral");
  Config saved;m.config=saved;m.config.quickSlots[0]=7;m.config.deltaIndex=3;m.config.speed=1;const auto retained=m.config;
  m.openTree(1);click();check(!std::memcmp(&retained,&m.config,sizeof(retained))&&m.config.version==4,"navigation preserves existing NVS configuration layout and values");
  // Entry/exit gestures are unchanged: center + RED edge + 2-second GREEN.
  m=Model();state=PanelState();panel.begin(state,0);state.red=true;panel.update(state,10,m,motion);state.green=true;panel.update(state,20,m,motion);
  const auto entered=panel.tick(2020,m,motion);state.green=false;panel.update(state,2030,m,motion);
  check(entered==PanelAction::Entered&&m.menu==Back,"physical guarded entry opens BACK without changing gesture");
  click();panel.tick(2040,m,motion);state.green=true;panel.update(state,2050,m,motion);
  check(panel.tick(4050,m,motion)==PanelAction::Blocked,"root BACK still requires RED OFF ON before reopening");
  out.printf("SETTINGS RESULT: %u passed, %u failed\n",passed,failed);return failed==0;
}
