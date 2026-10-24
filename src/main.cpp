#include <Arduino.h>
#include <TinyGPS++.h>
#include <Wire.h>
#include <SD.h>
#include <SPI.h>
#include "gps_config.h"
#include "dashboard.h"
#include "stationary.h"
#include "device_settings.h"
#include "lap_timing.h"
#include "pomposa_track.h"
#include "track_catalog.h"
#include "timing_selftest.h"

namespace {
// NEO TX -> ESP32 RX19; NEO RX -> ESP32 TX4.
constexpr uint32_t FRESH_MS = 5000;
constexpr uint32_t REPORT_MS = 2000;
constexpr bool STATIONARY_TEST = true;  // Display-only test; no SD writes.
StationaryStats stationary;
DeviceSettings deviceSettings;
Timing::LapTimer lapTimer(Tracks::PomposaLong);
unsigned int selectedTrack = 0;
uint32_t lastImuPoll = 0;
float lastGx = 0, lastGy = 0, lastGz = 0;
HardwareSerial gpsSerial(2);
uint32_t gpsBaud = 9600;
TinyGPSPlus gps;
TinyGPSCustom gpGgaTime(gps, "GPGGA", 1);
TinyGPSCustom gnGgaTime(gps, "GNGGA", 1);
TinyGPSCustom gpGgaQuality(gps, "GPGGA", 6);
TinyGPSCustom gnGgaQuality(gps, "GNGGA", 6);
SPIClass sdSpi(HSPI);
bool sdReady = false;
bool sdUsageValid = false;
uint8_t sdUsedPercent = 0;
bool haveGga = false;
uint32_t lastGga = 0, ggaInterval = 0, intervalMin = UINT32_MAX;
uint32_t intervalMax = 0, intervalCount = 0;
uint64_t intervalTotal = 0;
uint32_t lastGgaUtc = 0, ggaEpochInterval = 0;
uint8_t imuAddress = 0;
uint32_t lastReport = 0, lastGpsByte = 0, lastGpsSentence = 0;
bool receivedByte = false, receivedSentence = false;
Dashboard dashboard;
bool currentGpsFix = false, lastImuOk = false;
uint32_t lastImuAt = 0;
float lastAx = 0, lastAy = 0, lastAz = 0;
char logPath[24] = {};
bool loggingReady = false;
uint32_t loggedRows = 0, logErrors = 0, maxLogUs = 0, missedGpsEpochs = 0;

void beginLog() {
  if (STATIONARY_TEST) {
    Serial.println("MIRUJOCI TEST: SD zapis izklopljen. Test USTAVLJEN; 'r' zacne nov test, 'p' ustavi.");
    return;
  }
  if (!sdReady) return;
  // Preserve existing files, including files from earlier ESP32 boots.
  bool available = false;
  for (unsigned int index = 0; index < 10000; ++index) {
    snprintf(logPath, sizeof(logPath), "/gps%04u.csv", index);
    if (!SD.exists(logPath)) { available = true; break; }
  }
  if (!available) { Serial.println("SD LOG: ni prostega imena datoteke."); return; }
  File file = SD.open(logPath, FILE_WRITE);
  const char* header = "seq,esp_ms,gps_utc_ms,fix,lat,lon,satellites,imu_ok,ax_g,ay_g,az_g,gx_dps,gy_dps,gz_dps\n";
  if (!file) { ++logErrors; return; }
  const bool ok = file.print(header) == strlen(header);
  file.close();
  loggingReady = ok;
  if (!ok) ++logErrors;
  Serial.printf("SD LOG: %s, datoteka=%s. Vsak GPS GGA zapis preverimo z branjem.\n",
                ok ? "pripravljen" : "NAPAKA", logPath);
}

bool readRegisters(uint8_t address, uint8_t reg, uint8_t* data, uint8_t count);
int16_t signedWord(const uint8_t* data);

void logGpsEpoch(uint32_t utc, uint32_t receivedAt, bool currentFix) {
  if (!loggingReady) return;
  const uint32_t started = micros();
  uint8_t imu[14] = {};
  const bool imuOk = imuAddress && readRegisters(imuAddress, 0x3B, imu, sizeof(imu));
  lastImuOk = imuOk;
  if (imuOk) {
    lastImuAt = millis();
    lastAx = signedWord(imu) / 16384.0f;
    lastAy = signedWord(imu + 2) / 16384.0f;
    lastAz = signedWord(imu + 4) / 16384.0f;
  }
  const bool fix = currentFix && gps.location.isValid() && gps.location.age() < 400;
  const bool sats = gps.satellites.isValid() && gps.satellites.age() < FRESH_MS;
  char line[256];
  const int length = snprintf(line, sizeof(line),
      "%lu,%lu,%lu,%u,%.7f,%.7f,%lu,%u,%.4f,%.4f,%.4f,%.3f,%.3f,%.3f\n",
      static_cast<unsigned long>(loggedRows + 1), static_cast<unsigned long>(receivedAt),
      static_cast<unsigned long>(utc), fix, fix ? gps.location.lat() : 0.0,
      fix ? gps.location.lng() : 0.0, sats ? gps.satellites.value() : 0UL, imuOk,
      signedWord(imu) / 16384.0, signedWord(imu + 2) / 16384.0,
      signedWord(imu + 4) / 16384.0, signedWord(imu + 8) / 131.0,
      signedWord(imu + 10) / 131.0, signedWord(imu + 12) / 131.0);
  bool ok = length > 0 && length < static_cast<int>(sizeof(line));
  File file;
  size_t offset = 0;
  if (ok) {
    file = SD.open(logPath, FILE_APPEND);
    ok = static_cast<bool>(file);
  }
  if (ok) {
    offset = file.size();
    ok = file.write(reinterpret_cast<const uint8_t*>(line), length) == static_cast<size_t>(length);
  }
  if (file) file.close();  // Commit data and file size before readback.
  if (ok) {
    file = SD.open(logPath, FILE_READ);
    char readback[256];
    ok = file && file.seek(offset) &&
         file.read(reinterpret_cast<uint8_t*>(readback), length) == static_cast<size_t>(length) &&
         memcmp(readback, line, length) == 0;
    if (file) file.close();
  }
  const uint32_t duration = micros() - started;
  if (duration > maxLogUs) maxLogUs = duration;
  if (ok) ++loggedRows;
  else {
    ++logErrors;
    loggingReady = false;
    Serial.println("SD LOG: napaka zapisa ali primerjave prebranih podatkov; zapis ustavljen.");
  }
}

void detectSd() {
  Serial.println("SD: CS=13, MOSI=15, MISO=2, SCK=14; SPI=1 MHz.");
  sdSpi.begin(14, 2, 15, 13);
  // Disable automatic formatting; preserve all existing card contents.
  if (!SD.begin(13, sdSpi, 1000000, "/sd", 5, false)) {
    Serial.println("SD: priklop datotecnega sistema ni uspel. Preveri kartico, povezave in format.");
    return;
  }
  const uint8_t type = SD.cardType();
  if (type == CARD_NONE) {
    Serial.println("SD: kartica ni zaznana.");
    SD.end();
    return;
  }
  const char* name = type == CARD_MMC ? "MMC" : type == CARD_SD ? "SDSC" :
                     type == CARD_SDHC ? "SDHC/SDXC" : "neznan tip";
  Serial.printf("SD: %s | kapaciteta=%llu MiB | datotecni sistem=%llu MiB | zasedeno=%llu MiB\n",
                name, SD.cardSize() / (1024ULL * 1024),
                SD.totalBytes() / (1024ULL * 1024), SD.usedBytes() / (1024ULL * 1024));
  File root = SD.open("/", FILE_READ);
  uint8_t sector[512];
  const bool rootOk = root && root.isDirectory();
  const bool sectorOk = SD.readRAW(sector, 0);
  if (root) root.close();
  sdReady = rootOk && sectorOk;
  // Cache filesystem usage; do not scan the card inside the display loop.
  const uint64_t total = SD.totalBytes();
  const uint64_t used = SD.usedBytes();
  sdUsageValid = sdReady && total > 0;
  if (sdUsageValid) sdUsedPercent = static_cast<uint8_t>(min(100ULL, used * 100ULL / total));
  Serial.printf("SD: korenski imenik=%s, branje sektorja=%s.\n",
                rootOk ? "OK" : "NAPAKA", sectorOk ? "OK" : "NAPAKA");
}

void trackGga(TinyGPSCustom& timeField, TinyGPSCustom& qualityField) {
  if (!timeField.isUpdated()) return;
  const char* utc = timeField.value();
  if (strlen(utc) < 6) return;
  const uint32_t epoch = ((utc[0] - '0') * 10 + utc[1] - '0') * 3600000UL +
                        ((utc[2] - '0') * 10 + utc[3] - '0') * 60000UL +
                        static_cast<uint32_t>(atof(utc + 4) * 1000.0 + 0.5);
  if (haveGga && epoch == lastGgaUtc) return;  // Count unique GPS epochs.
  const uint32_t now = millis();
  if (haveGga) {
    ggaEpochInterval = (epoch + 86400000UL - lastGgaUtc) % 86400000UL;
    if (ggaEpochInterval > 200) missedGpsEpochs += (ggaEpochInterval - 1) / 200;
    ggaInterval = now - lastGga;
    if (ggaInterval < intervalMin) intervalMin = ggaInterval;
    if (ggaInterval > intervalMax) intervalMax = ggaInterval;
    intervalTotal += ggaInterval;
    ++intervalCount;
  }
  lastGga = now;
  lastGgaUtc = epoch;
  haveGga = true;
  currentGpsFix = atoi(qualityField.value()) > 0;
  const bool usable = currentGpsFix && gps.location.isValid() && gps.location.age() < 400 &&
      gps.speed.isValid() && gps.speed.age() < 500 && gps.hdop.isValid() && gps.hdop.age() < 500 &&
      gps.satellites.isValid() && gps.satellites.age() < 500;
  const Timing::Fix timingFix = {usable ? gps.location.lat() : 0, usable ? gps.location.lng() : 0,
      epoch, now, usable ? gps.speed.kmph() : 0, usable ? gps.hdop.hdop() : 0,
      usable ? static_cast<unsigned int>(gps.satellites.value()) : 0, usable};
  const Timing::Event timingEvent = lapTimer.process(timingFix);
  if (timingEvent == Timing::Event::Started)
    Serial.printf("TIMING: START %s, LAP %u, GPS=%.3f ms\n", lapTimer.track().name, lapTimer.lapNumber(), lapTimer.lastCrossingMs());
  else if (timingEvent == Timing::Event::LapCompleted)
    Serial.printf("TIMING: LAP %u koncan, cas=%.3f ms, BEST=%.3f ms; LAP %u\n",
                  lapTimer.completedLaps(), lapTimer.lastLapMs(), lapTimer.bestLapMs(), lapTimer.lapNumber());
  else if (timingEvent == Timing::Event::SectorCompleted)
    Serial.printf("TIMING: S%u koncan; primerjava=%s %+.3f s\n", lapTimer.currentSector() - 1,
                  lapTimer.deltaValid() ? "BEST" : "BREZ REFERENCE", lapTimer.deltaToBestMs() / 1000);
  else if (timingEvent == Timing::Event::LapRestarted)
    Serial.println("TIMING: manjka sektor, krog ni shranjen. Zacet nov krog.");
  else if (timingEvent == Timing::Event::Interrupted)
    Serial.println("TIMING: trenutni krog neveljaven (GPS/kakovost/skok/cas). Cakam nov prehod.");
  if (STATIONARY_TEST && currentGpsFix && gps.location.isValid() && gps.location.age() < 400)
    stationary.addGps(gps.location.lat(), gps.location.lng());
  logGpsEpoch(epoch, now, currentGpsFix);
}

void sampleStationaryImu() {
  if (!STATIONARY_TEST || millis() - lastImuPoll < 20) return;
  lastImuPoll = millis();
  if (!imuAddress) return;
  uint8_t raw[14];
  lastImuOk = readRegisters(imuAddress, 0x3B, raw, sizeof(raw));
  if (!lastImuOk) { ++stationary.imuErrors; return; }
  lastImuAt = millis();
  lastAx = signedWord(raw) / 16384.0f; lastAy = signedWord(raw + 2) / 16384.0f;
  lastAz = signedWord(raw + 4) / 16384.0f;
  lastGx = signedWord(raw + 8) / 131.0f; lastGy = signedWord(raw + 10) / 131.0f;
  lastGz = signedWord(raw + 12) / 131.0f;
  stationary.addImu(lastAx, lastAy, lastAz, lastGx, lastGy, lastGz);
}

void reportStationary() {
  if (stationary.paused || stationary.warming()) {
    Serial.printf("MIR TEST: %s, IMU n=%lu, GPS n=%lu\n",
                  stationary.paused ? "USTAVLJEN" : "UMIRJANJE",
                  static_cast<unsigned long>(stationary.accelNorm.count),
                  static_cast<unsigned long>(stationary.east.count));
    if (!stationary.accelNorm.count && !stationary.east.count) return;
  }
  Serial.printf("MIR TEST: %lu s, IMU n=%lu, I2C napake=%lu | |a|=%.5f g, sigma=%.5f g, odmik od 1g=%+.2f %%\n",
                static_cast<unsigned long>(stationary.seconds()),
                static_cast<unsigned long>(stationary.accelNorm.count),
                static_cast<unsigned long>(stationary.imuErrors), stationary.accelNorm.mean,
                stationary.accelNorm.sigma(), (stationary.accelNorm.mean - 1) * 100);
  Serial.printf("GYRO povprecje [deg/s]: %+.4f %+.4f %+.4f | sigma: %.4f %.4f %.4f\n",
                stationary.gyro[0].mean, stationary.gyro[1].mean, stationary.gyro[2].mean,
                stationary.gyro[0].sigma(), stationary.gyro[1].sigma(), stationary.gyro[2].sigma());
  Serial.printf("GPS MIR: n=%lu, odmik=%.2f m, najvec=%.2f m, raztros=%.2f m | hitrost n=%lu, povprecje=%.3f, max=%.3f km/h\n",
                static_cast<unsigned long>(stationary.east.count), stationary.drift,
                stationary.maxDrift, stationary.spread(), static_cast<unsigned long>(stationary.speed.count),
                stationary.speed.mean, stationary.speed.maximum);
}

void reportTiming() {
  if (intervalCount && millis() - lastGga < FRESH_MS) {
    Serial.printf("GPS GGA interval: zadnji=%lu ms, povprecje=%.1f ms, min=%lu ms, max=%lu ms, vzorcev=%lu\n",
                  static_cast<unsigned long>(ggaInterval),
                  static_cast<double>(intervalTotal) / intervalCount,
                  static_cast<unsigned long>(intervalMin),
                  static_cast<unsigned long>(intervalMax),
                  static_cast<unsigned long>(intervalCount));
    Serial.printf("GPS cas meritve (UTC): korak=%lu ms | NMEA checksum napake=%lu | manjkajoce 200-ms epohe=%lu\n",
                  static_cast<unsigned long>(ggaEpochInterval), gps.failedChecksum(),
                  static_cast<unsigned long>(missedGpsEpochs));
  } else Serial.println("GPS GGA interval: cakam sveze zaporedne stavke.");
}

bool readRegisters(uint8_t address, uint8_t reg, uint8_t* data, uint8_t count) {
  Wire.beginTransmission(address);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(address, count) != count) {
    while (Wire.available()) Wire.read();
    return false;
  }
  for (uint8_t i = 0; i < count; ++i) data[i] = Wire.read();
  return true;
}

bool writeRegister(uint8_t address, uint8_t reg, uint8_t value) {
  Wire.beginTransmission(address);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

void detectImu() {
  Serial.println("I2C pregled (SDA=21, SCL=22):");
  unsigned int found = 0;
  for (uint8_t address = 1; address < 127; ++address) {
    Wire.beginTransmission(address);
    if (Wire.endTransmission() != 0) continue;
    ++found;
    Serial.printf("  Naprava na 0x%02X\n", address);
    if (address != 0x68 && address != 0x69) continue;
    uint8_t identity = 0;
    if (!readRegisters(address, 0x75, &identity, 1)) {
      Serial.println("  Branje WHO_AM_I ni uspelo.");
      continue;
    }
    Serial.printf("  WHO_AM_I=0x%02X\n", identity);
    if (identity != 0x70 && identity != 0x71 && identity != 0x73) {
      Serial.println("  Identiteta ni MPU6500/MPU9250/MPU9255; preveri model modula.");
      continue;
    }
    if (!writeRegister(address, 0x6B, 0x01)) continue;
    delay(100);
    if (!writeRegister(address, 0x6C, 0x00) ||
        !writeRegister(address, 0x1B, 0x00) ||  // Gyro +/-250 deg/s.
        !writeRegister(address, 0x1C, 0x00) ||  // Accel +/-2 g.
        !writeRegister(address, 0x1A, 0x03) ||
        !writeRegister(address, 0x1D, 0x03)) continue;
    imuAddress = address;
    const char* model = identity == 0x70 ? "MPU6500" :
                        identity == 0x71 ? "MPU9250" : "MPU9255";
    Serial.printf("  %s pripravljen.\n", model);
  }
  if (found == 0) Serial.println("  Nobena I2C naprava se ne odziva.");
  if (imuAddress == 0) Serial.println("MPU ni pripravljen; preveri napajanje in povezave.");
}

int16_t signedWord(const uint8_t* data) {
  return static_cast<int16_t>((static_cast<uint16_t>(data[0]) << 8) | data[1]);
}

void reportImu() {
  if (imuAddress == 0) {
    Serial.println("MPU: ni zaznan. Za ponovni pregled pritisni RESET.");
    return;
  }
  uint8_t data[14];
  if (!readRegisters(imuAddress, 0x3B, data, sizeof(data))) {
    lastImuOk = false;
    Serial.println("MPU: napaka pri branju I2C.");
    return;
  }
  lastImuOk = true;
  lastImuAt = millis();
  lastAx = signedWord(data) / 16384.0f;
  lastAy = signedWord(data + 2) / 16384.0f;
  lastAz = signedWord(data + 4) / 16384.0f;
  Serial.printf("MPU accel [g]: X=%+.3f Y=%+.3f Z=%+.3f | gyro [deg/s]: X=%+.2f Y=%+.2f Z=%+.2f\n",
                signedWord(data) / 16384.0, signedWord(data + 2) / 16384.0,
                signedWord(data + 4) / 16384.0, signedWord(data + 8) / 131.0,
                signedWord(data + 10) / 131.0, signedWord(data + 12) / 131.0);
}

void reportGps() {
  const uint32_t now = millis();
  if (!receivedByte || now - lastGpsByte > FRESH_MS) {
    Serial.printf("GPS: ni svezih UART podatkov pri %lu baud. Preveri TX -> GPIO19, napajanje in GND.\n",
                  static_cast<unsigned long>(gpsBaud));
    return;
  }
  if (!receivedSentence || now - lastGpsSentence > FRESH_MS) {
    Serial.printf("GPS: UART podatki prihajajo, ni veljavnih NMEA stavkov. Preveri baud. Bajti=%lu, napacne kontrolne vsote=%lu\n",
                  gps.charsProcessed(), gps.failedChecksum());
    return;
  }
  Serial.print("GPS: veljavni NMEA podatki | sateliti v uporabi=");
  if (gps.satellites.isValid() && gps.satellites.age() < FRESH_MS)
    Serial.print(gps.satellites.value());
  else Serial.print("?");
  if (gps.location.isValid() && gps.location.age() < FRESH_MS) {
    Serial.printf(" | polozaj=%.6f, %.6f", gps.location.lat(), gps.location.lng());
    if (gps.speed.isValid() && gps.speed.age() < FRESH_MS)
      Serial.printf(" | hitrost=%.2f km/h", gps.speed.kmph());
    Serial.println();
  } else {
    Serial.println(" | brez svezega satelitskega polozaja (v notranjosti pricakovano).");
  }
}

void updateDashboard() {
  const uint32_t now = millis();
  DashboardData data;
  data.gpsOnline = receivedSentence && now - lastGpsSentence < 1500;
  data.fix = data.gpsOnline && currentGpsFix && haveGga && now - lastGga < 1500 &&
             gps.location.isValid() && gps.location.age() < 1500;
  data.speedValid = data.fix && gps.speed.isValid() && gps.speed.age() < 1500;
  if (data.speedValid) data.speed = gps.speed.kmph();
  if (data.fix) { data.lat = gps.location.lat(); data.lon = gps.location.lng(); }
  data.satellitesValid = data.gpsOnline && gps.satellites.isValid() && gps.satellites.age() < 1500;
  if (data.satellitesValid) data.satellites = gps.satellites.value();
  data.imuOk = lastImuOk && now - lastImuAt < 3000;
  data.ax = lastAx; data.ay = lastAy; data.az = lastAz;
  data.recording = loggingReady;
  data.path = logPath;
  data.rows = loggedRows; data.errors = logErrors;
  data.missed = missedGpsEpochs; data.checksumErrors = gps.failedChecksum();
  data.epochMs = data.gpsOnline ? ggaEpochInterval : 0;
  data.maxLogUs = maxLogUs;
  data.sdUsageValid = sdUsageValid;
  data.sdUsedPercent = sdUsedPercent;
  data.lapTimeMs = lapTimer.elapsedMs(now);
  data.lapNumber = lapTimer.lapNumber();
  data.leanValid = data.imuOk;
  if (data.leanValid)
    data.leanDegrees = deviceSettings.correctedLean(atan2f(lastAy, lastAz) * RAD_TO_DEG);
  data.leanZeroDegrees = deviceSettings.leanZeroDegrees;
  data.greenPreview = deviceSettings.greenPreview;
  data.trackName = lapTimer.track().name;
  data.lastLapMs = lapTimer.lastLapMs();
  data.bestLapMs = lapTimer.bestLapMs();
  data.trackIndex = selectedTrack;
  data.currentSector = lapTimer.currentSector();
  for (unsigned int i = 0; i < 3; ++i) data.sectorMs[i] = lapTimer.lastSectorMs(i);
  data.bestDeltaValid = lapTimer.deltaValid();
  data.deltaToBestMs = static_cast<int32_t>(lapTimer.deltaToBestMs());
  data.stationary = STATIONARY_TEST;
  data.test = &stationary;
  data.gx = lastGx; data.gy = lastGy; data.gz = lastGz;
  data.hdopValid = gps.hdop.isValid() && gps.hdop.age() < 1500;
  if (data.hdopValid) data.hdop = gps.hdop.hdop();
  dashboard.update(data);
}
}  // namespace

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\nESP32 lap timer: diagnostika GPS in MPU6500/9250/9255.");
  deviceSettings.begin();
  Serial.printf("SETUP: nicla nagiba=%+.2f deg. USB: s=setup, z=nicla, c=ponastavi, g=preizkus, n=zaslon, t=test timinga.\n",
                deviceSettings.leanZeroDegrees);
  Serial.printf("PROGA: %s (AiM legacy %s), cilj=%.6f,%.6f, polsirina=%.1f m.\n",
                lapTimer.track().name, lapTimer.track().sourceName, lapTimer.track().latitude,
                lapTimer.track().longitude, lapTimer.track().halfWidthM);
  dashboard.begin(STATIONARY_TEST);
  gpsSerial.setRxBufferSize(2048);
  gpsSerial.begin(9600, SERIAL_8N1, 19, 4);
  Serial.println("GPS: NEO TX -> GPIO19, NEO RX -> GPIO4.");
  Wire.begin(21, 22);
  Wire.setClock(100000);
  Wire.setTimeOut(50);
  detectImu();
  detectSd();
  const bool configured = GpsConfig::configure(gpsSerial, gpsBaud);
  Serial.printf("GPS: nastavitev 5 Hz %s; dejanski interval preveri v izpisu.\n",
                configured ? "potrjena prek UBX" : "NI potrjena");
  Serial.println("Izpis: vsakih 2000 ms. GPS in MPU zapis na SD: ob vsaki GGA meritvi (5 Hz). USB: 115200 baud.");
  // SD initialization may block; start measuring GPS intervals with fresh UART data.
  while (gpsSerial.available()) gpsSerial.read();
  beginLog();
  stationary.reset(false);
}

void loop() {
  while (gpsSerial.available()) {
    const char byte = static_cast<char>(gpsSerial.read());
    receivedByte = true;
    lastGpsByte = millis();
    if (gps.encode(byte)) {
      receivedSentence = true;
      lastGpsSentence = millis();
      // RMC speed is committed once per epoch; consume it once for statistics.
      if (STATIONARY_TEST && gps.speed.isUpdated() && gps.speed.isValid()) {
        const double speed = gps.speed.kmph();
        if (!stationary.paused && !stationary.warming() && gps.location.isValid() && gps.location.age() < 400)
          stationary.speed.add(speed);
      }
    }
    trackGga(gpGgaTime, gpGgaQuality);
    trackGga(gnGgaTime, gnGgaQuality);
  }
  sampleStationaryImu();
  if (lapTimer.poll(millis()) == Timing::Event::Interrupted)
    Serial.println("TIMING: izpad GPS, trenutni krog neveljaven.");
  while (Serial.available()) {
    const char command = Serial.read();
    if (command == 't' || command == 'T') runTimingSelfTests(Serial);
    if (command == 'k' || command == 'K') {
      dashboard.openTracks();
      for (unsigned int i = 0; i < Tracks::Count; ++i)
        Serial.printf("DIRKALISCA: %u = %s%s\n", i + 1, Tracks::Catalog[i]->name, i == selectedTrack ? " [IZBRANO]" : "");
    }
    if (dashboard.tracksOpen() && command >= '1' && command <= '9') {
      const unsigned int index = command - '1';
      if (index < Tracks::Count) {
        if (index != selectedTrack) {
          selectedTrack = index;
          lapTimer.selectTrack(*Tracks::Catalog[index]);
        }
        Serial.printf("DIRKALISCE: %s, sektorji=%u\n", lapTimer.track().name, lapTimer.track().splitCount + 1);
      } else Serial.println("DIRKALISCE: ta stevilka ni v seznamu.");
    }
    if (command == 'r' || command == 'R') stationary.reset();
    if (command == 'p' || command == 'P') stationary.stop();
    if (command == 'n' || command == 'N') {
      Serial.println("UKAZ: N");
      dashboard.nextScreen();
    }
    if (command == 's' || command == 'S') dashboard.toggleSettings();
    if (command == 'q' || command == 'Q' || command == 'b' || command == 'B' || command == 27)
      dashboard.closeSettings();
    if (command == 'g' || command == 'G') {
      deviceSettings.greenPreview = !deviceSettings.greenPreview;
      Serial.printf("SETUP: zeleni preizkus=%s\n", deviceSettings.greenPreview ? "VKLOPLJEN" : "IZKLOPLJEN");
    }
    if (command == 'z' || command == 'Z') {
      const float norm = sqrtf(lastAx * lastAx + lastAy * lastAy + lastAz * lastAz);
      const bool stable = lastImuOk && millis() - lastImuAt < 200 && norm > 0.8f && norm < 1.2f &&
                          fabsf(lastGx) < 8 && fabsf(lastGy) < 8 && fabsf(lastGz) < 8;
      const bool saved = stable && deviceSettings.setLeanZero(atan2f(lastAy, lastAz) * RAD_TO_DEG);
      Serial.printf("SETUP: nastavitev nicle %s; nicla=%+.2f deg.\n",
                    saved ? "SHRANJENA" : "NI USPELA (preveri mirovanje/MPU/pomnilnik)", deviceSettings.leanZeroDegrees);
    }
    if (command == 'c' || command == 'C') {
      Serial.printf("SETUP: ponastavitev nicle %s\n", deviceSettings.setLeanZero(0) ? "SHRANJENA" : "NI USPELA");
    }
  }
  if (millis() - lastReport >= REPORT_MS) {
    lastReport = millis();
    reportGps();
    reportImu();
    reportTiming();
    if (STATIONARY_TEST) reportStationary();
    Serial.printf("SD: %s (preverjeno ob zagonu).\n", sdReady ? "branje OK" : "ni pripravljena");
    Serial.printf("SD LOG: %s | %s | zapisano in prebrano=%lu | napake=%lu | najdaljsi zapis+branje+MPU=%.2f ms\n",
                  loggingReady ? "AKTIVEN" : "USTAVLJEN", logPath,
                  static_cast<unsigned long>(loggedRows), static_cast<unsigned long>(logErrors),
                  maxLogUs / 1000.0);
  }
  updateDashboard();
  delay(1);
}
