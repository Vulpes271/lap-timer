#pragma once
#include <WiFi.h>
#include <WebServer.h>
#include <SPIFFS.h>
#include <Preferences.h>
#include <esp_partition.h>
#include <algorithm>
#include "paddock_state.h"
#include "lap_timing.h"

// Separate from SD logging and existing settings. Only real completed laps enter
// this archive. Read-only HTTP is enabled in the paddock, never during riding.
class PhoneLink {
  struct Header {
    uint32_t magic = 0x4c415031, id = 0, day = 0, number = 0, startUtcMs = 0;
    char track[64] = {};
  };
  struct Lap { uint32_t total, sectors[3]; };
  WebServer server_{80};
  PaddockState motion_;
  Header active_;
  String device_, ssid_, password_, path_;
  bool mounted_ = false, radio_ = false, trip_ = false, error_ = false, writeFailed_ = false;
  uint32_t scope_ = 0, retryAt_ = 0;
  unsigned lapCount_ = 0;

  static String quoted(const char* value) {
    String out = "\"";
    for (; *value; ++value) {
      if (*value == '"' || *value == '\\') out += '\\';
      if (uint8_t(*value) >= 32) out += *value;
    }
    return out + '"';
  }
  static String path(uint32_t id) { return "/phone-" + String(id) + ".bin"; }
  bool header(File& file, Header& h) {
    return file && file.size() >= sizeof(h) && file.read((uint8_t*)&h, sizeof(h)) == sizeof(h)
        && h.magic == 0x4c415031 && h.id && h.track[63] == 0;
  }
  String summary(const Header& h, size_t bytes) {
    return "{\"id\":" + String(h.id) + ",\"day\":" + String(h.day) +
      ",\"number\":" + String(h.number) + ",\"startUtcMs\":" + String(h.startUtcMs) +
      ",\"track\":" + quoted(h.track) + ",\"lapCount\":" + String((bytes-sizeof(Header))/sizeof(Lap)) + "}";
  }
  void respond(const String& body, int status = 200) {
    server_.sendHeader("Cache-Control", "no-store");
    server_.send(status, "application/json", body);
  }
  void sessions() {
    if (!mounted_) { respond("{\"error\":\"storage_unavailable\"}",503); return; }
    String body = "{\"version\":1,\"device\":" + quoted(device_.c_str()) +
      ",\"storageError\":" + (error_ ? "true" : "false") + ",\"sessions\":[";
    File root = SPIFFS.open("/"); bool first = true;
    for (File f = root.openNextFile(); f; f = root.openNextFile()) {
      if (!String(f.name()).startsWith("/phone-") && !String(f.name()).startsWith("phone-")) { f.close(); continue; }
      Header h;
      if (header(f,h)) { if (!first) body += ','; body += summary(h,f.size()); first=false; }
      f.close();
    }
    body += "]}"; respond(body);
  }
  void laps() {
    const String id = server_.arg("id");
    if (!id.length() || id.length()>10) { respond("{}",400); return; }
    for (unsigned i=0;i<id.length();++i) if (!isDigit(id[i])) { respond("{}",400); return; }
    File f = SPIFFS.open(path(strtoul(id.c_str(),nullptr,10)),FILE_READ); Header h;
    if (!header(f,h)) { respond("{}",404); return; }
    String body = "{\"session\":" + summary(h,f.size()) + ",\"laps\":[";
    Lap lap; unsigned n = 0;
    while (f.read((uint8_t*)&lap,sizeof(lap)) == sizeof(lap)) {
      if(n) body += ',';
      body += "{\"number\":" + String(++n) + ",\"lapMs\":" + String(lap.total) +
        ",\"sectorsMs\":[" + String(lap.sectors[0]) + ',' + String(lap.sectors[1]) + ',' + String(lap.sectors[2]) + "]}";
    }
    f.close(); respond(body+"]}");
  }
  bool start(uint32_t day, uint32_t utc, const char* track, uint32_t scope) {
    if (!mounted_) return false;
    uint32_t maxId=0, number=0;
    File root=SPIFFS.open("/");
    for(File f=root.openNextFile();f;f=root.openNextFile()) {
      Header h;
      if(header(f,h)) { maxId=std::max(maxId,h.id); if(h.day==day) number=std::max(number,h.number); }
      f.close();
    }
    active_=Header(); active_.id=maxId+1; active_.number=number+1;
    while(active_.id && SPIFFS.exists(path(active_.id))) ++active_.id;
    if(!active_.id) { error_=true; return false; }
    active_.day=day; active_.startUtcMs=utc;
    strlcpy(active_.track,track,sizeof(active_.track));
    path_=path(active_.id);
    File f=SPIFFS.open(path_,FILE_WRITE);
    const bool ok=f && f.write((uint8_t*)&active_,sizeof(active_))==sizeof(active_);
    if(f) f.close();
    if(!ok) { SPIFFS.remove(path_); error_=true; return false; }
    scope_=scope; trip_=true; lapCount_=0; writeFailed_=false;
    Serial.printf("PHONE: session %lu day %lu\n",(unsigned long)active_.number,(unsigned long)day);
    return true;
  }
 public:
  void begin() {
    // Dedicated internal SPIFFS partition; never formats or accesses the SD card.
    mounted_=SPIFFS.begin(false);
    // Initialize only an erased partition. A corrupt archive is preserved.
    if(!mounted_) {
      const esp_partition_t* partition=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_DATA_SPIFFS,nullptr);
      uint8_t bytes[256]; bool erased=partition!=nullptr;
      for(size_t offset=0;erased && offset<partition->size;offset+=sizeof(bytes)) {
        if(esp_partition_read(partition,offset,bytes,sizeof(bytes))!=ESP_OK) { erased=false; break; }
        for(auto byte:bytes) if(byte!=0xff) { erased=false; break; }
      }
      if(erased && SPIFFS.format()) mounted_=SPIFFS.begin(false);
    }
    device_=String((uint32_t)(ESP.getEfuseMac()>>16),HEX); device_.toUpperCase();
    ssid_="LapTimer-"+device_;
    Preferences prefs; prefs.begin("phone-link",false);
    password_=prefs.getString("password","");
    if(password_.length()<12) { char key[17]; snprintf(key,sizeof(key),"%08lx%08lx",(unsigned long)esp_random(),(unsigned long)esp_random()); password_=key; prefs.putString("password",password_); }
    prefs.end();
    WiFi.persistent(false); WiFi.mode(WIFI_OFF);
    server_.on("/api/v1/sessions",HTTP_GET,[this](){sessions();});
    server_.on("/api/v1/laps",HTTP_GET,[this](){laps();});
    server_.onNotFound([this](){respond("{}",404);});
    Serial.printf("PHONE: SSID=%s PASSWORD=%s IP=192.168.4.1 archive=%s\n",ssid_.c_str(),password_.c_str(),mounted_?"OK":"FAILED");
  }
  bool observe(const Timing::Fix& fix, uint32_t day, uint32_t scope, const char* track) {
    const bool wasStopped=motion_.stopped;
    const bool good=fix.valid && fix.satellites>=4 && fix.hdop>0 && fix.hdop<=3;
    motion_.observe(fix.receivedMs,good,fix.latitude,fix.longitude,fix.speedKmh);
    if (trip_ && (day!=active_.day || scope!=scope_ || (!wasStopped && motion_.stopped))) trip_=false;
    if (good && !motion_.stopped && fix.speedKmh>5 && !trip_) start(day,fix.utcMs,track,scope);
    return !wasStopped && motion_.stopped;
  }
  void timing(Timing::Event event, const Timing::LapTimer& timer) {
    if(event!=Timing::Event::LapCompleted || !trip_ || !mounted_ || writeFailed_) return;
    // Bound a single response and reject overflow explicitly, preserving old laps.
    if(lapCount_>=512) { error_=true; return; }
    Lap lap{uint32_t(std::lround(timer.lastLapMs())),{}};
    for(unsigned i=0;i<3;++i) lap.sectors[i]=uint32_t(std::lround(timer.lastSectorMs(i)));
    File f=SPIFFS.open(path_,FILE_APPEND);
    if(!f || f.write((uint8_t*)&lap,sizeof(lap))!=sizeof(lap)) { error_=true; writeFailed_=true; }
    else ++lapCount_;
    if(f) f.close();
  }
  void poll(uint32_t now) {
    motion_.poll(now);
    if(radio_ && !motion_.stopped) { server_.stop(); WiFi.softAPdisconnect(true); WiFi.mode(WIFI_OFF); radio_=false; }
    if(!radio_ && motion_.stopped && uint32_t(now-retryAt_)>=5000) {
      retryAt_=now; WiFi.mode(WIFI_AP);
      radio_=WiFi.softAP(ssid_.c_str(),password_.c_str(),1,false,1);
      if(radio_) { server_.begin(); Serial.printf("PHONE: Wi-Fi ready %s\n",ssid_.c_str()); }
    }
    if(radio_) server_.handleClient();
  }
};
