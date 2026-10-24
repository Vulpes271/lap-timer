#pragma once

#include <TFT_eSPI.h>
#include "stationary.h"
#include "track_catalog.h"

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
};

enum class DashboardScreen : uint8_t { LapTimer, Diagnostics, Stationary, Settings, Tracks };

class Dashboard {
 public:
  void begin(bool stationaryMode = false) {
    tft.init();
    tft.setRotation(1);  // Landscape: 480 x 320.
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

  void toggleSettings() {
    if (screen == DashboardScreen::Tracks) selectScreen(DashboardScreen::Settings);
    else if (screen == DashboardScreen::Settings) closeSettings();
    else selectScreen(DashboardScreen::Settings);
  }

  void selectScreen(DashboardScreen selected) {
    screen = selected;
    const char* name = screen == DashboardScreen::LapTimer ? "GLAVNI" :
                       screen == DashboardScreen::Settings ? "SETUP" : screen == DashboardScreen::Tracks ? "DIRKALISCA" :
                       screen == DashboardScreen::Stationary ? "MIRUJOCI TEST" : "DIAGNOSTIKA";
    Serial.printf("ZASLON: %s\n", name);
    region = 0;
    memset(previous, 0, sizeof(previous));
    memset(previousColors, 0, sizeof(previousColors));
    lastLeanPixel = -1;
    tft.fillScreen(background);
    tft.setTextSize(1);
    tft.setTextPadding(0);
    if (screen == DashboardScreen::LapTimer) {
      drawLapLayout();
      return;
    }
    if (screen == DashboardScreen::Tracks) {
      tft.setTextColor(TFT_WHITE, background);
      tft.drawString("SETUP > DIRKALISCA", 24, 16, 4);
      tft.drawFastHLine(24, 52, 432, TFT_DARKGREY);
      for (unsigned int i = 0; i < Tracks::Count; ++i) {
        char label[64];
        snprintf(label, sizeof(label), "%u  %s", i + 1, Tracks::Catalog[i]->name);
        tft.drawString(label, 24, 70 + i * 28, 4);
      }
      tft.setTextColor(TFT_LIGHTGREY, background);
      tft.drawString("Stevilka = izberi | S = nazaj v SETUP", 24, 268, 2);
      tft.drawString("Q / ESC = glavni zaslon", 24, 292, 2);
      return;
    }
    if (screen == DashboardScreen::Settings) {
      tft.setTextColor(TFT_WHITE, background);
      tft.drawString("SETUP", 24, 16, 4);
      tft.drawFastHLine(24, 52, 432, TFT_DARKGREY);
      tft.drawString("K  Dirkalisca:", 24, 64, 2);
      tft.drawString("Z  Nastavi niclo | C  Ponastavi niclo", 24, 88, 2);
      tft.drawString("G  Zeleni preizkus", 24, 208, 2);
      tft.setTextColor(TFT_LIGHTGREY, background);
      tft.drawString("Ukazi prek USB; fizicni gumb sledi.", 24, 268, 2);
      tft.drawString("N / Q / ESC  Nazaj | S  Zapri SETUP", 24, 292, 2);
      return;
    }
    const bool stationaryMode = screen == DashboardScreen::Stationary;
    tft.setTextColor(TFT_WHITE, background);
    tft.drawString(stationaryMode ? "MIRUJOCI TEST" : "LAP TIMER", 16, 8, 4);
    tft.setTextColor(TFT_CYAN, background);
    tft.drawRightString("GPS + IMU + SD", 464, 14, 2);
    tft.drawFastHLine(16, 38, 448, TFT_DARKGREY);
    tft.setTextColor(TFT_LIGHTGREY, background);
    if (!stationaryMode) tft.drawString("km/h", 360, 72, 4);
    tft.drawFastHLine(16, 282, 448, TFT_DARKGREY);
    tft.drawString("Diagnostika | GPS 5 Hz | cakanje na podatke", 16, 294, 2);
    Serial.printf("TFT: inicializacija poslana, driver=0x%04X, %dx%d. Priklop se ni preverjen.\n",
                  TFT_DRIVER, tft.width(), tft.height());
  }

  // One small region per call. No full-screen redraw in the acquisition loop.
  void update(const DashboardData& data) {
    if (millis() - lastDraw < 25) return;
    lastDraw = millis();
    if (screen == DashboardScreen::Tracks) { updateTracks(data); return; }
    if (screen == DashboardScreen::LapTimer) { updateLap(data); return; }
    if (screen == DashboardScreen::Settings) { updateSettings(data); return; }
    if (screen == DashboardScreen::Stationary && data.test) { updateStationary(data); return; }
    char text[96];
    uint16_t color = TFT_WHITE;
    int font = 2, y = 112 + (region - 1) * 27;
    switch (region) {
      case 0:
        if (data.speedValid) snprintf(text, sizeof(text), "%.1f", data.speed);
        else snprintf(text, sizeof(text), "--.-");
        font = 7; y = 52;
        color = data.speedValid ? TFT_WHITE : TFT_DARKGREY;
        break;
      case 1:
        snprintf(text, sizeof(text), "GPS: %s | sat: %s%lu | korak: %lu ms",
                 !data.gpsOnline ? "NI PODATKOV" : data.fix ? "POLOZAJ OK" : "ISCEM POLOZAJ",
                 data.satellitesValid ? "" : "? ", static_cast<unsigned long>(data.satellites),
                 static_cast<unsigned long>(data.epochMs));
        color = data.fix ? TFT_GREEN : TFT_YELLOW;
        break;
      case 2:
        if (data.fix) snprintf(text, sizeof(text), "LAT %.6f   LON %.6f", data.lat, data.lon);
        else snprintf(text, sizeof(text), "LAT --   LON --");
        break;
      case 3:
        if (data.imuOk) snprintf(text, sizeof(text), "MPU [g]  X %+.3f  Y %+.3f  Z %+.3f", data.ax, data.ay, data.az);
        else snprintf(text, sizeof(text), "MPU: ni svezih meritev");
        color = data.imuOk ? TFT_WHITE : TFT_YELLOW;
        break;
      case 4:
        snprintf(text, sizeof(text), "SD: %s | %s | vrstice: %lu",
                 data.recording ? "ZAPISUJEM" : "USTAVLJEN", data.path,
                 static_cast<unsigned long>(data.rows));
        color = data.recording ? TFT_GREEN : TFT_YELLOW;
        break;
      case 5:
        snprintf(text, sizeof(text), "Napake SD: %lu  NMEA: %lu  Izpadi GPS: %lu",
                 static_cast<unsigned long>(data.errors), static_cast<unsigned long>(data.checksumErrors),
                 static_cast<unsigned long>(data.missed));
        color = data.errors || data.checksumErrors || data.missed ? TFT_ORANGE : TFT_LIGHTGREY;
        break;
      case 6:
        snprintf(text, sizeof(text), "SD najdlje: %.1f ms | delovanje: %lu s",
                 data.maxLogUs / 1000.0, static_cast<unsigned long>(millis() / 1000));
        color = TFT_LIGHTGREY;
        break;
      default:
        snprintf(text, sizeof(text), "Diagnostika | GPS 5 Hz | cas kroga: --");
        y = 294; color = TFT_CYAN;
        break;
    }
    // Opaque text padding removes previous, longer values without clearing the screen.
    if (strcmp(previous[region], text) != 0 || previousColors[region] != color) {
      tft.setTextColor(color, background);
      tft.setTextPadding(region == 0 ? 330 : 448);
      tft.drawString(text, 16, y, font);
      snprintf(previous[region], sizeof(previous[region]), "%s", text);
      previousColors[region] = color;
    }
    region = (region + 1) % 8;
  }

 private:
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
      for (int x : {2, 468}) {
        tft.fillRoundRect(x, y, 10, 40, 3, on ? color : background);
        tft.drawRoundRect(x, y, 10, 40, 3, on ? color : TFT_DARKGREY);
      }
    }
  }

  void updateLap(const DashboardData& data) {
    char text[40];
    uint16_t color = TFT_WHITE;
    if (region < 2) {
      const bool valid = region == 0 ? data.rearPressureValid : data.frontPressureValid;
      const float pressure = region == 0 ? data.rearPressureBar : data.frontPressureBar;
      const char wheel = region == 0 ? 'R' : 'F';
      if (valid) snprintf(text, sizeof(text), "%c %.2f bar", wheel, pressure);
      else snprintf(text, sizeof(text), "%c --.-- bar", wheel);
      color = valid ? TFT_WHITE : TFT_LIGHTGREY;
      if (strcmp(previous[region], text) != 0 || previousColors[region] != color) {
        tft.setTextSize(1); tft.setTextPadding(216);
        tft.setTextColor(color, background);
        tft.drawCentreString(text, region == 0 ? 124 : 356, 40, 4);
      }
    } else if (region == 2) {
      const uint32_t tenths = data.lapTimeMs / 100;
      const uint32_t minutes = tenths / 600;
      snprintf(text, sizeof(text), "%lu:%02lu.%lu",
               static_cast<unsigned long>(minutes),
               static_cast<unsigned long>((tenths / 10) % 60),
               static_cast<unsigned long>(tenths % 10));
      if (strcmp(previous[region], text) != 0) {
        tft.setTextColor(TFT_WHITE, background);
        tft.setTextSize(minutes < 10 ? 3 : 2); tft.setTextPadding(448);
        tft.drawCentreString(text, 240, 104, 7);
        tft.setTextSize(1);
      }
    } else if (region == 3) {
      const float angle = data.leanDegrees;
      const int pixel = data.leanValid ? 240 + static_cast<int>(constrain(angle, -60.0f, 60.0f) * (125.0f / 60.0f)) : -1;
      if (data.leanValid) snprintf(text, sizeof(text), "%.1f", fabsf(angle));
      else snprintf(text, sizeof(text), "--");
      color = data.leanValid ? TFT_CYAN : TFT_DARKGREY;
      if (strcmp(previous[region], text) != 0 || previousColors[region] != color) {
        tft.setTextColor(color, background); tft.setTextPadding(140);
        tft.drawCentreString(text, 240, 279, 4);
        // Draw the degree glyph explicitly: built-in fonts differ in Unicode coverage.
        tft.drawCircle(240 + tft.textWidth(text, 4) / 2 + 5, 283, 2, color);
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
      snprintf(text, sizeof(text), "GPS %.7s    BAT %.4s    SD %.4s", satellites, battery, sd);
      color = TFT_LIGHTGREY;
      if (strcmp(previous[region], text) != 0 || previousColors[region] != color) {
        tft.setTextSize(1); tft.setTextPadding(448);
        tft.setTextColor(color, background);
        tft.drawCentreString(text, 240, 8, 2);
      }
    } else if (region == 5) {
      snprintf(text, sizeof(text), "LAP %lu", static_cast<unsigned long>(data.lapNumber));
      if (strcmp(previous[region], text) != 0) {
        tft.setTextSize(1); tft.setTextPadding(180);
        tft.setTextColor(TFT_LIGHTGREY, background);
        tft.drawString(text, 24, 87, 2);
      }
    } else {
      if (data.greenPreview) {
        snprintf(text, sizeof(text), "TEST");
        color = TFT_GREEN;
      } else if (data.bestDeltaValid) {
        snprintf(text, sizeof(text), "BEST %+.1f s", data.deltaToBestMs / 1000.0);
        color = data.deltaToBestMs < 0 ? TFT_GREEN : data.deltaToBestMs > 0 ? TFT_RED : TFT_LIGHTGREY;
      } else {
        snprintf(text, sizeof(text), "BEST --");
        color = TFT_DARKGREY;
      }
      const unsigned int active = data.greenPreview ? 3 : data.bestDeltaValid ?
          static_cast<unsigned int>(fmin(3.0, ceil(fabs(static_cast<double>(data.deltaToBestMs)) / 200.0))) : 0;
      // Include segment count in the cache key: bar transitions need not change rounded text.
      char key[40];
      snprintf(key, sizeof(key), "%.30s:%u", text, active);
      if (strcmp(previous[region], key) != 0 || previousColors[region] != color) {
        tft.setTextSize(1); tft.setTextPadding(190);
        tft.setTextColor(color, background);
        tft.drawRightString(text, 456, 87, 2);
        drawComparisonBars(active, color);
      }
      snprintf(text, sizeof(text), "%s", key);
    }
    snprintf(previous[region], sizeof(previous[region]), "%s", text);
    previousColors[region] = color;
    region = (region + 1) % 7;
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
      tft.setTextSize(1); tft.setTextPadding(region == 3 ? 300 : 432);
      tft.setTextColor(TFT_CYAN, background);
      const int y = region < 2 ? 112 + region * 24 : region == 2 ? 232 : region == 3 ? 64 : region == 4 ? 160 : 184;
      const int x = region == 3 ? 126 : 24;
      tft.drawString(text, x, y, 2);
      if (region == 0 || (region == 1 && data.leanValid))
        tft.drawCircle(x + tft.textWidth(text, 2) + 4, y + 3, 2, TFT_CYAN);
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
    tft.setTextColor(TFT_LIGHTGREY, background);
    tft.drawString("Sektorji: lastne tocke iz AiM trase", 24, 146, 2);
    snprintf(previous[0], sizeof(previous[0]), "%s", text);
  }

  void updateStationary(const DashboardData& data) {
    const StationaryStats& s = *data.test;
    char text[96];
    uint16_t color = TFT_WHITE;
    switch (region) {
      case 0:
        snprintf(text, sizeof(text), "GPS %s | SAT %lu | HDOP %s%.1f",
                 data.fix ? "OK" : data.gpsOnline ? "ISCEM" : "NI PODATKOV",
                 static_cast<unsigned long>(data.satellites), data.hdopValid ? "" : "? ", data.hdop);
        color = data.fix ? TFT_GREEN : TFT_YELLOW; break;
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
        color = data.imuOk ? TFT_WHITE : TFT_YELLOW; break;
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
        color = s.warming() ? TFT_YELLOW : TFT_CYAN; break;
      default:
        snprintf(text, sizeof(text), "Napake I2C %lu | NMEA %lu | izpadi GPS %lu",
                 static_cast<unsigned long>(s.imuErrors), static_cast<unsigned long>(data.checksumErrors),
                 static_cast<unsigned long>(data.missed));
        color = s.imuErrors || data.checksumErrors || data.missed ? TFT_ORANGE : TFT_LIGHTGREY;
        break;
    }
    if (strcmp(previous[region], text) != 0 || previousColors[region] != color) {
      tft.setTextColor(color, background); tft.setTextPadding(448);
      tft.drawString(text, 16, 44 + region * 24, 2);
      snprintf(previous[region], sizeof(previous[region]), "%s", text);
      previousColors[region] = color;
    }
    // Footer also replaces the startup message after the first update.
    if (region == 0) {
      tft.setTextColor(TFT_CYAN, background); tft.setTextPadding(448);
      tft.drawString("SD IZKLOP | USB: r = nov test, p = ustavi", 16, 294, 2);
    }
    region = (region + 1) % 10;
  }
  static constexpr uint16_t background = 0x0841;
  TFT_eSPI tft;
  uint32_t lastDraw = 0;
  uint8_t region = 0;
  DashboardScreen screen = DashboardScreen::LapTimer;
  int lastLeanPixel = -1;
  char previous[10][96] = {};
  uint16_t previousColors[10] = {};
};
