#pragma once
#include <cstddef>
#include <cstring>

// No Arduino dependencies: bounded, nonblocking CR/LF adapter. Single-character
// Commands are dispatched only after Enter.
class TerminalReader {
 public:
  template<class Handler, class Error> void accept(char c, Handler handler, Error error) {
    if (c=='\r' || c=='\n') {
      if (overflow) error("line too long; entire line discarded");
      else if (length) { buffer[length]=0;handler(buffer); }
      length=0;overflow=false;return;
    }
    if (c==8 || c==127) { if(!overflow && length)--length;return; }
    if (c==27 && length==0) { char escape[]={27,0};handler(escape);return; }
    if (static_cast<unsigned char>(c)<32) return;
    if (overflow) return;
    if (length+1>=sizeof(buffer)) { overflow=true;return; }
    buffer[length++]=c;
  }
 private:
  char buffer[192] = {};
  size_t length=0;
  bool overflow=false;
};
