#pragma once

#include <TFT_eSPI.h>
#include "stationary.h"
#include "track_catalog.h"
#include "control_model.h"
#include "imu_estimate.h"

struct DashboardData {
  bool gpsOnline = false, fix = false, speedValid = false;
  bool satellitesValid = false, imuOk = false, recording = false;
  uint32_t satellites = 0, rows = 0, errors = 0, missed = 0;
  uint32_t epochMs = 0, checksumErrors = 0, maxLogUs = 0;
  double speed = 0, lat = 0, lon = 0;
  float ax = 0, ay = 0, az = 0;
  const char* path = "";
  bool stationary = false, hdopValid = false;
  float gx = 0, gy = 0, gz = 0;
  double hdop = 0;
  const StationaryStats* test = nullptr;
  uint32_t lapTimeMs = 0;
  uint32_t lapNumber = 0;
  bool bestDeltaValid = false;
  int32_t deltaToBestMs = 0;  // Cumulative difference at the latest sector boundary.
  bool frontPressureValid = false, rearPressureValid = false, leanValid = false;
  float frontPressureBar = 0, rearPressureBar = 0, leanDegrees = 0;
  bool batteryValid = false, sdUsageValid = false;
  uint8_t batteryPercent = 0, sdUsedPercent = 0;
  bool greenPreview = false;
  float leanZeroDegrees = 0;
  const char* trackName = "";
  double lastLapMs = 0, bestLapMs = 0;
  unsigned int trackIndex = 0, currentSector = 0;
  double sectorMs[3] = {};
  bool testSource=false, pressureAlert=false;
  unsigned int pressureUnits=0;
  double deltaStepMs=200;
  bool cameraLayout=false,rawLean=false;
  unsigned speedUnits=0;
};

enum class DashboardScreen : uint8_t { LapTimer, Diagnostics, Stationary, Settings, Tracks };

class Dashboard {
 public:
  void begin(bool stationaryMode = false) {
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
    selectScreen(DashboardScreen::LapTimer);
    Serial.printf("TFT: demonstracijski zaslon casa kroga, %dx%d.\n", tft.width(), tft.height());
  }

  // Future button handler can call nextScreen(); pins are not reserved yet.
  void nextScreen() {
    if (screen == DashboardScreen::Settings || screen == DashboardScreen::Tracks) {
      selectScreen(DashboardScreen::LapTimer);
      return;
    }
    selectScreen(static_cast<DashboardScreen>((static_cast<uint8_t>(screen) + 1) % 4));
  }

  void closeSettings() { selectScreen(DashboardScreen::LapTimer); }
  void openTracks() { selectScreen(DashboardScreen::Tracks); }
  bool tracksOpen() const { return screen == DashboardScreen::Tracks; }

  void reportRendering() {
    Serial.printf("TFT BUFFER: %s, RAM prost=%lu B, zadnji izris=%lu us, najdaljsi=%lu us, poslanih pik=%lu\n",
                  bufferedText ? "OK" : "IZKLOPLJEN", static_cast<unsigned long>(ESP.getFreeHeap()),
                  static_cast<unsigned long>(lastTextUs), static_cast<unsigned long>(maxTextUs),
                  static_cast<unsigned long>(textPixelsSent));
    if (screen != DashboardScreen::LapTimer || !bufferedText) return;
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

  void toggleSettings() {
    if (screen == DashboardScreen::Tracks) selectScreen(DashboardScreen::Settings);
    else if (screen == DashboardScreen::Settings) closeSettings();
    else selectScreen(DashboardScreen::Settings);
  }

  void selectScreen(DashboardScreen selected) {
    controlPage=-1;
    background=normalBackground;
    screen = selected;
    const char* name = screen == DashboardScreen::LapTimer ? "GLAVNI" :
                       screen == DashboardScreen::Settings ? "SETUP" : screen == DashboardScreen::Tracks ? "DIRKALISCA" :
                       screen == DashboardScreen::Stationary ? "MIRUJOCI TEST" : "DIAGNOSTIKA";
    Serial.printf("ZASLON: %s\n", name);
    region = 0;
    memset(previous, 0, sizeof(previous));
    memset(previousColors, 0, sizeof(previousColors));
    lastLeanPixel = -1;
    memset(textMask, 0, sizeof(textMask));
    comparisonActive = -1;
    stationaryFooterDrawn = false;
    tft.fillScreen(background);
    tft.setTextSize(1);
    tft.setTextPadding(0);
    if (screen == DashboardScreen::LapTimer) {
      drawLapLayout();
      return;
    }
    if (screen == DashboardScreen::Tracks) {
      tft.setTextColor(foreground, background);
      tft.drawString("SETUP > DIRKALISCA", 24, 16, 4);
      tft.drawFastHLine(24, 52, 432, TFT_DARKGREY);
      for (unsigned int i = 0; i < Tracks::Count; ++i) {
        char label[64];
        snprintf(label, sizeof(label), "%u  %s", i + 1, Tracks::Catalog[i]->name);
        tft.drawString(label, 24, 70 + i * 28, 4);
      }
      tft.setTextColor(muted, background);
      tft.drawString("Stevilka = izberi | S = nazaj v SETUP", 24, 268, 2);
      tft.drawString("Q / ESC = glavni zaslon", 24, 292, 2);
      return;
    }
    if (screen == DashboardScreen::Settings) {
      tft.setTextColor(foreground, background);
      tft.drawString("SETUP", 24, 16, 4);
      tft.drawFastHLine(24, 52, 432, TFT_DARKGREY);
      tft.drawString("K  Dirkalisca:", 24, 64, 2);
      tft.drawString("Z  Nastavi niclo | C  Ponastavi niclo", 24, 88, 2);
      tft.drawString("G  Zeleni preizkus", 24, 208, 2);
      tft.setTextColor(muted, background);
      tft.drawString("Ukazi prek USB; fizicni gumb sledi.", 24, 268, 2);
      tft.drawString("N / Q / ESC  Nazaj | S  Zapri SETUP", 24, 292, 2);
      return;
    }
    const bool stationaryMode = screen == DashboardScreen::Stationary;
    tft.setTextColor(foreground, background);
    tft.drawString(stationaryMode ? "MIRUJOCI TEST" : "LAP TIMER", 16, 8, 4);
    tft.setTextColor(accent, background);
    tft.drawRightString("GPS + IMU + SD", 464, 14, 2);
    tft.drawFastHLine(16, 38, 448, TFT_DARKGREY);
    tft.setTextColor(muted, background);
    if (!stationaryMode) tft.drawString("km/h", 360, 72, 4);
    tft.drawFastHLine(16, 282, 448, TFT_DARKGREY);
    tft.drawString("Diagnostika | GPS 5 Hz | cakanje na podatke", 16, 294, 2);
    Serial.printf("TFT: inicializacija poslana, driver=0x%04X, %dx%d. Priklop se ni preverjen.\n",
                  TFT_DRIVER, tft.width(), tft.height());
  }

  // One small region per call. No full-screen redraw in the acquisition loop.
  void updateControlled(DashboardData data, const Control::Model& model, const Imu::Estimate& imu) {
    using namespace Control;
    const auto view=model.view(millis());
    setTheme(model.config.theme);
    const bool minimal=minimalView(view);
    // Selection/edit changes update buffered rows; they must not clear the screen.
    const int key=int(view)+(model.testMode?10000:0)+(view==View::Settings?100+model.treeMenu+(model.editing?1000:0):0)+
      model.config.theme*100000+model.config.pressure*1000000+model.config.speed*3000000+model.config.accel*6000000;
    if(controlPage!=key){
      if(view==View::Race||view==View::Camera){selectScreen(DashboardScreen::LapTimer);}
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
    if(view==View::Settings && model.treeMenu && model.editing){
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
          color=row==model.editRow?TFT_RED:row<options&&row==model.choiceIndex(config)?savedColor:foreground;
        }else if(region==6){
          y=255;height=22;font=2;color=model.error[0]?TFT_RED:muted;
          if(model.error[0])std::snprintf(text,sizeof(text),"%s",model.error);
          else {char label[64];model.choiceLabel(model.choiceIndex(config),label,sizeof(label));std::snprintf(text,sizeof(text),"SAVED: %s",label);}
        }else{y=288;height=18;font=2;color=muted;std::snprintf(text,sizeof(text),"CENTER + GREEN: SELECT | BACK: CANCEL");}
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
    }else if(view==View::Settings){
      if(region==0){std::snprintf(text,sizeof(text),"%s %u/%u  %s",model.editing?"EDIT":"SELECT",model.menu+1,MenuCount,menuName(model.menu));font=2;height=16;y=60;}
      else if(region==1){model.menuValue(model.menu,model.editing?model.draft:config,text,sizeof(text));y=94;}
      else if(region==2){std::snprintf(text,sizeof(text),"Next: %s",menuName((model.menu+1)%MenuCount));font=2;height=16;y=142;}
      else if(region==3){std::snprintf(text,sizeof(text),"%s",model.error[0]?model.error:imu.message[0]?imu.message:"LEFT/RIGHT select; GREEN edit/confirm");font=2;height=16;y=174;color=warning;}
      else if(region==4){std::snprintf(text,sizeof(text),"SD %s | RECORD %s | UTC %+.2f h",data.sdUsageValid?"OK":"LOST",data.recording?"ON":"OFF",config.utcOffsetMinutes/60.0);font=2;height=16;y=214;}
      else {std::snprintf(text,sizeof(text),"GREEN hold 2s / RED OFF = EXIT");font=2;height=16;y=284;}
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
    const unsigned count=minimal?2:view==View::Settings?(model.treeMenu&&(!model.editing||model.choiceCount())?8:6):3;
    region=(region+1)%count;
  }

  void update(const DashboardData& data) {
    if (millis() - lastDraw < 25) return;
    lastDraw = millis();
    if (screen == DashboardScreen::Tracks) { updateTracks(data); return; }
    if (screen == DashboardScreen::LapTimer) { updateLap(data); return; }
    if (screen == DashboardScreen::Settings) { updateSettings(data); return; }
    if (screen == DashboardScreen::Stationary && data.test) { updateStationary(data); return; }
    char text[96];
    uint16_t color = foreground;
    int font = 2, y = 112 + (region - 1) * 27;
    switch (region) {
      case 0:
        if (data.speedValid) snprintf(text, sizeof(text), "%.1f", data.speed);
        else snprintf(text, sizeof(text), "--.-");
        font = 7; y = 52;
        color = data.speedValid ? foreground : TFT_DARKGREY;
        break;
      case 1:
        snprintf(text, sizeof(text), "GPS: %s | sat: %s%lu | korak: %lu ms",
                 !data.gpsOnline ? "NI PODATKOV" : data.fix ? "POLOZAJ OK" : "ISCEM POLOZAJ",
                 data.satellitesValid ? "" : "? ", static_cast<unsigned long>(data.satellites),
                 static_cast<unsigned long>(data.epochMs));
        color = data.fix ? TFT_GREEN : warning;
        break;
      case 2:
        if (data.fix) snprintf(text, sizeof(text), "LAT %.6f   LON %.6f", data.lat, data.lon);
        else snprintf(text, sizeof(text), "LAT --   LON --");
        break;
      case 3:
        if (data.imuOk) snprintf(text, sizeof(text), "MPU [g]  X %+.3f  Y %+.3f  Z %+.3f", data.ax, data.ay, data.az);
        else snprintf(text, sizeof(text), "MPU: ni svezih meritev");
        color = data.imuOk ? foreground : warning;
        break;
      case 4:
        snprintf(text, sizeof(text), "SD: %s | %s | vrstice: %lu",
                 data.recording ? "ZAPISUJEM" : "USTAVLJEN", data.path,
                 static_cast<unsigned long>(data.rows));
        color = data.recording ? TFT_GREEN : warning;
        break;
      case 5:
        snprintf(text, sizeof(text), "Napake SD: %lu  NMEA: %lu  Izpadi GPS: %lu",
                 static_cast<unsigned long>(data.errors), static_cast<unsigned long>(data.checksumErrors),
                 static_cast<unsigned long>(data.missed));
        color = data.errors || data.checksumErrors || data.missed ? TFT_ORANGE : muted;
        break;
      case 6:
        snprintf(text, sizeof(text), "SD najdlje: %.1f ms | delovanje: %lu s",
                 data.maxLogUs / 1000.0, static_cast<unsigned long>(millis() / 1000));
        color = muted;
        break;
      default:
        snprintf(text, sizeof(text), "Diagnostika | GPS 5 Hz | cas kroga: --");
        y = 294; color = accent;
        break;
    }
    // Opaque text padding removes previous, longer values without clearing the screen.
    if (strcmp(previous[region], text) != 0 || previousColors[region] != color) {
      drawBufferedText(text, 16, y, region == 0 ? 330 : 448, region == 0 ? 48 : 16,
                       font, 1, color, previousColors[region]);
      snprintf(previous[region], sizeof(previous[region]), "%s", text);
      previousColors[region] = color;
    }
    region = (region + 1) % 8;
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
      if (data.greenPreview) {
        snprintf(text, sizeof(text), "TEST");
        color = TFT_GREEN;
      } else if (data.bestDeltaValid) {
        snprintf(text, sizeof(text), "BEST %+.1f s", data.deltaToBestMs / 1000.0);
        color = abs(data.deltaToBestMs)<=10 ? muted : data.deltaToBestMs < 0 ? TFT_GREEN : TFT_RED;
      } else {
        snprintf(text, sizeof(text), "BEST --");
        color = TFT_DARKGREY;
      }
      const unsigned int active = data.greenPreview ? 3 : data.bestDeltaValid && abs(data.deltaToBestMs)>10 ?
          static_cast<unsigned int>(fmin(3.0, ceil(fabs(static_cast<double>(data.deltaToBestMs)) / data.deltaStepMs))) : 0;
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

  void updateSettings(const DashboardData& data) {
    char text[80];
    if (region == 0)
      snprintf(text, sizeof(text), "Nicla: %+.2f", data.leanZeroDegrees);
    else if (region == 1) {
      if (data.leanValid) snprintf(text, sizeof(text), "Nagib: %+.1f", data.leanDegrees);
      else snprintf(text, sizeof(text), "Trenutni nagib: --");
    } else if (region == 2) snprintf(text, sizeof(text), "Zeleni preizkus: %s", data.greenPreview ? "VKLOPLJEN" : "IZKLOPLJEN");
    else if (region == 3) snprintf(text, sizeof(text), "%s", data.trackName);
    else if (region == 4) snprintf(text, sizeof(text), "Zadnji %.3f s | Best %.3f s", data.lastLapMs / 1000, data.bestLapMs / 1000);
    else snprintf(text, sizeof(text), "S1 %.2f | S2 %.2f | S3 %.2f s", data.sectorMs[0] / 1000, data.sectorMs[1] / 1000, data.sectorMs[2] / 1000);
    if (strcmp(previous[region], text) != 0) {
      const int y = region < 2 ? 112 + region * 24 : region == 2 ? 232 : region == 3 ? 64 : region == 4 ? 160 : 184;
      const int x = region == 3 ? 126 : 24;
      drawBufferedText(text, x, y, region == 3 ? 300 : 432, 16, 2, 1,
                       accent, previousColors[region], TL_DATUM,
                       region == 0 || (region == 1 && data.leanValid));
      previousColors[region] = accent;
      snprintf(previous[region], sizeof(previous[region]), "%s", text);
    }
    region = (region + 1) % 6;
  }

  void updateTracks(const DashboardData& data) {
    char text[96];
    snprintf(text, sizeof(text), "Izbrano: %s | 3 sektorji", data.trackName);
    if (strcmp(previous[0], text) == 0) return;
    tft.setTextSize(1); tft.setTextPadding(432);
    tft.setTextColor(TFT_GREEN, background);
    tft.drawString(text, 24, 120, 2);
    tft.setTextColor(muted, background);
    tft.drawString("Sektorji: lastne tocke iz AiM trase", 24, 146, 2);
    snprintf(previous[0], sizeof(previous[0]), "%s", text);
  }

  void updateStationary(const DashboardData& data) {
    const StationaryStats& s = *data.test;
    char text[96];
    uint16_t color = foreground;
    switch (region) {
      case 0:
        snprintf(text, sizeof(text), "GPS %s | SAT %lu | HDOP %s%.1f",
                 data.fix ? "OK" : data.gpsOnline ? "ISCEM" : "NI PODATKOV",
                 static_cast<unsigned long>(data.satellites), data.hdopValid ? "" : "? ", data.hdop);
        color = data.fix ? TFT_GREEN : warning; break;
      case 1:
        if (data.speedValid)
          snprintf(text, sizeof(text), "v %.2f | povp %.2f | max %.2f km/h", data.speed, s.speed.mean, s.speed.maximum);
        else snprintf(text, sizeof(text), "Hitrost: -- | cakam veljavno GPS meritev");
        break;
      case 2:
        if (s.east.count > 1)
          snprintf(text, sizeof(text), "GPS odmik %.1f | max %.1f | sigma %.1f m", s.drift, s.maxDrift, s.spread());
        else snprintf(text, sizeof(text), "GPS odmik: -- | cakam vzorce");
        break;
      case 3:
        if (data.imuOk) snprintf(text, sizeof(text), "ACC g: %+.3f  %+.3f  %+.3f", data.ax, data.ay, data.az);
        else snprintf(text, sizeof(text), "MPU: ni svezih meritev");
        color = data.imuOk ? foreground : warning; break;
      case 4:
        if (s.accelNorm.count > 1)
          snprintf(text, sizeof(text), "|a| %.4f g | sigma %.4f | odmik %+.1f%%", s.accelNorm.mean, s.accelNorm.sigma(), (s.accelNorm.mean - 1) * 100);
        else snprintf(text, sizeof(text), "|a|: cakam statistiko (v mirovanju ~1 g)");
        break;
      case 5:
        if (data.imuOk) snprintf(text, sizeof(text), "GYR d/s: %+.2f  %+.2f  %+.2f", data.gx, data.gy, data.gz);
        else snprintf(text, sizeof(text), "GYR: --");
        break;
      case 6:
        if (s.gyro[0].count)
          snprintf(text, sizeof(text), "GYR povp: %+.3f  %+.3f  %+.3f d/s", s.gyro[0].mean, s.gyro[1].mean, s.gyro[2].mean);
        else snprintf(text, sizeof(text), "GYR povp: cakam statistiko");
        break;
      case 7:
        if (s.gyro[0].count > 1)
          snprintf(text, sizeof(text), "GYR sigma: %.3f  %.3f  %.3f d/s", s.gyro[0].sigma(), s.gyro[1].sigma(), s.gyro[2].sigma());
        else snprintf(text, sizeof(text), "GYR sigma: cakam statistiko");
        break;
      case 8:
        snprintf(text, sizeof(text), "%s | %lu s | IMU %lu | GPS %lu",
                 s.paused ? "USTAVLJEN" : s.warming() ? "UMIRJANJE 10 s" : "MERIM", static_cast<unsigned long>(s.seconds()),
                 static_cast<unsigned long>(s.accelNorm.count), static_cast<unsigned long>(s.east.count));
        color = s.warming() ? warning : accent; break;
      default:
        snprintf(text, sizeof(text), "Napake I2C %lu | NMEA %lu | izpadi GPS %lu",
                 static_cast<unsigned long>(s.imuErrors), static_cast<unsigned long>(data.checksumErrors),
                 static_cast<unsigned long>(data.missed));
        color = s.imuErrors || data.checksumErrors || data.missed ? TFT_ORANGE : muted;
        break;
    }
    if (strcmp(previous[region], text) != 0 || previousColors[region] != color) {
      drawBufferedText(text, 16, 44 + region * 24, 448, 16, 2, 1, color, previousColors[region]);
      snprintf(previous[region], sizeof(previous[region]), "%s", text);
      previousColors[region] = color;
    }
    // Footer also replaces the startup message after the first update.
    if (region == 0 && !stationaryFooterDrawn) {
      tft.setTextColor(accent, background); tft.setTextPadding(448);
      tft.drawString("SD IZKLOP | USB: r = nov test, p = ustavi", 16, 294, 2);
      stationaryFooterDrawn = true;
    }
    region = (region + 1) % 10;
  }
  uint16_t background = TFT_BLACK, normalBackground=TFT_BLACK;
  uint16_t foreground=TFT_WHITE,muted=TFT_LIGHTGREY,accent=TFT_CYAN;
  uint16_t warning=TFT_YELLOW;
  Control::View controlledView=Control::View::Race;
  int controlPage=-1;
  TFT_eSPI tft;
  TFT_eSprite textCanvas = TFT_eSprite(&tft);
  bool bufferedText = false;
  bool stationaryFooterDrawn = false;
  uint8_t textMask[320][60] = {};
  uint16_t textRow[480] = {};
  uint32_t textPixelsSent = 0, lastTextUs = 0, maxTextUs = 0;
  int comparisonActive = -1;
  uint16_t comparisonColor = TFT_DARKGREY;
  uint32_t lastDraw = 0;
  uint8_t region = 0;
  DashboardScreen screen = DashboardScreen::LapTimer;
  int lastLeanPixel = -1;
  char previous[10][96] = {};
  uint16_t previousColors[10] = {};
};
