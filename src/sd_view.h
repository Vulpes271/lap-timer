#pragma once
#include <cstdint>
namespace Sd {
inline bool formatAllowed(bool test,bool logging,bool active,bool moving,bool alarm,bool busy){return !test&&!logging&&!active&&!moving&&!alarm&&!busy;}
struct Entry {char name[128]="";uint32_t bytes=0;};
struct Page {
  static constexpr unsigned capacity=5;
  Entry entries[capacity];unsigned first=0,count=0,total=0;uint32_t generation=0;
  bool ok=false;char message[80]="";
};
}
