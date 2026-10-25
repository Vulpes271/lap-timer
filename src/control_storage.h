#pragma once
#include <Preferences.h>
#include <cstddef>
#include "control_model.h"

namespace Control {
struct StoredConfig { uint32_t magic = 0x4C544334; Config config; uint32_t checksum = 0; };
// Exact old on-device layout. Preserve all confirmed units, ranges, IDs and SD settings.
struct ConfigV3 {
  uint32_t version=3;
  uint8_t base=0,speed=0,pressure=0,temperature=0,accel=0,track=0;
  uint8_t holdSeconds=5,deltaIndex=1;
  bool pressureAlarm=false,logging=false;
  float frontMin=0,frontMax=0,rearMin=0,rearMax=0,hysteresis=.05f;
  uint32_t confirmMs=1000,staleMs=120000;
  int16_t utcOffsetMinutes=120;
  char frontId[24]="",rearId[24]="";
};
struct StoredConfigV3 {uint32_t magic=0x4C544333;ConfigV3 config;uint32_t checksum=0;};
inline bool migrateV3(const StoredConfigV3& old,Config& output){
  if(old.magic!=0x4C544333||old.config.version!=3||old.checksum!=hashBytes(&old.config,sizeof(old.config)))return false;
  static_assert(offsetof(Config,rearId)==offsetof(ConfigV3,rearId),"V3 prefix layout changed");
  static_assert(sizeof(Config)>=sizeof(ConfigV3),"V3 migration destination too small");
  Config next;
  std::memcpy(static_cast<void*>(&next),&old.config,sizeof(old.config));
  next.version=4;next.quickSlots[0]=0;next.quickSlots[1]=1;next.quickSlots[2]=2;next.theme=0;
  if(!validConfig(next))return false;
  output=next;return true;
}
inline bool saveConfig(const Config& config, const char* space="lap-control") {
  if (!validConfig(config)) return false;
  StoredConfig record; record.config=config;
  record.checksum=hashBytes(&record.config,sizeof(record.config));
  Preferences store;
  if (!store.begin(space,false)) return false;
  const bool ok=store.putBytes("config",&record,sizeof(record))==sizeof(record);
  store.end();return ok;
}
inline bool loadConfig(Config& config, const char* space="lap-control") {
  Preferences store;
  if (!store.begin(space,true)) return false;
  StoredConfig record;
  if(store.getBytesLength("config")==sizeof(StoredConfigV3)){
    StoredConfigV3 old;
    const bool read=store.getBytes("config",&old,sizeof(old))==sizeof(old);store.end();
    return read && migrateV3(old,config);
  }
  const bool ok=store.getBytesLength("config")==sizeof(record) &&
    store.getBytes("config",&record,sizeof(record))==sizeof(record) && record.magic==0x4C544334 &&
    record.checksum==hashBytes(&record.config,sizeof(record.config)) && validConfig(record.config);
  store.end();if(ok)config=record.config;return ok;
}
}
