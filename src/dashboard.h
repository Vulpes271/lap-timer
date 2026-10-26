#pragma once

#include <TFT_eSPI.h>
#include "track_catalog.h"
#include "control_model.h"
#include "imu_estimate.h"

struct DashboardData {
  bool speedValid=false,satellitesValid=false;
  uint32_t satellites=0;double speed=0;
  uint32_t lapTimeMs = 0;
  uint32_t lapNumber = 0;
  bool bestDeltaValid = false;
  double deltaToBestMs = 0;  // Cumulative difference at the latest sector boundary.
  bool frontPressureValid = false, rearPressureValid = false, leanValid = false;
  float frontPressureBar = 0, rearPressureBar = 0, leanDegrees = 0;
  bool batteryValid = false, sdUsageValid = false;
  uint8_t batteryPercent = 0, sdUsedPercent = 0;
  float leanZeroDegrees = 0;
  bool testSource=false, pressureAlert=false;
  unsigned int pressureUnits=0;
  double deltaStepMs=200;
  bool cameraLayout=false,rawLean=false;
  unsigned speedUnits=0;
};

class Dashboard {
 public:
  void begin() {
    tft.init();
    tft.setRotation(1);  // Landscape: 480 x 320.
    textCanvas.setColorDepth(1);
    bufferedText = textCanvas.createSprite(480, 144) != nullptr;
    Serial.printf("TFT: pomnilniski izris=%s, medpomnilnik 8640 B + maska 19200 B.\n",
                  bufferedText ? "OK" : "NI POMNILNIKA, neposredni izris");
    // Branding screen before GPS acquisition. No blocking color-test sequence.
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(3);
    tft.drawCentreString("RACE", 240, 112, 4);
    tft.setTextSize(1);
    tft.drawFastHLine(144, 208, 192, TFT_GREEN);
    Serial.println("TFT: zagon RACE.");
    delay(800);
    resetLapScreen();
    Serial.printf("TFT: demonstracijski zaslon casa kroga, %dx%d.\n", tft.width(), tft.height());
  }

  void reportRendering() {
    Serial.printf("TFT BUFFER: %s, RAM prost=%lu B, zadnji izris=%lu us, najdaljsi=%lu us, poslanih pik=%lu\n",
                  bufferedText ? "OK" : "IZKLOPLJEN", static_cast<unsigned long>(ESP.getFreeHeap()),
                  static_cast<unsigned long>(lastTextUs), static_cast<unsigned long>(maxTextUs),
                  static_cast<unsigned long>(textPixelsSent));
    if (!bufferedText) return;
    if(controlPage>=0 && minimalView(controlledView)){
      const uint16_t bottom=tft.readPixel(240,275),footer=tft.readPixel(240,300);
      Serial.printf("TFT QUICK CLEAN: %s expected=%04X bottom=%04X footer=%04X\n",
                    bottom==background&&footer==background?"PASS":"FAIL",background,bottom,footer);return;
    }
    if(controlPage>=0 && controlledView!=Control::View::Race && controlledView!=Control::View::Camera){
      Serial.println("TFT RAM: select screen race after overlays expire for this diagnostic");return;
    }
    // Sample actual panel RAM, verifying transfer, RGB565 byte order and background.
    for (int target = 0; target <= 1; ++target) {
      bool found = false;
      for (int y = 104; y < 248 && !found; ++y) {
        for (int x = 12; x < 468; ++x) {
          const bool bit = textMask[y][x / 8] & (0x80 >> (x % 8));
          if (bit != static_cast<bool>(target)) continue;
          const uint16_t actual = tft.readPixel(x, y);
          const uint16_t expected = target ? foreground : background;
          Serial.printf("TFT RAM: %s x=%d y=%d expected=%04X actual=%04X %s\n",
                        target ? "text" : "ozadje", x, y, expected, actual,
                        actual == expected ? "PASS" : "FAIL");
          found = true;
          break;
        }
      }
    }
  }

  void resetLapScreen() {
    controlPage=-1;background=normalBackground;region=0;lastLeanPixel=-1;comparisonActive=-1;
    memset(previous,0,sizeof(previous));memset(previousColors,0,sizeof(previousColors));memset(textMask,0,sizeof(textMask));
    tft.fillScreen(background);tft.setTextSize(1);tft.setTextPadding(0);drawLapLayout();
  }

  // One small region per call. No full-screen redraw in the acquisition loop.
  void updateControlled(DashboardData data, const Control::Model& model, const Imu::Estimate& imu) {
    using namespace Control;
    const auto view=model.view(millis());
    setTheme(model.config.theme);
    const bool minimal=minimalView(view);
    // Selection/edit changes update buffered rows; they must not clear the screen.
    const int key=int(view)+(model.testMode?10000:0)+(view==View::Settings?100+model.treeMenu+(model.editing?1000:0)+(model.numericActive?500:0):0)+
      (view==View::Settings&&model.specialPage?2000+model.specialPage*10:0)+model.config.theme*100000+model.config.pressure*1000000+model.config.speed*3000000+model.config.accel*6000000;
    if(controlPage!=key){
      if(view==View::Race||view==View::Camera){resetLapScreen();}
      else {
        background=view==View::Best?TFT_GREEN:view==View::Alarm?TFT_RED:normalBackground;
        tft.fillScreen(background);memset(textMask,0,sizeof(textMask));
        memset(previous,0,sizeof(previous));memset(previousColors,0,sizeof(previousColors));region=0;
        tft.setTextSize(1);tft.setTextPadding(0);tft.setTextColor(view==View::Best?TFT_BLACK:view==View::Alarm?TFT_WHITE:foreground,background);
        const char* name=view==View::Camera?"CAMERA":view==View::Settings?"SETUP":view==View::LastLap?"LAST LAP":
          view==View::Rear?"REAR PRESSURE":view==View::Front?"FRONT PRESSURE":view==View::Best?"BEST":view==View::Alarm?"PRESSURE ALARM":"LAST LAP";
        if(!minimal)tft.drawString(name,16,8,4);
        if(model.testMode)tft.drawRightString("TEST",464,8,4);
      }
      controlPage=key;
    }
    controlledView=view;
    if(view==View::Race||view==View::Camera){
      data.cameraLayout=view==View::Camera;data.speedUnits=model.config.speed;
      if(data.cameraLayout){
        double angle=0;bool raw=false;
        data.leanValid=imu.displayAngle(millis(),data.leanZeroDegrees,angle,raw);
        data.leanDegrees=angle;data.rawLean=data.leanValid&&raw;
      }
      update(data);return;
    }
    if(millis()-lastDraw<25)return;
    lastDraw=millis();
    char text[96]="";int x=16,y=64,width=448,height=26,font=4,size=1;uint8_t datum=TL_DATUM;bool degree=false;
    uint16_t color=view==View::Best?TFT_BLACK:view==View::Alarm?TFT_WHITE:foreground;
    const auto& config=model.config;const auto& source=model.source();const auto& session=source.session;
    if(view==View::Settings && model.specialPage==ImuWizard){
      const auto& wizard=imu.guided;
      y=42+region*42;height=36;font=2;
      if(region==0){std::snprintf(text,sizeof(text),"%s",wizard.title());font=4;}
      else if(region==1){std::snprintf(text,sizeof(text),"%s",wizard.instruction());color=wizard.stage==Imu::GuidedCalibration::Done||wizard.stage==Imu::GuidedCalibration::UprightOk||wizard.stage==Imu::GuidedCalibration::LeftOk||wizard.stage==Imu::GuidedCalibration::RightOk?TFT_GREEN:foreground;}
      else if(region==2){if(wizard.measuring())std::snprintf(text,sizeof(text),"MEASURING  %u %%",wizard.window.percent());font=4;}
      else if(region==3){std::snprintf(text,sizeof(text),"%s",wizard.detail);color=warning;}
      else if(region==4){y=210;height=30;font=4;std::snprintf(text,sizeof(text),"%s BACK",model.specialCursor==0?">":" ");color=model.specialCursor==0?TFT_RED:foreground;}
      else if(region==5){y=247;height=30;font=4;std::snprintf(text,sizeof(text),"%s %s",model.specialCursor==1?">":" ",wizard.measuring()?"MEASURING...":wizard.stage==Imu::GuidedCalibration::Done||wizard.stage==Imu::GuidedCalibration::Failed?"FINISH":"MEASURE");color=model.specialCursor==1?TFT_RED:foreground;}
      else{std::snprintf(text,sizeof(text),"LEFT / RIGHT + GREEN: MOVE | CENTER: SELECT");y=286;height=18;color=muted;}
    }else if(view==View::Settings && model.specialPage==SdFiles){
      const unsigned first=model.fileCursor<5?0:model.fileCursor-4;
      if(region==0){std::snprintf(text,sizeof(text),model.filesLoading?"READING SD...":"SD FILES: %u",model.filePage.total);y=40;height=22;font=2;}
      else if(region<=5){
        const unsigned row=first+region-1;char label[128]="";
        if((!model.filesLoading||row==0)&&row<model.fileRowCount()){model.fileRowLabel(row,label,sizeof(label));std::snprintf(text,sizeof(text),"%s %.82s",row==model.fileCursor?">":" ",label);}
        y=72+(region-1)*34;height=30;font=2;color=row==model.fileCursor?TFT_RED:foreground;
      }else if(region==6){
        y=246;height=28;font=2;color=muted;
        if(!model.filesLoading){if(model.fileCursor>0&&model.fileCursor<=model.filePage.count)std::snprintf(text,sizeof(text),"%lu BYTES",(unsigned long)model.filePage.entries[model.fileCursor-1].bytes);
        else std::snprintf(text,sizeof(text),"%s",model.filePage.message);}
      }else{y=286;height=18;font=2;color=muted;std::snprintf(text,sizeof(text),"LEFT / RIGHT + GREEN: MOVE | CENTER: SELECT");}
    }else if(view==View::Settings && model.specialPage==SdFormat){
      y=48+region*42;height=34;font=4;
      if(region==0)std::snprintf(text,sizeof(text),"FORMAT SD");
      else if(region==1){std::snprintf(text,sizeof(text),"%s",model.sdBusy?"FORMATTING...":model.sdFormatOk?"FORMAT OK":"FORMAT FAILED");color=model.sdFormatOk?TFT_GREEN:TFT_RED;}
      else if(region==2){font=2;std::snprintf(text,sizeof(text),"%s",model.sdBusy?"DO NOT REMOVE CARD OR POWER":"");}
      else if(region==3){font=2;std::snprintf(text,sizeof(text),"%s",model.error);color=TFT_RED;}
      else if(region==5){font=2;y=286;height=18;std::snprintf(text,sizeof(text),"%s",model.sdBusy?"PLEASE WAIT":"CENTER + GREEN: BACK");}
    }else if(view==View::Settings && model.treeMenu && model.editing){
      const unsigned options=model.choiceCount();
      const uint16_t savedColor=config.theme==1?TFT_DARKGREEN:TFT_GREEN;
      if(region==0){
        std::snprintf(text,sizeof(text),"%s",menuName(model.menu));y=40;height=30;font=4;
      }else if(options){
        const unsigned first=model.editRow<5?0:model.editRow-4;
        if(region<=5){
          const unsigned row=first+region-1;
          if(row<=options){char label[80];model.choiceLabel(row,label,sizeof(label));std::snprintf(text,sizeof(text),"%s %s",row==model.editRow?">":" ",label);}
          y=82+(region-1)*34;height=30;font=4;
          color=row==model.editRow?TFT_RED:row>0&&row-1==model.choiceIndex(config)?savedColor:foreground;
        }else if(region==6){
          y=255;height=22;font=2;color=model.error[0]?TFT_RED:muted;
          if(model.error[0])std::snprintf(text,sizeof(text),"%s",model.error);
          else if(model.menu==SdFormat){std::snprintf(text,sizeof(text),"ERASE ALL FILES DELETES THE ENTIRE CARD");color=TFT_RED;}
          else {char label[64];model.choiceLabel(model.choiceIndex(config)+1,label,sizeof(label));std::snprintf(text,sizeof(text),"SAVED: %s",label);}
        }else{y=288;height=18;font=2;color=muted;std::snprintf(text,sizeof(text),"CENTER + GREEN: SELECT | BACK: CANCEL");}
      }else if(model.numericEditor()&&!model.numericActive){
        if(region==1||region==2){const unsigned row=region-1;y=90+row*48;height=36;font=4;std::snprintf(text,sizeof(text),"%s %s",model.editRow==row?">":" ",row?"EDIT VALUE":"BACK");color=model.editRow==row?TFT_RED:foreground;}
        else if(region==3){model.numericValue(text,sizeof(text));y=202;height=30;font=4;color=savedColor;}
        else if(region==5){y=286;height=18;font=2;color=muted;std::snprintf(text,sizeof(text),"CENTER + GREEN: SELECT | RED OFF: CANCEL");}
        else{y=246;height=26;font=2;std::snprintf(text,sizeof(text),"%s",model.error);color=TFT_RED;}
      }else{
        if(region==1){
          std::snprintf(text,sizeof(text),"%s",model.menu==FrontRange||model.menu==RearRange?(model.rangePart?"MAXIMUM":"MINIMUM"):"VALUE");
          y=82;height=30;font=4;color=muted;
        }else if(region==2){
          model.numericValue(text,sizeof(text));x=16;y=124;width=448;height=78;font=4;size=3;datum=TC_DATUM;color=TFT_RED;
        }else if(region==3){std::snprintf(text,sizeof(text),"LEFT -       RIGHT +");y=214;height=30;font=4;datum=TC_DATUM;}
        else if(region==4){std::snprintf(text,sizeof(text),"%s",model.error);y=250;height=26;font=2;color=TFT_RED;}
        else{std::snprintf(text,sizeof(text),"CENTER + GREEN: SAVE | RED OFF: CANCEL");y=286;height=18;font=2;color=muted;}
      }
      for(char* c=text;*c;++c)if(*c>='a'&&*c<='z')*c-=('a'-'A');
    }else if(view==View::Settings && model.treeMenu){
      const unsigned count=model.treeCount();
      const unsigned first=model.menuRow<5?0:model.menuRow-4;
      if(region==0){
        std::snprintf(text,sizeof(text),"%s  %u/%u",model.menuDepth?categoryName(model.menuCategory):"MAIN MENU",model.menuRow+1,count);
        y=40;height=18;font=2;color=muted;
      }else if(region<=5){
        const unsigned row=first+region-1;const bool selected=row==model.menuRow;
        if(row<count)std::snprintf(text,sizeof(text),"%s %s",selected?">":" ",model.treeLabel(row));
        y=66+(region-1)*34;height=30;font=4;color=selected?(model.menuConfirmed?(config.theme==1?TFT_DARKGREEN:TFT_GREEN):TFT_RED):foreground;
      }else if(region==6){
        y=242;height=34;font=2;
        if(model.error[0]){std::snprintf(text,sizeof(text),"%s",model.error);color=TFT_RED;}
        else if(model.menuDepth && model.menuCategory==unsigned(Category::Calibration)&&imu.message[0]&&!model.editing){
          std::snprintf(text,sizeof(text),"%s",imu.message);color=warning;
        }else if(model.menuDepth && model.menu!=Back){
          char value[80];model.menuValue(model.menu,model.editing?model.draft:config,value,sizeof(value));
          std::snprintf(text,sizeof(text),"%s: %s",model.editing?((model.menu==FrontRange||model.menu==RearRange)?(model.rangePart?"EDIT MAX":"EDIT MIN"):"EDIT"):model.menuConfirmed?"CONFIRMED":"VALUE",value);
          color=model.menuConfirmed?(config.theme==1?TFT_DARKGREEN:TFT_GREEN):model.editing?TFT_RED:muted;
        }else std::snprintf(text,sizeof(text),"LEFT / RIGHT + GREEN = UP / DOWN");
      }else{
        y=285;height=18;font=2;color=muted;
        std::snprintf(text,sizeof(text),"CENTER + GREEN: OPEN | RED OFF: EXIT");
      }
      for(char* c=text;*c;++c)if(*c>='a'&&*c<='z')*c-=('a'-'A');
    }else if(view==View::Alarm){
      const unsigned wheel=region%2;
      const auto& p=source.pressure[wheel];const bool active=source.alarms[wheel][0].active||source.alarms[wheel][1].active;
      if(region<2){
        if(active)std::snprintf(text,sizeof(text),"%c %.2f %s %s",wheel?'R':'F',pressureValue(p.bar,config.pressure),pressureUnit(config.pressure),source.alarms[wheel][0].active?"LOW":"HIGH");
        else std::snprintf(text,sizeof(text),"%c --",wheel?'R':'F');
        y=82+region*86;size=2;height=52;
      }else{std::snprintf(text,sizeof(text),"GREEN = ACKNOWLEDGE / RACE");font=2;height=16;y=284;}
    }else{
      // Minimal review pages: one heading (including units), one large value.
      if(region==0){
        if(view==View::LastLap||view==View::Hold){if(session.laps)std::snprintf(text,sizeof(text),"LAP %u",session.laps);else std::snprintf(text,sizeof(text),"LAP --");}
        else if(view==View::Rear||view==View::Front)std::snprintf(text,sizeof(text),"%s PRESSURE [%s]",view==View::Rear?"REAR":"FRONT",pressureUnit(config.pressure));
        else if(view==View::BestLap||view==View::Best)std::snprintf(text,sizeof(text),"%s",view==View::Best?"BEST":"BEST LAP");
        else if(view==View::MaxAngle)std::snprintf(text,sizeof(text),"MAX ANGLE");
        else if(view==View::MaxSpeed)std::snprintf(text,sizeof(text),"MAX SPEED [%s]",config.speed?"mph":"km/h");
        else std::snprintf(text,sizeof(text),"MAX %s [%s]",view==View::MaxAcceleration?"ACCELERATION":"DECELERATION",config.accel?"m/s2":"g");
        y=16;width=model.testMode?368:448;
      }else{
        bool have=true;double value=0;
        const auto& peaks=source.maxima;
        if(view==View::Rear||view==View::Front){const auto& p=source.pressure[view==View::Front?0:1];have=p.fresh(model.time(millis()),config.staleMs);if(have)std::snprintf(text,sizeof(text),"%.2f",pressureValue(p.bar,config.pressure));}
        else if(view==View::MaxAngle){have=peaks.angleValid;degree=true;if(have)std::snprintf(text,sizeof(text),"%.1f",peaks.angle);}
        else if(view==View::MaxSpeed){have=peaks.speedValid;if(have)std::snprintf(text,sizeof(text),"%.1f",speedValue(peaks.speedKmh,config.speed));}
        else if(view==View::MaxAcceleration||view==View::MaxDeceleration){have=peaks.accelerationValid;if(have)std::snprintf(text,sizeof(text),"%.2f",accelValue(view==View::MaxAcceleration?peaks.accelerationG:peaks.decelerationG,config.accel));}
        else{value=view==View::BestLap?session.best.lapMs:session.lastMs;have=value>0;if(have)formatLap(text,sizeof(text),value);}
        if(!have)std::snprintf(text,sizeof(text),"--");
        x=12;width=456;y=104;font=7;size=3;height=144;datum=TC_DATUM;
      }
    }
    if(std::strcmp(previous[region],text)||previousColors[region]!=color){
      drawBufferedText(text,x,y,width,height,font,size,color,previousColors[region],datum,degree);
      std::snprintf(previous[region],sizeof(previous[region]),"%s",text);previousColors[region]=color;
    }
    const unsigned count=minimal?2:view==View::Settings?(model.specialPage?(model.specialPage==SdFiles?8:model.specialPage==ImuWizard?7:6):model.treeMenu&&(!model.editing||model.choiceCount())?8:6):3;
    region=(region+1)%count;
  }

  void update(const DashboardData& data) {
    if(millis()-lastDraw<25)return;
    lastDraw=millis();updateLap(data);
  }

 private:
  static bool minimalView(Control::View view){return Control::isQuickView(view)||view==Control::View::Hold||view==Control::View::Best;}
  void setTheme(unsigned theme){
    normalBackground=theme==1?TFT_WHITE:theme==2?TFT_NAVY:TFT_BLACK;
    foreground=theme==1?TFT_BLACK:TFT_WHITE;
    muted=theme==1?TFT_DARKGREY:TFT_LIGHTGREY;
    accent=theme==1?TFT_BLUE:TFT_CYAN;
    warning=theme==1?TFT_MAROON:TFT_YELLOW;
  }
  static void formatLap(char* text,size_t size,double ms){const unsigned tenths=unsigned(ms/100);std::snprintf(text,size,"%u:%02u.%u",tenths/600,(tenths/10)%60,tenths%10);}
  // Render text and its degree sign in RAM first. Transfer only changed row spans,
  // with their final foreground/background pixels, never an erase-then-draw pass.
  void drawBufferedText(const char* text, int x, int y, int width, int height,
                        int font, int size, uint16_t color, uint16_t oldColor,
                        uint8_t datum = TL_DATUM, bool degree = false) {
    if (!bufferedText) {
      tft.setTextSize(size); tft.setTextPadding(width); tft.setTextColor(color, background);
      if (datum == TC_DATUM) tft.drawCentreString(text, x + width / 2, y, font);
      else if (datum == TR_DATUM) tft.drawRightString(text, x + width, y, font);
      else tft.drawString(text, x, y, font);
      if (degree) {
        const int end = datum == TC_DATUM ? x + width / 2 + tft.textWidth(text, font) / 2 :
                        datum == TR_DATUM ? x + width : x + tft.textWidth(text, font);
        tft.drawCircle(end + 5, y + 4, 2, color);
      }
      tft.setTextSize(1);
      return;
    }
    const uint32_t started = micros();
    textCanvas.resetViewport();
    textCanvas.fillSprite(TFT_BLACK);
    textCanvas.setViewport(0, 0, width, height);
    textCanvas.setTextSize(size);
    while(size>1 && textCanvas.textWidth(text,font)+(degree?10:0)>width)textCanvas.setTextSize(--size);
    textCanvas.setTextPadding(0);
    textCanvas.setTextColor(TFT_WHITE, TFT_BLACK);
    textCanvas.setTextDatum(datum);
    const int anchor = datum == TC_DATUM ? width / 2 : datum == TR_DATUM ? width : 0;
    textCanvas.drawString(text, anchor, 0, font);
    if (degree) {
      const int end = datum == TC_DATUM ? width / 2 + textCanvas.textWidth(text, font) / 2 :
                      datum == TR_DATUM ? width : textCanvas.textWidth(text, font);
      textCanvas.drawCircle(end + 5, 4, 2, TFT_WHITE);
    }
    const auto* pixels = static_cast<const uint8_t*>(textCanvas.getPointer());
    const bool savedSwap = tft.getSwapBytes();
    tft.setSwapBytes(true);  // textRow contains native RGB565 words.
    tft.startWrite();
    for (int row = 0; row < height; ++row) {
      int first = width, last = -1;
      for (int col = 0; col < width; ++col) {
        const bool bit = pixels[row * 60 + col / 8] & (0x80 >> (col % 8));
        const int screenX = x + col;
        uint8_t& cached = textMask[y + row][screenX / 8];
        const uint8_t mask = 0x80 >> (screenX % 8);
        const bool oldBit = cached & mask;
        if (bit != oldBit || (bit && color != oldColor)) {
          if (first == width) first = col;
          last = col;
          if (bit) cached |= mask;
          else cached &= ~mask;
        }
        textRow[col] = bit ? color : background;
      }
      if (last >= first) {
        tft.pushImage(x + first, y + row, last - first + 1, 1, textRow + first);
        textPixelsSent += last - first + 1;
      }
    }
    tft.endWrite();
    tft.setSwapBytes(savedSwap);
    lastTextUs = micros() - started;
    if (lastTextUs > maxTextUs) maxTextUs = lastTextUs;
  }

  void drawLapLayout() {
    tft.drawFastHLine(16, 82, 448, TFT_DARKGREY);
    drawComparisonBars(0, TFT_DARKGREY);
  }

  void fillLeanStrip(int logicalStart, int width, uint16_t color) {
    // Logical centre is 240; reserve x=165..314 for the numeric angle.
    // The left bar ends at 165 and the right bar starts at 315.
    const int x = (logicalStart < 240 ? 165 : 315) + logicalStart - 240;
    if (width > 0) tft.fillRect(x, 284, width, 18, color);
  }

  void updateLeanBar(int pixel, uint16_t color) {
    const int oldEnd = lastLeanPixel < 0 ? 240 : lastLeanPixel;
    const int newEnd = pixel < 0 ? 240 : pixel;
    if ((oldEnd - 240) * (newEnd - 240) >= 0) {
      // Same side: only paint the strip between the previous and new endpoint.
      const int width = abs(newEnd - oldEnd);
      if (width > 0) {
        const uint16_t fill = abs(newEnd - 240) > abs(oldEnd - 240) ? color : background;
        fillLeanStrip(min(oldEnd, newEnd), width, fill);
      }
    } else {
      // Crossing zero: erase the old side, then fill the new side.
      fillLeanStrip(min(oldEnd, 240), abs(oldEnd - 240), background);
      fillLeanStrip(min(newEnd, 240), abs(newEnd - 240), color);
    }
    lastLeanPixel = pixel;
  }

  void drawComparisonBars(unsigned int active, uint16_t color) {
    // Maximum width outside the large timer: x=12..467 is reserved for text.
    for (int segment = 0; segment < 3; ++segment) {
      const int y = 108 + (2 - segment) * 46;
      const bool on = static_cast<unsigned int>(segment) < active;
      const bool wasOn = comparisonActive >= 0 && segment < comparisonActive;
      if (comparisonActive >= 0 && on == wasOn && (!on || color == comparisonColor)) continue;
      for (int x : {2, 468}) {
        tft.fillRoundRect(x, y, 10, 40, 3, on ? color : background);
        tft.drawRoundRect(x, y, 10, 40, 3, on ? color : TFT_DARKGREY);
      }
    }
    comparisonActive = active;
    comparisonColor = color;
  }

  void updateLap(const DashboardData& data) {
    char text[80];
    uint16_t color = foreground;
    if (region < 2) {
      const bool valid = region == 0 ? data.rearPressureValid : data.frontPressureValid;
      const float pressure = region == 0 ? data.rearPressureBar : data.frontPressureBar;
      const char wheel = region == 0 ? 'R' : 'F';
      if (valid) snprintf(text, sizeof(text), "%c %.2f %s", wheel, Control::pressureValue(pressure,data.pressureUnits),Control::pressureUnit(data.pressureUnits));
      else snprintf(text, sizeof(text), "%c --.-- %s", wheel,Control::pressureUnit(data.pressureUnits));
      color = valid ? foreground : muted;
      if (strcmp(previous[region], text) != 0 || previousColors[region] != color) {
        drawBufferedText(text, region == 0 ? 16 : 248, 40, 216, 26, 4, 1,
                         color, previousColors[region], TC_DATUM);
      }
    } else if (region == 2) {
      const uint32_t tenths = data.lapTimeMs / 100;
      const uint32_t minutes = tenths / 600;
      snprintf(text, sizeof(text), "%lu:%02lu.%lu",
               static_cast<unsigned long>(minutes),
               static_cast<unsigned long>((tenths / 10) % 60),
               static_cast<unsigned long>(tenths % 10));
      if (strcmp(previous[region], text) != 0) {
        drawBufferedText(text, 12, 104, 456, data.cameraLayout?96:144, 7, data.cameraLayout?2:minutes < 10 ? 3 : 2,
                         foreground, previousColors[region], TC_DATUM);
      }
    } else if (region == 3) {
      const float angle = data.leanDegrees;
      const int pixel = data.leanValid ? 240 + static_cast<int>(constrain(angle, -60.0f, 60.0f) * (125.0f / 60.0f)) : -1;
      if (data.leanValid) snprintf(text, sizeof(text), "%.1f", fabsf(angle));
      else snprintf(text, sizeof(text), "--");
      color = data.leanValid ? accent : TFT_DARKGREY;
      if (strcmp(previous[region], text) != 0 || previousColors[region] != color) {
        drawBufferedText(text, 170, 279, 140, 26, 4, 1, color,
                         previousColors[region], TC_DATUM, true);
      }
      if (pixel != lastLeanPixel || color != previousColors[region]) {
        updateLeanBar(pixel, color);
      }
    } else if (region == 4) {
      char satellites[8], battery[8], sd[8];
      if (data.satellitesValid)
        snprintf(satellites, sizeof(satellites), "%lu", static_cast<unsigned long>(data.satellites));
      else snprintf(satellites, sizeof(satellites), "--");
      if (data.batteryValid) snprintf(battery, sizeof(battery), "%u%%", data.batteryPercent);
      else snprintf(battery, sizeof(battery), "--");
      if (data.sdUsageValid) snprintf(sd, sizeof(sd), "%u%%", data.sdUsedPercent);
      else snprintf(sd, sizeof(sd), "--");
      snprintf(text, sizeof(text), "%sGPS %.7s BAT %.4s SD %.4s%s%s",data.testSource?"TEST ":"", satellites, battery, sd,data.pressureAlert?" !":"",data.rawLean?" RAW":"");
      color = muted;
      if (strcmp(previous[region], text) != 0 || previousColors[region] != color) {
        drawBufferedText(text, 16, 8, 448, 16, 2, 1, color, previousColors[region], TC_DATUM);
      }
    } else if (region == 5) {
      snprintf(text, sizeof(text), "LAP %lu", static_cast<unsigned long>(data.lapNumber));
      if (strcmp(previous[region], text) != 0) {
        drawBufferedText(text, 24, 87, 180, 16, 2, 1, muted, previousColors[region]);
      }
    } else if(region==7 && data.cameraLayout){
      if(data.speedValid)snprintf(text,sizeof(text),"%.0f %s",Control::speedValue(data.speed,data.speedUnits),data.speedUnits?"MPH":"KM/H");
      else snprintf(text,sizeof(text),"-- %s",data.speedUnits?"MPH":"KM/H");
      if(strcmp(previous[region],text)!=0||previousColors[region]!=color){
        drawBufferedText(text,16,211,448,52,4,2,color,previousColors[region],TC_DATUM);
      }
    } else {
      const unsigned active=Control::deltaLights(data.deltaToBestMs,data.deltaStepMs,data.bestDeltaValid);
      if (data.bestDeltaValid) {
        snprintf(text, sizeof(text), "BEST %+.1f s", data.deltaToBestMs / 1000.0);
        color = active ? (data.deltaToBestMs < 0 ? TFT_GREEN : TFT_RED) : muted;
      } else {
        snprintf(text, sizeof(text), "BEST --");
        color = TFT_DARKGREY;
      }
      // Include segment count in the cache key: bar transitions need not change rounded text.
      char key[40];
      snprintf(key, sizeof(key), "%.30s:%u", text, active);
      if (strcmp(previous[region], key) != 0 || previousColors[region] != color) {
        drawBufferedText(text, 266, 87, 190, 16, 2, 1, color, previousColors[region], TR_DATUM);
        drawComparisonBars(active, color);
      }
      snprintf(text, sizeof(text), "%s", key);
    }
    snprintf(previous[region], sizeof(previous[region]), "%s", text);
    previousColors[region] = color;
    region = (region + 1) % (data.cameraLayout?8:7);
  }

  uint16_t background = TFT_BLACK, normalBackground=TFT_BLACK;
  uint16_t foreground=TFT_WHITE,muted=TFT_LIGHTGREY,accent=TFT_CYAN;
  uint16_t warning=TFT_YELLOW;
  Control::View controlledView=Control::View::Race;
  int controlPage=-1;
  TFT_eSPI tft;
  TFT_eSprite textCanvas = TFT_eSprite(&tft);
  bool bufferedText = false;
  uint8_t textMask[320][60] = {};
  uint16_t textRow[480] = {};
  uint32_t textPixelsSent = 0, lastTextUs = 0, maxTextUs = 0;
  int comparisonActive = -1;
  uint16_t comparisonColor = TFT_DARKGREY;
  uint32_t lastDraw = 0;
  uint8_t region = 0;
  int lastLeanPixel = -1;
  char previous[10][96] = {};
  uint16_t previousColors[10] = {};
};
