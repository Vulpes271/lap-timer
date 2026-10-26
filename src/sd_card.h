#pragma once
#include <SD.h>
#include <vfs_api.h>
#include <ff.h>
#include "sd_view.h"

namespace Sd {
// Own the SDFS object so formatting always addresses this card's actual drive.
class Card : public fs::SDFS {
 public:
  Card():SDFS(fs::FSImplPtr(new VFSImpl())){}
  bool begin(uint8_t pin,SPIClass& bus,uint32_t frequency,const char* path,uint8_t maxFiles,bool formatEmpty){
    cs=pin;spi=&bus;hz=frequency;mount=path;files=maxFiles;
    return SDFS::begin(pin,bus,frequency,path,maxFiles,formatEmpty);
  }
  bool formatConfirmed(){
    if(!spi)return false;
    if(_pdrv==0xFF&&!SDFS::begin(cs,*spi,hz,mount,files,true))return false;
    char drive[]={char('0'+_pdrv),':',0};FATFS* filesystem=nullptr;DWORD freeClusters=0;
    if(f_getfree(drive,&freeClusters,&filesystem)!=FR_OK||!filesystem)return false;
    auto* work=static_cast<BYTE*>(malloc(4096));if(!work)return false;
    const MKFS_PARM options={FM_FAT32,2,0,0,32768};
    const FRESULT result=f_mkfs(drive,&options,work,4096);free(work);
    const bool mounted=f_mount(filesystem,drive,1)==FR_OK;
    return result==FR_OK&&mounted;
  }
 private:
  uint8_t cs=13,files=5;SPIClass* spi=nullptr;uint32_t hz=1000000;const char* mount="/sd";
};
inline Card& card(){static Card instance;return instance;}
}
