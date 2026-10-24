#pragma once

#include <TFT_eSPI.h>
#include "stationary.h"

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
  uint32_t lapTimeMs = 0;  // Remains zero until track/lap detection is implemented.
  uint32_t lapNumber = 0;
  bool bestDeltaValid = false;
  int32_t deltaToBestMs = 0;  // Negative: faster; positive: slower, at equal progress.
  bool frontPressureValid = false, rearPressureValid = false, leanValid = false;
  float frontPressureBar = 0, rearPressureBar = 0, leanDegrees = 0;
  bool batteryValid = false, sdUsageValid = false;
  uint8_t batteryPercent = 0, sdUsedPercent = 0;
  bool greenPreview = false;
  float leanZeroDegrees = 0;
};

enum class DashboardScreen : uint8_t { LapTimer, Diagnostics, Stationary, Settings };

class Dashboard {
 public:
  void begin(bool stationaryMode = false) {
    tft.init();
    tft.setRotation(1);  // Landscape: 480 x 320.
    Serial.print("TFT: register D3 (diagnosticno branje):");
    for (uint8_t index = 0; index < 4; ++index)
      Serial.printf(" %02X", tft.readcommand8(0xD3, index));
    Serial.println();
    // Visual startup test before GPS acquisition: color bars and orientation.
    tft.fillScreen(TFT_BLACK);
    tft.fillRect(0, 0, 160, 240, TFT_RED);
    tft.fillRect(160, 0, 160, 240, TFT_GREEN);
    tft.fillRect(320, 0, 160, 240, TFT_BLUE);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("TFT TEST 480x320", 16, 255, 4);
    tft.drawString("RDECA | ZELENA | MODRA", 16, 292, 2);
    const uint16_t expected[] = {TFT_RED, TFT_GREEN, TFT_BLUE};
    unsigned int matches = 0;
    Serial.print("TFT: branje barv iz pomnilnika:");
    for (uint8_t index = 0; index < 3; ++index) {
      const uint16_t value = tft.readPixel(index * 160 + 80, 100);
      if (value == expected[index]) ++matches;
      Serial.printf(" %04X", value);
    }
    Serial.printf(" | ujemanje=%u/3 (pricakovano F800 07E0 001F).\n", matches);
    Serial.println("TFT: barvni preizkus 2 sekundi, nato prikaz podatkov. LED/BL mora biti napajan za vidno sliko.");
    delay(2000);
    selectScreen(DashboardScreen::LapTimer);
    Serial.printf("TFT: demonstracijski zaslon casa kroga, %dx%d.\n", tft.width(), tft.height());
  }

  // Future button handler can call nextScreen(); pins are not reserved yet.
  void nextScreen() {
    selectScreen(static_cast<DashboardScreen>((static_cast<uint8_t>(screen) + 1) % 4));
  }

  void selectScreen(DashboardScreen selected) {
    screen = selected;
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
    if (screen == DashboardScreen::Settings) {
      tft.setTextColor(TFT_WHITE, background);
      tft.drawString("SETUP", 24, 16, 4);
      tft.drawFastHLine(24, 52, 432, TFT_DARKGREY);
      tft.drawString("Motor postavi naravnost in ga umiri.", 24, 68, 2);
      tft.drawString("Z  Nastavi niclo nagiba", 24, 166, 2);
      tft.drawString("C  Ponastavi niclo nagiba", 24, 190, 2);
      tft.drawString("G  Vklopi / izklopi zeleni preizkus", 24, 214, 2);
      tft.setTextColor(TFT_LIGHTGREY, background);
      tft.drawString("Ukazi prek USB; fizicni gumb sledi.", 24, 268, 2);
      tft.drawString("N  Naslednji zaslon", 24, 292, 2);
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
    tft.drawFastHLine(16, 224, 448, TFT_DARKGREY);
    drawComparisonBars(0, TFT_DARKGREY);
    drawLeanScale();
  }

  void drawLeanScale() {
    tft.drawFastHLine(40, 280, 400, TFT_DARKGREY);
    for (int degrees = -60; degrees <= 60; degrees += 15) {
      const int x = 240 + degrees * 3;
      tft.drawFastVLine(x, degrees == 0 ? 270 : 276, degrees == 0 ? 19 : 9,
                       degrees == 0 ? TFT_WHITE : TFT_DARKGREY);
    }
  }

  void drawComparisonBars(unsigned int active, uint16_t color) {
    // Outside the 448-pixel time text region, so time redraws preserve the bars.
    for (int segment = 0; segment < 5; ++segment) {
      const int y = 104 + (4 - segment) * 22;
      const uint16_t fill = static_cast<unsigned int>(segment) < active ? color : TFT_DARKGREY;
      tft.fillRoundRect(4, y, 8, 18, 3, fill);
      tft.fillRoundRect(468, y, 8, 18, 3, fill);
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
      snprintf(text, sizeof(text), "%02lu:%02lu.%lu",
               static_cast<unsigned long>((tenths / 600) % 100),
               static_cast<unsigned long>((tenths / 10) % 60),
               static_cast<unsigned long>(tenths % 10));
      if (strcmp(previous[region], text) != 0) {
        tft.setTextColor(TFT_WHITE, background);
        tft.setTextSize(2); tft.setTextPadding(448);
        tft.drawCentreString(text, 240, 106, 7);
        tft.setTextSize(1);
      }
    } else if (region == 3) {
      const float angle = data.leanDegrees;
      const int pixel = data.leanValid ? 240 + static_cast<int>(constrain(angle, -60.0f, 60.0f) * 3) : -1;
      if (data.leanValid) snprintf(text, sizeof(text), "%s%.1f deg", angle < -0.05f ? "L " : angle > 0.05f ? "D " : "", fabsf(angle));
      else snprintf(text, sizeof(text), "-- deg");
      color = data.leanValid ? TFT_CYAN : TFT_DARKGREY;
      if (strcmp(previous[region], text) != 0 || previousColors[region] != color) {
        tft.setTextColor(color, background); tft.setTextPadding(220);
        tft.drawCentreString(text, 240, 238, 4);
      }
      if (pixel != lastLeanPixel || color != previousColors[region]) {
        tft.fillRect(32, 268, 416, 23, background);
        drawLeanScale();
        if (pixel >= 0) {
          tft.fillTriangle(pixel - 5, 269, pixel + 5, 269, pixel, 276, color);
          tft.drawFastVLine(pixel, 276, 15, color);
        }
        lastLeanPixel = pixel;
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
      const unsigned int active = data.greenPreview ? 5 : data.bestDeltaValid ?
          static_cast<unsigned int>(fmin(5.0, ceil(fabs(static_cast<double>(data.deltaToBestMs)) / 200.0))) : 0;
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
      snprintf(text, sizeof(text), "Umerjena nicla: %+.2f deg", data.leanZeroDegrees);
    else if (region == 1) {
      if (data.leanValid) snprintf(text, sizeof(text), "Trenutni nagib: %+.1f deg", data.leanDegrees);
      else snprintf(text, sizeof(text), "Trenutni nagib: --");
    } else snprintf(text, sizeof(text), "Zeleni preizkus: %s", data.greenPreview ? "VKLOPLJEN" : "IZKLOPLJEN");
    if (strcmp(previous[region], text) != 0) {
      tft.setTextSize(1); tft.setTextPadding(432);
      tft.setTextColor(TFT_CYAN, background);
      tft.drawString(text, 24, region < 2 ? 100 + region * 24 : 240, 2);
      snprintf(previous[region], sizeof(previous[region]), "%s", text);
    }
    region = (region + 1) % 3;
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
