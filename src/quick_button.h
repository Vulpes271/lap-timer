#pragma once
#include <Arduino.h>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

// Active-low hardware adapter. Logical QUICK handling stays in TerminalApp.
class PressDebouncer {
 public:
  void begin(bool pressed, uint32_t now) {
    candidate = stable = pressed;
    changedAt = now;
    armed = !pressed;  // A button held at startup must first be released.
  }
  bool update(bool pressed, uint32_t now) {
    if (pressed != candidate) { candidate = pressed; changedAt = now; }
    if (candidate == stable || uint32_t(now - changedAt) < 30) return false;
    stable = candidate;
    if (!stable) { armed = true; return false; }
    const bool emit = armed;
    armed = false;
    return emit;
  }
 private:
  bool candidate = false, stable = false, armed = true;
  uint32_t changedAt = 0;
};

// All contacts close to GND. GPIO34/35 require external 10k pull-ups to 3.3 V.
#include "panel_navigation.h"
class LevelDebouncer {
 public:
  void begin(bool value,uint32_t now){stable=candidate=value;changedAt=now;}
  bool update(bool value,uint32_t now){
    if(value!=candidate){candidate=value;changedAt=now;}
    if(candidate!=stable && uint32_t(now-changedAt)>=30){stable=candidate;return true;}
    return false;
  }
  bool value() const{return stable;}
 private:
  bool stable=false,candidate=false;
  uint32_t changedAt=0;
};
class PanelButtons {
 public:
  static constexpr uint8_t greenPin=32,redPin=5,leftPin=34,rightPin=35;
  struct Frame {PanelState state;uint32_t at;};
  std::atomic<uint32_t> dropped{0};
  PanelState initial;
  bool begin(){
    pinMode(greenPin,INPUT_PULLUP);pinMode(redPin,INPUT_PULLUP);
    pinMode(leftPin,INPUT);pinMode(rightPin,INPUT);
    initial=read();
    const bool levels[]={initial.green,initial.red,initial.left,initial.right};
    for(unsigned i=0;i<4;++i)debounce[i].begin(levels[i],millis());
    events=xQueueCreate(16,sizeof(Frame));
    if(!events)return false;
    if(xTaskCreatePinnedToCore(sample,"panel-buttons",2048,this,2,nullptr,0)!=pdPASS){
      vQueueDelete(events);events=nullptr;return false;
    }
    return true;
  }
  bool pop(Frame& frame){return events&&xQueueReceive(events,&frame,0)==pdTRUE;}
  PanelState live() const {return read();}
 private:
  QueueHandle_t events=nullptr;
  LevelDebouncer debounce[4];
  static PanelState read(){return {digitalRead(greenPin)==LOW,digitalRead(redPin)==LOW,
                                  digitalRead(leftPin)==LOW,digitalRead(rightPin)==LOW};}
  static void sample(void* pointer){
    auto& self=*static_cast<PanelButtons*>(pointer);TickType_t wake=xTaskGetTickCount();
    for(;;){
      const uint32_t now=millis();const auto state=read();
      const bool values[]={state.green,state.red,state.left,state.right};bool changed=false;
      for(unsigned i=0;i<4;++i)changed=self.debounce[i].update(values[i],now)||changed;
      if(changed){
        Frame frame{{self.debounce[0].value(),self.debounce[1].value(),self.debounce[2].value(),self.debounce[3].value()},now};
        if(xQueueSend(self.events,&frame,0)!=pdTRUE)++self.dropped;
      }
      vTaskDelayUntil(&wake,pdMS_TO_TICKS(5)?pdMS_TO_TICKS(5):1);
    }
  }
};
