#pragma once
#include <Arduino.h>
#include <atomic>
#include <algorithm>
#include "sd_card.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include "control_model.h"

class AnalysisLogger {
 public:
  struct BestRecord {
    uint32_t magic=0x4C544231,version=1,day=0,geometry=0,sequence=0;
    Control::Reference reference;
    uint32_t checksum=0;
  };
  enum Kind : uint8_t { Line, Start, Stop, Best, Diagnostic, ReferenceTest, Inspect, ListFiles, Format };
  struct Job { Kind kind=Line; bool test=false; uint32_t day=0,geometry=0;Control::Reference reference;char text[448]=""; };
  std::atomic<bool> ready{false};
  volatile bool active=false;
  std::atomic<uint32_t> dropped{0},errors{0},written{0},maxWriteUs{0};
  volatile uint8_t fault=0; // 1 fail, 2 slow; restricted to test files only.
  char path[64]="";
  std::atomic<bool> formatting{false};
  std::atomic<uint32_t> formatGeneration{0};bool formatOk=false;
  bool formattedUsageValid=false;uint8_t formattedUsedPercent=0;
  void begin(bool mounted) {
    ready=mounted;
    mutex=xSemaphoreCreateMutex();pageMutex=xSemaphoreCreateMutex();queue=xQueueCreate(64,sizeof(Job));
    if (!mutex || !pageMutex || !queue) {ready=false;return;}
    workerStarted=xTaskCreatePinnedToCore(worker,"sd-writer",6144,this,1,nullptr,0)==pdPASS;
    if(!workerStarted)ready=false;
  }
  bool submit(const Job& job) {
    if (!workerStarted || !queue || (!ready&&job.kind!=Format) || (formatting&&job.kind!=Format)) {++dropped;return false;}
    if (xQueueSend(queue,&job,0)!=pdTRUE){++dropped;return false;}return true;
  }
  void line(bool test, const char* text) { Job j;j.test=test;std::snprintf(j.text,sizeof(j.text),"%s",text);submit(j); }
  void event(bool test,uint32_t at,const char* type,const char* detail) {
    char text[448];std::snprintf(text,sizeof(text),"EVENT,%lu,%s,%s\n",(unsigned long)at,type,detail);line(test,text);
  }
  void start(bool test,const char* metadata) { Job j;j.kind=Start;j.test=test;std::snprintf(j.text,sizeof(j.text),"%s",metadata);submit(j); }
  void stop() { Job j;j.kind=Stop;submit(j); }
  void best(uint32_t day,uint32_t geometry,const Control::Reference& reference) {
    if (!day) return;
    Job j;j.kind=Best;j.day=day;j.geometry=geometry;j.reference=reference;submit(j);
  }
  bool loadBest(uint32_t day,uint32_t geometry,Control::Reference& reference,bool* deferred=nullptr) {
    if(deferred)*deferred=false;
    if(!ready || !day)return false;
    if(xSemaphoreTake(mutex,0)!=pdTRUE){if(deferred)*deferred=true;return false;}
    BestRecord best;const bool found=readBest(day,geometry,best);
    xSemaphoreGive(mutex);if(found)reference=best.reference;return found;
  }
  void diagnostic() {Job j;j.kind=Diagnostic;submit(j);}
  void testReference() {Job j;j.kind=ReferenceTest;j.test=true;submit(j);}
  void inspect(){Job j;j.kind=Inspect;submit(j);}
  bool listFiles(unsigned first,uint32_t generation){Job j;j.kind=ListFiles;j.day=first;j.geometry=generation;return submit(j);}
  bool getFilePage(Sd::Page& target){
    if(!pageMutex||xSemaphoreTake(pageMutex,0)!=pdTRUE)return false;
    target=page;xSemaphoreGive(pageMutex);return true;
  }
  bool formatCard(){
    if(active||formatting.exchange(true))return false;
    Job j;j.kind=Format;
    if(!submit(j)){formatting=false;return false;}return true;
  }
 private:
  QueueHandle_t queue=nullptr;SemaphoreHandle_t mutex=nullptr,pageMutex=nullptr;
  Sd::Page page;
  bool workerStarted=false;
  Sd::Page listingPage;File directories[4];unsigned listingDepth=0;bool listing=false;
  File file;bool fileTest=false;uint32_t lastFlush=0;
  static void worker(void* self){static_cast<AnalysisLogger*>(self)->run();}
  static uint32_t checksum(const BestRecord& r) {
    uint32_t h=Control::hashBytes(&r.day,sizeof(r.day));h=Control::hashBytes(&r.geometry,sizeof(r.geometry),h);
    h=Control::hashBytes(&r.sequence,sizeof(r.sequence),h);return Control::hashBytes(&r.reference,sizeof(r.reference),h);
  }
  bool readBest(uint32_t day,uint32_t geometry,BestRecord& output,bool test=false) {
    bool found=false;
    for(char slot : {'a','b'}) {
      char name[64];std::snprintf(name,sizeof(name),"%s/best_%08lx_%08lu_%c.dat",test?"/test":"",(unsigned long)geometry,(unsigned long)day,slot);
      File input=Sd::card().open(name,FILE_READ);BestRecord r;
      const bool ok=input && input.size()==sizeof(r) && input.read(reinterpret_cast<uint8_t*>(&r),sizeof(r))==sizeof(r);
      if(input)input.close();
      const double sum=r.reference.sectors[0]+r.reference.sectors[1]+r.reference.sectors[2];
      if(ok && r.magic==0x4C544231 && r.version==1 && r.day==day && r.geometry==geometry &&
        r.checksum==checksum(r) && std::isfinite(r.reference.lapMs) && r.reference.lapMs>=30000 &&
        r.reference.lapMs<=600000 && r.reference.sectors[0]>0 && r.reference.sectors[1]>0 &&
        r.reference.sectors[2]>0 && std::fabs(sum-r.reference.lapMs)<1 && (!found || r.sequence>output.sequence)) {output=r;found=true;}
    }
    return found;
  }
  void saveBest(const Job& j) {
    if(j.test && !Sd::card().exists("/test"))Sd::card().mkdir("/test");
    BestRecord previous;readBest(j.day,j.geometry,previous,j.test);
    BestRecord r;r.day=j.day;r.geometry=j.geometry;r.sequence=previous.sequence+1;r.reference=j.reference;r.checksum=checksum(r);
    char target[64];std::snprintf(target,sizeof(target),"%s/best_%08lx_%08lu_%c.dat",j.test?"/test":"",(unsigned long)j.geometry,(unsigned long)j.day,r.sequence%2?'a':'b');
    const char* temp=j.test?"/test/best_pending.tmp":"/best_pending.tmp";
    File output=Sd::card().open(temp,FILE_WRITE);bool ok=output && output.write(reinterpret_cast<const uint8_t*>(&r),sizeof(r))==sizeof(r);
    if(output){output.flush();output.close();}
    BestRecord verify;File input=Sd::card().open(temp,FILE_READ);
    ok=ok && input && input.size()==sizeof(r) && input.read(reinterpret_cast<uint8_t*>(&verify),sizeof(verify))==sizeof(verify) && verify.checksum==checksum(verify);
    if(input)input.close();
    // Alternate slots preserve the other valid record if power fails during replacement.
    if(ok){if(Sd::card().exists(target))ok=Sd::card().remove(target);if(ok)ok=Sd::card().rename(temp,target);}
    if(!ok)++errors;
    Serial.printf("SD BEST: %s day=%lu geometry=%08lx\n",ok?"SAVED":"FAILED",(unsigned long)j.day,(unsigned long)j.geometry);
  }
  void openSession(const Job& j) {
    close();const char* dir=j.test?"/test":"/sessions";
    if(!Sd::card().exists(dir) && !Sd::card().mkdir(dir)){++errors;return;}
    bool available=false;
    for(unsigned index=0;index<1000000;++index){std::snprintf(path,sizeof(path),"%s/s%06u.csv",dir,index);if(!Sd::card().exists(path)){available=true;break;}}
    if(!available){++errors;return;}
    file=Sd::card().open(path,FILE_WRITE);fileTest=j.test;active=bool(file);
    if(!file){++errors;return;}
    const String header=String("# firmware=terminal-v1,source=")+(j.test?"TEST":"REAL")+",schema=1\n# "+j.text+"\n# tagged CSV; each stream owns timestamp; units canonical\n";
    if(file.print(header)!=header.length()){++errors;close();return;}
    lastFlush=millis();Serial.printf("SD SESSION: %s %s\n",j.test?"TEST":"REAL",path);
  }
  void close(){if(file){file.flush();file.close();}active=false;}
  void readback() {
    const char* name="/terminal_readback.tmp";
    if(Sd::card().exists(name)){Serial.println("SD READBACK: SKIP (existing diagnostic file preserved)");return;}
    const char text[]="lap-timer diagnostic readback\n";
    File out=Sd::card().open(name,FILE_WRITE);bool ok=out && out.write((const uint8_t*)text,sizeof(text))==sizeof(text);
    if(out)out.close();
    char actual[sizeof(text)]={};File in=Sd::card().open(name,FILE_READ);
    ok=ok && in && in.read((uint8_t*)actual,sizeof(actual))==sizeof(actual) && !std::memcmp(actual,text,sizeof(text));
    if(in)in.close();
    Sd::card().remove(name);if(!ok)++errors;Serial.printf("SD READBACK: %s\n",ok?"PASS":"FAIL");
  }
  void referenceTest(){
    Job job;job.kind=Best;job.test=true;job.day=20991231;job.geometry=0x54455354;
    job.reference.lapMs=123456;for(auto& value:job.reference.sectors)value=41152;
    saveBest(job);BestRecord first;
    const bool loaded=readBest(job.day,job.geometry,first,true)&&first.reference.lapMs==123456;
    saveBest(job);BestRecord second;
    const bool newer=readBest(job.day,job.geometry,second,true)&&second.sequence>first.sequence;
    char name[64];std::snprintf(name,sizeof(name),"/test/best_%08lx_%08lu_%c.dat",(unsigned long)job.geometry,(unsigned long)job.day,second.sequence%2?'a':'b');
    File out=Sd::card().open(name,FILE_WRITE);if(out){out.print("intentionally corrupt TEST reference");out.close();}
    BestRecord fallback;const bool recovered=readBest(job.day,job.geometry,fallback,true)&&fallback.sequence==first.sequence;
    // Restore the TEST record after exercising the interrupted-write fallback.
    saveBest(job);
    Serial.printf("SD REFERENCE TEST: %s load=%u sequence=%u corrupt_slot_fallback=%u (TEST files only)\n",loaded&&newer&&recovered?"PASS":"FAIL",loaded,newer,recovered);
  }
  void inspectFile(){
    if(file)file.flush();
    File input=Sd::card().open(path,FILE_READ);
    if(!input){Serial.println("SD INSPECT: NO FILE");return;}
    const size_t bytes=input.size();
    if(bytes>8192){input.seek(bytes-8192);while(input.available()&&input.read()!='\n'){} }
    unsigned imuRows=0,gpsRows=0;uint32_t previous=0,minDt=0xFFFFFFFF,maxDt=0;
    char line[448];size_t used=0;
    while(input.available()){
      const char c=input.read();
      if(c=='\n'){
        line[used]=0;unsigned long ms=0,us=0;
        if(std::sscanf(line,"IMU,%lu,%lu",&ms,&us)==2){if(imuRows){const uint32_t dt=uint32_t(us)-previous;if(dt<minDt)minDt=dt;if(dt>maxDt)maxDt=dt;}previous=us;++imuRows;}
        if(!std::strncmp(line,"GPS,",4))++gpsRows;
        used=0;
      }else if(used+1<sizeof(line))line[used++]=c;
    }
    input.close();
    Serial.printf("SD INSPECT: file=%s bytes=%u imu_rows=%u gps_rows=%u imu_min_us=%lu imu_max_us=%lu (last 8192 bytes)\n",path,unsigned(bytes),imuRows,gpsRows,(unsigned long)(minDt==0xFFFFFFFF?0:minDt),(unsigned long)maxDt);
  }
  void cancelListing(){for(auto& directory:directories)directory.close();listing=false;listingDepth=0;}
  void publishListing(){
    auto& result=listingPage;
    std::snprintf(result.message,sizeof(result.message),"%s",!result.ok?"SD NOT AVAILABLE":result.total?"FILES (UP TO 3 DIRECTORY LEVELS)":"CARD EMPTY");
    if(pageMutex&&xSemaphoreTake(pageMutex,portMAX_DELAY)==pdTRUE){page=result;xSemaphoreGive(pageMutex);}
    Serial.printf("SD FILES: %s first=%u count=%u total=%u\n",result.ok?"OK":"FAILED",result.first,result.count,result.total);
    for(unsigned i=0;i<result.count;++i)Serial.printf("SD FILE %s %lu B\n",result.entries[i].name,(unsigned long)result.entries[i].bytes);
  }
  void list(const Job& job){
    cancelListing();listingPage=Sd::Page();listingPage.first=job.day;listingPage.generation=job.geometry;
    directories[0]=Sd::card().open("/",FILE_READ);listingPage.ok=directories[0]&&directories[0].isDirectory();
    listing=listingPage.ok;if(!listing)publishListing();
  }
  // One directory entry between queued writes keeps acquisition logging running.
  void listStep(){
    File entry=directories[listingDepth].openNextFile();
    if(!entry){directories[listingDepth].close();if(listingDepth){--listingDepth;return;}listing=false;publishListing();return;}
    if(entry.isDirectory()){
      if(listingDepth<3){directories[++listingDepth]=entry;return;}
    }else{
      auto& result=listingPage;
      if(result.total>=result.first&&result.count<Sd::Page::capacity){auto& item=result.entries[result.count++];std::snprintf(item.name,sizeof(item.name),"%s",entry.path());item.bytes=entry.size();}
      ++result.total;
    }
    entry.close();
  }
  void format(){
    cancelListing();close();ready=false;fault=0;
    formatOk=Sd::card().formatConfirmed();ready=formatOk;path[0]=0;
    formattedUsageValid=false;formattedUsedPercent=0;
    if(formatOk){const uint64_t total=Sd::card().totalBytes(),used=Sd::card().usedBytes();formattedUsageValid=total>0;if(total)formattedUsedPercent=uint8_t(std::min(uint64_t(100),used*100/total));}
    if(!formatOk)++errors;
    ++formatGeneration;formatting=false;
    Serial.printf("SD FORMAT: %s (all card files removed if successful)\n",formatOk?"OK":"FAILED");
  }
  void run() {
    Job job;
    for(;;) {
      const bool received=xQueueReceive(queue,&job,listing?0:pdMS_TO_TICKS(100))==pdTRUE;
      if(xSemaphoreTake(mutex,pdMS_TO_TICKS(20))!=pdTRUE){if(received)++dropped;continue;}
      const uint32_t started=micros();
      if(received) {
        if(job.kind==Start)openSession(job);
        else if(job.kind==Stop)close();
        else if(job.kind==Best)saveBest(job);
        else if(job.kind==Diagnostic)readback();
        else if(job.kind==ReferenceTest)referenceTest();
        else if(job.kind==Inspect)inspectFile();
        else if(job.kind==ListFiles)list(job);
        else if(job.kind==Format)format();
        else if(file && job.test==fileTest) {
          if(fileTest && fault==2)vTaskDelay(pdMS_TO_TICKS(200));
          const size_t length=std::strlen(job.text);
          if((fileTest && fault==1) || file.write((const uint8_t*)job.text,length)!=length){++errors;close();Serial.println("SD WRITE: FAILED; timing continues");}
          else ++written;
        }
      }
      if(listing)listStep();
      if(file && millis()-lastFlush>=500){file.flush();lastFlush=millis();}
      const uint32_t us=micros()-started;if(us>maxWriteUs)maxWriteUs=us;
      xSemaphoreGive(mutex);
      if(listing&&!received)vTaskDelay(1);
    }
  }
};
