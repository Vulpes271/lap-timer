#include <Arduino.h>
#include <TinyGPS++.h>
#include <Wire.h>
#include "sd_card.h"
#include <SPI.h>
#include "gps_config.h"
#include "dashboard.h"
#include "build_config.h"
#include "device_settings.h"
#include "lap_timing.h"
#include "pomposa_track.h"
#include "track_catalog.h"
#include "terminal_app.h"
#if LAP_TIMER_DEV
#include "timing_selftest.h"
#include "control_selftest.h"
#include "storage_imu_selftest.h"
#endif
#include "quick_button.h"

namespace {
// NEO TX -> ESP32 RX19; NEO RX -> ESP32 TX4.
constexpr uint32_t FRESH_MS = 5000;
constexpr uint32_t REPORT_MS = 2000;
DeviceSettings deviceSettings;
TerminalApp app;
PanelButtons panelButtons;
QueueHandle_t imuSamples=nullptr;
std::atomic<uint32_t> imuSampleDrops{0};
Timing::LapTimer lapTimer(Tracks::PomposaLong);
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
bool dailyReferencePending=false;
uint32_t missedGpsEpochs = 0;

void reloadDailyReference() {
  Control::Reference reference;
  bool deferred=false;
  const bool found=app.logger.loadBest(app.model.day,app.model.geometry,reference,&deferred);
  if(deferred){dailyReferencePending=true;return;}
  dailyReferencePending=false;
  app.model.real.session.best=reference;
  lapTimer.loadReference(reference.lapMs,reference.sectors);
  Serial.printf("REFERENCE: %s day=%lu geometry=%08lx best=%.3f\n",found?"SD":"SESSION/EMPTY",(unsigned long)app.model.day,(unsigned long)app.model.geometry,reference.lapMs);
}
void selectTrack(unsigned index) {
  lapTimer.selectTrack(*Tracks::Catalog[index]);
  app.model.geometry=0;
  app.model.setScope(0,Control::trackHash(*Tracks::Catalog[index]));
  app.model.simulated.session=Control::Session();
  app.model.simulated.maxima=Control::Maxima();
  dailyReferencePending=false;
  if(app.model.config.logging){app.logger.event(app.model.testMode,app.model.time(millis()),"SESSION_END","track_or_timezone_change");}
  Serial.printf("TRACK: %s, previous lap/session ended\n",Tracks::Catalog[index]->name);
}
void referenceDate(uint32_t utc,uint32_t now,bool fix) {
  static uint32_t candidate=0;static unsigned consistent=0;
  const bool valid=fix && gps.date.isValid() && gps.date.age()<1500 && gps.time.isValid() && gps.time.age()<1500;
  const uint32_t date=valid?Control::localDate(gps.date.year(),gps.date.month(),gps.date.day(),utc,app.model.config.utcOffsetMinutes):0;
  if(candidate!=date){candidate=date;consistent=1;}else if(consistent<3)++consistent;
  if(consistent<2)return;
  if(app.model.setScope(date,Control::trackHash(lapTimer.track()))){
    lapTimer.selectTrack(lapTimer.track());
    reloadDailyReference();
    if(app.model.config.logging && !app.model.testMode){app.logger.stop();app.startLog();}
    Serial.printf("REFERENCE SCOPE: %s day=%lu; active lap discarded on scope change\n",date?"GNSS LOCAL DAY":"UNDATED SESSION",(unsigned long)date);
  }
  (void)now;
}

void detectSd() {
  Serial.println("SD: CS=13, MOSI=15, MISO=2, SCK=14; SPI=1 MHz.");
  sdSpi.begin(14, 2, 15, 13);
  // Disable automatic formatting; preserve all existing card contents.
  if (!Sd::card().begin(13, sdSpi, 1000000, "/sd", 8, false)) {
    Serial.println("SD: priklop datotecnega sistema ni uspel. Preveri kartico, povezave in format.");
    return;
  }
  const uint8_t type = Sd::card().cardType();
  if (type == CARD_NONE) {
    Serial.println("SD: kartica ni zaznana.");
    Sd::card().end();
    return;
  }
  const char* name = type == CARD_MMC ? "MMC" : type == CARD_SD ? "SDSC" :
                     type == CARD_SDHC ? "SDHC/SDXC" : "neznan tip";
  Serial.printf("SD: %s | kapaciteta=%llu MiB | datotecni sistem=%llu MiB | zasedeno=%llu MiB\n",
                name, Sd::card().cardSize() / (1024ULL * 1024),
                Sd::card().totalBytes() / (1024ULL * 1024), Sd::card().usedBytes() / (1024ULL * 1024));
  File root = Sd::card().open("/", FILE_READ);
  uint8_t sector[512];
  const bool rootOk = root && root.isDirectory();
  const bool sectorOk = Sd::card().readRAW(sector, 0);
  if (root) root.close();
  sdReady = rootOk && sectorOk;
  // Cache filesystem usage; do not scan the card inside the display loop.
  const uint64_t total = Sd::card().totalBytes();
  const uint64_t used = Sd::card().usedBytes();
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
  referenceDate(epoch,now,currentGpsFix);
  const bool usable = currentGpsFix && gps.location.isValid() && gps.location.age() < 400 &&
      gps.speed.isValid() && gps.speed.age() < 500 && gps.hdop.isValid() && gps.hdop.age() < 500 &&
      gps.satellites.isValid() && gps.satellites.age() < 500;
  const Timing::Fix timingFix = {usable ? gps.location.lat() : 0, usable ? gps.location.lng() : 0,
      epoch, now, usable ? gps.speed.kmph() : 0, usable ? gps.hdop.hdop() : 0,
      usable ? static_cast<unsigned int>(gps.satellites.value()) : 0, usable};
  app.model.observeSpeed(false,timingFix.speedKmh,usable&&timingFix.satellites>=4&&timingFix.hdop>0&&timingFix.hdop<=3);
  if(dailyReferencePending)reloadDailyReference();
  const Timing::Event timingEvent = dailyReferencePending?Timing::Event::None:lapTimer.process(timingFix);
  app.onTiming(timingEvent,lapTimer,now);
  app.imu.navigation(epoch,now,usable?gps.speed.kmph():0,
    gps.course.isValid()?gps.course.deg():0,usable && gps.course.isValid() && gps.course.age()<500);
  if(app.model.config.logging && !app.model.testMode){
    char line[448];snprintf(line,sizeof(line),"GPS,%lu,%lu,%lu,%u,%.8f,%.8f,%.4f,%.3f,%.2f,%u,%lu,%lu\n",
      (unsigned long)now,(unsigned long)epoch,(unsigned long)app.model.day,usable,timingFix.latitude,timingFix.longitude,timingFix.speedKmh,
      gps.course.isValid()?gps.course.deg():NAN,timingFix.hdop,timingFix.satellites,(unsigned long)missedGpsEpochs,(unsigned long)gps.failedChecksum());app.logger.line(false,line);
  }
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

}

void consumeImuSamples() {
  if(!imuSamples)return;
  Imu::Sample sample;
  while(xQueueReceive(imuSamples,&sample,0)==pdTRUE){
    lastImuOk=sample.valid;lastImuAt=sample.ms;
    if(!sample.valid){app.imu.process(sample);app.logImu(sample);continue;}
    lastAx=sample.accel.x;lastAy=sample.accel.y;lastAz=sample.accel.z;
    lastGx=sample.gyro.x;lastGy=sample.gyro.y;lastGz=sample.gyro.z;
    app.imu.process(sample);app.logImu(sample);
    app.model.observeImu(false,deviceSettings.correctedLean(app.imu.rollDegrees()),app.imu.valid,
                        app.imu.longitudinalG,app.imu.valid);
  }
  if(app.rawImuDrops!=imuSampleDrops.load()){
    app.rawImuDrops=imuSampleDrops.load();
    if(app.model.config.logging&&!app.model.testMode){char detail[48];snprintf(detail,sizeof(detail),"total=%lu",(unsigned long)app.rawImuDrops);app.logger.event(false,millis(),"IMU_QUEUE_DROP",detail);}
  }
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

void imuAcquisition(void*) {
  TickType_t wake=xTaskGetTickCount();
  for(;;){
    Imu::Sample sample;uint8_t raw[14]={};
    sample.valid=imuAddress && readRegisters(imuAddress,0x3B,raw,sizeof(raw));
    sample.us=micros();sample.ms=millis();
    if(sample.valid){sample.accel={signedWord(raw)/16384.0,signedWord(raw+2)/16384.0,signedWord(raw+4)/16384.0};
      sample.gyro={signedWord(raw+8)/131.0,signedWord(raw+10)/131.0,signedWord(raw+12)/131.0};}
    if(xQueueSend(imuSamples,&sample,0)!=pdTRUE)++imuSampleDrops;
    vTaskDelayUntil(&wake,pdMS_TO_TICKS(20));
  }
}

bool zeroLean() {
  const double norm=std::sqrt(lastAx*lastAx+lastAy*lastAy+lastAz*lastAz);
  const bool stable=app.imu.valid && lastImuOk && millis()-lastImuAt<150 && norm>.9 && norm<1.1 &&
    std::hypot(lastGx,std::hypot(lastGy,lastGz))<8;
  const bool ok=stable && deviceSettings.setLeanZero(app.imu.rollDegrees());
  if(ok)app.model.clearImuMaxima();
  if(ok&&app.model.config.logging){char detail[48];snprintf(detail,sizeof(detail),"zero=%.3f",deviceSettings.leanZeroDegrees);app.logger.event(app.model.testMode,app.model.time(millis()),"LEAN_ZERO",detail);}
  Serial.printf("IMU ZERO: %s value=%+.3f (requires calibrated stable IMU)\n",ok?"SAVED":"REJECTED",deviceSettings.leanZeroDegrees);return ok;
}
void reportImu() {
  Serial.printf("IMU raw age=%lu ms, valid=%u, accel_g=%+.4f,%+.4f,%+.4f gyro_dps=%+.3f,%+.3f,%+.3f drops=%lu | %s\n",
    (unsigned long)(millis()-lastImuAt),lastImuOk,lastAx,lastAy,lastAz,lastGx,lastGy,lastGz,(unsigned long)imuSampleDrops,app.imu.quality);
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
  const bool gpsOnline=receivedSentence&&now-lastGpsSentence<1500;
  const bool fix=gpsOnline&&currentGpsFix&&haveGga&&now-lastGga<1500&&gps.location.isValid()&&gps.location.age()<1500;
  data.speedValid=fix&&gps.speed.isValid()&&gps.speed.age()<1500;
  if(data.speedValid)data.speed=gps.speed.kmph();
  data.satellitesValid=gpsOnline&&gps.satellites.isValid()&&gps.satellites.age()<1500;
  if(data.satellitesValid)data.satellites=gps.satellites.value();
  data.sdUsageValid = sdUsageValid && app.logger.ready.load();
  data.sdUsedPercent = sdUsedPercent;
  data.lapTimeMs = lapTimer.elapsedMs(now);
  data.lapNumber = lapTimer.lapNumber();
  data.leanValid = lastImuOk && now-lastImuAt<150 && app.imu.valid;
  if (data.leanValid)
    data.leanDegrees = deviceSettings.correctedLean(app.imu.rollDegrees());
  data.leanZeroDegrees = deviceSettings.leanZeroDegrees;
  const auto& source=app.model.source();const auto& session=source.session;
  data.bestDeltaValid = session.deltaValid;
  data.deltaToBestMs = session.delta;
  data.deltaStepMs=app.model.deltaStepMs();
  data.pressureUnits=app.model.config.pressure;
  data.testSource=app.model.testMode;data.pressureAlert=app.model.alertPresent();
  data.frontPressureValid=source.pressure[0].fresh(app.model.time(now),app.model.config.staleMs);
  data.rearPressureValid=source.pressure[1].fresh(app.model.time(now),app.model.config.staleMs);
  data.frontPressureBar=source.pressure[0].bar;data.rearPressureBar=source.pressure[1].bar;
  if(app.model.testMode){data.lapNumber=session.laps?session.laps+1:0;data.lapTimeMs=session.laps?app.model.time(now)-session.lapAt:0;}
  dashboard.updateControlled(data,app.model,app.imu);
}
}  // namespace

void setup() {
  Serial.setRxBufferSize(1024);
  Serial.begin(115200);
  delay(500);
  Serial.println("\nESP32 lap timer: diagnostika GPS in MPU6500/9250/9255.");
  deviceSettings.begin();
  Serial.printf("SETUP: nicla nagiba=%+.2f deg. USB: help / status / imu zero.\n",
                deviceSettings.leanZeroDegrees);
  Serial.printf("PROGA: %s (AiM legacy %s), cilj=%.6f,%.6f, polsirina=%.1f m.\n",
                lapTimer.track().name, lapTimer.track().sourceName, lapTimer.track().latitude,
                lapTimer.track().longitude, lapTimer.track().halfWidthM);
  dashboard.begin();
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
  Serial.println("USB: 115200. GPS nominalno 5 Hz, IMU nominalno 50 Hz. 'help' za terminal; 'verbose on' za diagnostiko.");
  // SD initialization may block; start measuring GPS intervals with fresh UART data.
  while (gpsSerial.available()) gpsSerial.read();
  app.zeroImu=zeroLean;app.changeTrack=selectTrack;
  app.renderStatus=[](){dashboard.reportRendering();};
  app.leanZero=[](){return deviceSettings.leanZeroDegrees;};
#if LAP_TIMER_DEV
  app.selftest=[](){runTimingSelfTests(Serial);runControlSelfTests(Serial);};
  app.storageImuSelftest=[](){runStorageImuSelfTests(Serial);};
  app.settingsSelftest=[](){runSettingsSelfTests(Serial);};
#endif
  app.storageChanged=[](){
    sdReady=app.logger.ready.load();sdUsageValid=app.logger.formattedUsageValid;sdUsedPercent=app.logger.formattedUsedPercent;
    const double sectors[3]={0,0,0};lapTimer.loadReference(0,sectors);
  };
  app.calibrationChanged=[](){deviceSettings.setLeanZero(0);app.model.clearImuMaxima();if(app.model.config.logging){app.logger.stop();app.startLog();}};
  app.reloadReference=[](){if(lapTimer.running()||app.model.testMode||!app.model.day){Serial.println("REFERENCE: requires REAL, stopped and valid GNSS day");return false;}reloadDailyReference();return true;};
  app.begin(sdReady);
  const bool panelReady=panelButtons.begin();
  app.panelState=panelButtons.initial;app.panel.begin(app.panelState,millis());
  Serial.printf("PANEL: GREEN=32 RED=5 LEFT=34 RIGHT=35, contacts to GND, external 10k to 3.3V on 34/35, task=%s\n",
                panelReady?"READY":"FAILED");
  lapTimer.selectTrack(*Tracks::Catalog[app.model.config.track]);
  imuSamples=xQueueCreate(64,sizeof(Imu::Sample));
  if(!imuSamples || xTaskCreatePinnedToCore(imuAcquisition,"imu-sample",3072,nullptr,2,nullptr,1)!=pdPASS)
    Serial.println("IMU TASK: FAILED");
  Serial.println("READY terminal-v1; enter help (all commands require Enter)");
}

void loop() {
  while (gpsSerial.available()) {
    const char byte = static_cast<char>(gpsSerial.read());
    receivedByte = true;
    lastGpsByte = millis();
    if (gps.encode(byte)) {
      receivedSentence = true;
      lastGpsSentence = millis();

    }
    trackGga(gpGgaTime, gpGgaQuality);
    trackGga(gnGgaTime, gnGgaQuality);
  }
  consumeImuSamples();
  if (lapTimer.poll(millis()) == Timing::Event::Interrupted) {
    app.onTiming(Timing::Event::Interrupted,lapTimer,millis());
    Serial.println("TIMING: izpad GPS, trenutni krog neveljaven.");
  }
  app.panelMotion={lapTimer.running(),currentGpsFix && gps.speed.isValid() && gps.speed.age()<1500,
                   gps.speed.isValid()?gps.speed.kmph():0};
  PanelButtons::Frame panelFrame;
  const uint32_t dropped=panelButtons.dropped.load();
  if(dropped!=app.panelDrops){
    // Never replay an incomplete sequence of contacts after a queue overflow.
    while(panelButtons.pop(panelFrame)){}
    app.panelState=panelButtons.live();app.panel.begin(app.panelState,millis());
    app.model.input(Control::Input::SettingsOff,millis());
    app.panelDrops=dropped;Serial.println("PANEL: queue overflow, release GREEN and cycle RED OFF/ON");
  }
  while(panelButtons.pop(panelFrame))app.panelInput(panelFrame.state,panelFrame.at);
  app.poll();
  if (app.verbose && millis() - lastReport >= REPORT_MS) {
    lastReport = millis();
    reportGps();
    reportImu();
    reportTiming();
    Serial.printf("SD LOG: mounted=%u active=%u file=%s rows=%lu dropped=%lu errors=%lu max_us=%lu\n",
      app.logger.ready.load(),app.logger.active,app.logger.path,(unsigned long)app.logger.written,
      (unsigned long)app.logger.dropped,(unsigned long)app.logger.errors,(unsigned long)app.logger.maxWriteUs);
  }
  updateDashboard();
  delay(1);
}
