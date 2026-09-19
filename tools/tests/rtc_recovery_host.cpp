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
#include "Peripherals/RomGpio/RomGpio.h"
#undef private
#define RTC_EWRAM
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
u8 mem_swapByte(u8 value,u8* target) { const u8 old=*target; *target=value; return old; }
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
struct Bus {
    RomGpioRtc rtc;
    RomGpio gpio;
    rio_registers_t regs{};
    Bus() { gpio.Initialize(&regs); gpio.WriteControlRegister(1); }
    void pins(unsigned value) { gpio.WriteDataRegister(value); rtc.Update(gpio); }
    void command(unsigned value) {
        gpio.WriteDirectionRegister(7); pins(0);
        for(int bit=7;bit>=0;--bit) { const unsigned data=((value>>bit)&1)*2; pins(4|data); pins(5|data); }
    }
    void byte(u8 value) {
        for(unsigned bit=0;bit<8;++bit) { const unsigned data=((value>>bit)&1)*2; pins(4|data); pins(5|data); }
    }
    void write(unsigned op,std::initializer_list<u8> data) { command(op); for(u8 b:data) byte(b); pins(0); }
    std::vector<u8> read(unsigned op,unsigned count) {
        command(op); gpio.WriteDirectionRegister(5); std::vector<u8> out(count);
        for(unsigned i=0;i<count*8;++i) { pins(4); out[i/8]|=gpio.GetPinState(1)<<(i%8); pins(5); }
        pins(0); return out;
    }
};
int main() {
    for(bool mode24:{false,true}) {
        reset(); Bus bus;
        bus.write(0x62,{static_cast<u8>(mode24?0x40:0)});
        check("GPIO status read/write",bus.read(0x63,1)==std::vector<u8>{static_cast<u8>(mode24?0x40:0)});
        bus.rtc._stateDirty=false; gRomGpioRtcStateDirty=0; schedules=0;
        const u8 hour=mode24?0x23:0x91;
        const u8 readHour=mode24?0xa3:0x91;
        bus.write(0x64,{0x24,0x02,0x28,3,hour,0x59,0x59});
        check("GPIO date/time round trip",bus.read(0x65,7)==std::vector<u8>({0x24,0x02,0x28,3,readHour,0x59,0x59}));
        check("GPIO write persists offset",bus.rtc._stateDirty && gRomGpioRtcStateDirty && schedules>0);
        ++hostSeconds;
        check("GPIO leap-day rollover",bus.read(0x65,7)==std::vector<u8>({0x24,0x02,0x29,4,0,0,0}));
        bus.write(0x66,{static_cast<u8>(mode24?0x20:0x88),0x45,0x50});
        const auto time=bus.read(0x67,3);
        check("GPIO time-only write",time==std::vector<u8>({static_cast<u8>(mode24?0xa0:0x88),0x45,0x50}));
        const auto fullDate=bus.read(0x65,7);
        const auto offset=bus.rtc._rtcOffset;
        bus.command(0x64); bus.byte(0x99); bus.byte(0x12); bus.pins(0);
        check("incomplete command does not commit offset",bus.rtc._rtcOffset==offset && bus.read(0x65,7)==fullDate);
        bus.command(0x70); bus.pins(0);
        check("invalid command leaves time unchanged",bus.read(0x67,3)==time);
        bus.write(0x60,{});
        check("GPIO reset",bus.read(0x65,7)==std::vector<u8>({0,1,1,0,0,0,0}));
    }
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
    std::cout<<checks<<" RTC GPIO/recovery cases, "<<failures<<" failures; actual GPIO state machine, fake read-only FatFs/host clock, no physical write/durability claim\n";
    return failures?1:0;
}
