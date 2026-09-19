#include <cstdint>
#include <cstring>
#include <iostream>
#include <map>
#include <string>
#include <vector>
using u8=uint8_t; using u16=uint16_t; using u32=uint32_t;
using s16=int16_t; using s64=int64_t; using UINT=unsigned;
#define private public
#include "Peripherals/RomGpio/RomGpioRtc.h"
#undef private
#define RTC_EWRAM
constexpr unsigned RIO_RTC_STATUS_24H=0x40, RIO_RTC_STATUS_POWER=0x80,
    RIO_RTC_STATUS_WRITE_MASK=0x6a;
constexpr int FR_OK=0, FA_OPEN_EXISTING=0, FA_READ=1;
struct FIL { unsigned reserved; };
static FIL sRtcStateFile;
static RtcPersistence::StateFile sRtcStateFileBuffer;
volatile u8 gRomGpioRtcStateDirty;
RomGpioRtc::rio_rtc_datetime_t RomGpioRtc::sDSRtcDateTime;
std::map<std::string,std::vector<u8>> files;
std::string current, readFailure;
u32 hostSeconds;
unsigned schedules, checks, failures;
int f_open(FIL*,const char* path,int) { current=path; return files.count(current)?0:1; }
unsigned f_size(FIL*) { return files.at(current).size(); }
int f_read(FIL*,void* out,UINT size,UINT* read) {
    *read=0;
    if(current==readFailure) return 1;
    if(size>files.at(current).size()) return 1;
    std::memcpy(out,files.at(current).data(),size); *read=size; return 0;
}
int f_close(FIL*) { return 0; } // close/physical failure policy is a separate gate
void sav_requestFileWrite() { ++schedules; }
void RomGpioRtc::UpdateDSDateTime() { FromSecondsSinceJanuary2000(hostSeconds,sDSRtcDateTime,true); }
#include "production_recovery.h"
constexpr RtcPersistence::Identity ID_A{0x11111111,0x2000000,0x12345678};
constexpr RtcPersistence::Identity ID_B{0x22222222,0x1000000,0x87654321};
void check(const char* name,bool ok) { ++checks; if(!ok) { ++failures; std::cout<<"FAIL "<<name<<'\n'; } }
void reset() { files.clear(); current.clear(); readFailure.clear(); hostSeconds=100; schedules=0; }
void put(const char* path,const RtcPersistence::Identity& id,u32 seq,u32 game=500,u32 host=100) {
    auto state=RtcPersistence::CreateState(id,seq,host,game,-2,0x40,0x1234);
    const auto* p=reinterpret_cast<const u8*>(&state); files[path]={p,p+sizeof(state)};
}
void load(RomGpioRtc& rtc,const RtcPersistence::Identity& id=ID_A) { rtc.Initialize("primary","temp","backup",id); }
int main() {
    reset(); RomGpioRtc empty; load(empty); check("missing state",empty._sequence==0 && !empty._stateDirty);
    reset(); put("primary",ID_A,7); RomGpioRtc normal; load(normal);
    check("primary",normal._sequence==7 && normal._rtcOffset==400 && !normal._stateDirty && schedules==0);
    check("metadata",normal._weekDayOffset==-2 && normal._statusRegister==0x40 && normal._intRegister==0x1234);
    const auto original=files.at("primary");
    for(unsigned byte=0;byte<original.size();++byte) for(unsigned bit=0;bit<8;++bit) {
        reset(); files["primary"]=original; files["primary"][byte]^=1u<<bit; put("backup",ID_A,6,600);
        RomGpioRtc rtc; load(rtc); check("corrupt primary uses backup",rtc._sequence==6 && rtc._rtcOffset==500 && rtc._stateDirty && schedules==1);
    }
    for(unsigned length=0;length<original.size();++length) {
        reset(); put("primary",ID_A,7); put("temp",ID_A,8,900); files["temp"].resize(length);
        RomGpioRtc rtc; load(rtc); check("interrupted temp ignored",rtc._sequence==7 && !rtc._stateDirty);
    }
    reset(); put("primary",ID_A,7); put("temp",ID_A,8,900); put("backup",ID_A,6);
    RomGpioRtc temp; load(temp); check("complete newer temp recovered",temp._sequence==8 && temp._rtcOffset==800 && temp._stateDirty);
    reset(); put("primary",ID_A,UINT32_MAX); put("temp",ID_A,0,900);
    RomGpioRtc wrap; load(wrap); check("sequence wrap",wrap._sequence==0 && wrap._rtcOffset==800);
    reset(); put("primary",ID_A,7); put("backup",ID_A,7,900);
    RomGpioRtc tie; load(tie); check("equal sequence prefers primary",tie._rtcOffset==400 && !tie._stateDirty);
    reset(); put("primary",ID_B,20); put("backup",ID_A,6,600);
    RomGpioRtc identity; load(identity); check("foreign identity rejected",identity._sequence==6 && identity._rtcOffset==500);
    reset(); put("primary",ID_A,7); put("backup",ID_A,6,600); readFailure="primary";
    RomGpioRtc error; load(error); check("read error uses backup",error._sequence==6);
    reset(); put("primary",ID_A,7); hostSeconds=160;
    RomGpioRtc forward; load(forward); check("forward host",forward._rtcOffset+hostSeconds==560 && !forward._stateDirty);
    hostSeconds=90; RomGpioRtc backward; load(backward);
    check("backward host freezes elapsed",backward._rtcOffset+hostSeconds==500 && backward._stateDirty);
    reset(); put("primary",ID_A,7,500); RomGpioRtc a; load(a);
    put("primary",ID_B,2,900); RomGpioRtc b; load(b,ID_B);
    check("per-ROM offsets independent",a._rtcOffset==400 && b._rtcOffset==800);
    check("future sequence comparison",!RtcPersistence::IsSequenceNewer(7,8));
    std::cout<<checks<<" RTC recovery selection cases, "<<failures<<" failures; fake read-only FatFs/clock, no GPIO/write/durability claim\n";
    return failures?1:0;
}
