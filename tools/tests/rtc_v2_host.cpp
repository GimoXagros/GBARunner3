#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <initializer_list>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

using u8=uint8_t; using u16=uint16_t; using u32=uint32_t;
using s16=int16_t; using s64=int64_t; using UINT=unsigned;
#define private public
#include "Peripherals/RomGpio/RomGpioRtc.h"
#include "Peripherals/RomGpio/RomGpio.h"
#undef private
#include "Peripherals/RomGpio/RtcFault.h"

using RtcPersistence::LoadStatus;
using RtcPersistence::StateFileV2;
constexpr int FR_OK=0, FR_DISK_ERR=1, FR_NO_FILE=4, FR_NO_PATH=5, FR_EXIST=8;
constexpr int FA_OPEN_EXISTING=0, FA_READ=1, FA_WRITE=2;
constexpr int FA_CREATE_NEW=4, FA_CREATE_ALWAYS=8;
using FRESULT=int;
struct FIL { char path[64]; size_t position; bool live; int mode; };

#include "rtc_v2_fixtures.h"

std::map<std::string,std::vector<u8>> files;
std::string failure;
unsigned opens=0, writes=0, closes=0, renames=0, unlinks=0, schedules=0;
int liveHandles=0;
u32 hostSeconds=FIXTURE_HOST;
bool wrote=false;
bool forceLegacyOverwrite=false, forceBackupDelete=false;

template<size_t N> std::vector<u8> bytes(const u8 (&data)[N]) { return {data,data+N}; }

bool hit(const char* operation) { return failure == operation; }
int f_open(FIL* file,const char* path,int mode)
{
    ++opens;
    if (liveHandles != 0 || (mode & FA_WRITE && hit("open_write")) ||
        (!(mode & FA_WRITE) && hit("open_read"))) return FR_DISK_ERR;
    if (mode & FA_CREATE_NEW)
    {
        if (files.count(path)) return FR_EXIST;
        files[path]={};
    }
    else if (mode & FA_CREATE_ALWAYS) files[path]={};
    else if (!files.count(path)) return FR_NO_FILE;
    std::strncpy(file->path,path,sizeof(file->path)-1);
    file->path[sizeof(file->path)-1]=0;
    file->position=0; file->mode=mode; file->live=true;
    ++liveHandles;
    return FR_OK;
}
u32 f_size(FIL* file) { return static_cast<u32>(files.at(file->path).size()); }
int f_read(FIL* file,void* output,UINT requested,UINT* count)
{
    *count=0;
    if (hit("read") || (wrote && hit("readback"))) return FR_DISK_ERR;
    const auto& data=files.at(file->path);
    if (file->position>data.size()) return FR_DISK_ERR;
    size_t available=data.size()-file->position;
    size_t amount=std::min<size_t>(requested,available);
    if (hit("short_read") && amount) --amount;
    if (amount) std::memcpy(output,data.data()+file->position,amount);
    file->position+=amount; *count=static_cast<UINT>(amount);
    return FR_OK;
}
int f_write(FIL* file,const void* input,UINT requested,UINT* count)
{
    ++writes; *count=0;
    if (hit("write")) return FR_DISK_ERR;
    size_t amount=requested;
    if (hit("short_write") && amount) --amount;
    auto& data=files.at(file->path);
    if (data.size()<file->position+amount) data.resize(file->position+amount);
    if (amount) std::memcpy(data.data()+file->position,input,amount);
    file->position+=amount; *count=static_cast<UINT>(amount);
    wrote=true;
    if(forceLegacyOverwrite && std::string(file->path)=="modern1" && files.count("legacy2"))
        files["legacy2"][0]^=1;
    return FR_OK;
}
int f_sync(FIL*) { return hit("sync") ? FR_DISK_ERR : FR_OK; }
int f_close(FIL* file)
{
    ++closes;
    if ((file->mode & FA_WRITE && hit("close_write")) ||
        (!(file->mode & FA_WRITE) && hit("close_read"))) return FR_DISK_ERR;
    file->live=false; --liveHandles;
    return FR_OK;
}
int f_rename(const char*,const char*) { ++renames; return FR_DISK_ERR; }
int f_unlink(const char* path)
{
    ++unlinks;
    if(forceBackupDelete && std::string(path)=="legacy2")
    { files.erase(path); return FR_OK; }
    return FR_DISK_ERR;
}
void sav_requestFileWrite() { ++schedules; }
u8 mem_swapByte(u8 value,u8* target) { const u8 old=*target; *target=value; return old; }
[[noreturn]] void rtc_persistenceFault(LoadStatus) { throw std::runtime_error("RTC terminal write fault"); }

RomGpio gRomGpio;
static RomGpioRtc sRomGpioRtc;
extern "C" bool rtc_runOnWorkStack();
#include "production_rtc.h"
// Host extraction keeps the production RomGpio bridge but cannot execute the
// target assembly stack switch. The linked ARM test executes that wrapper.
extern "C" bool rtc_runOnWorkStack() { return rtc_flushOnWorkStackBody(); }
void RomGpioRtc::UpdateDSDateTime() { FromSecondsSinceJanuary2000(hostSeconds,sDSRtcDateTime,true); }

constexpr RtcPersistence::Identity ID_A{0x45455042,32u*1024*1024,0x12345678};
constexpr RtcPersistence::Identity ID_B{0x45565841,16u*1024*1024,0x87654321};
unsigned checks=0, failures=0;
void check(const char* name,bool condition)
{
    ++checks;
    if(!condition) { ++failures; std::cout<<"FAIL "<<name<<'\n'; }
}
void rebootPreservingMedia()
{
    failure.clear(); opens=writes=closes=renames=unlinks=schedules=0;
    // An explicit simulated process restart is the only place that releases
    // a FIL left live by a failed close.
    liveHandles=0; sRtcFileHandleLive=false; sRtcStateFile={};
    hostSeconds=FIXTURE_HOST; wrote=false;
}
void reset()
{
    files.clear(); rebootPreservingMedia();
}
void put(const char* path,const std::vector<u8>& data) { files[path]=data; }
LoadStatus load(RomGpioRtc& rtc,const RtcPersistence::Identity& id=ID_A)
{
    return rtc.Initialize("legacy0","legacy1","legacy2",
                          "modern0","modern1","modern2",id);
}
struct Bus
{
    RomGpioRtc rtc;
    RomGpio gpio;
    rio_registers_t registers{};
    Bus() { gpio.Initialize(&registers); gpio.WriteControlRegister(1); }
    void pins(unsigned value) { gpio.WriteDataRegister(value); rtc.Update(gpio); }
    void command(unsigned value)
    {
        gpio.WriteDirectionRegister(7); pins(0);
        for(int bit=7;bit>=0;--bit)
        { const unsigned data=((value>>bit)&1)*2; pins(4|data); pins(5|data); }
    }
    void byte(u8 value)
    {
        for(unsigned bit=0;bit<8;++bit)
        { const unsigned data=((value>>bit)&1)*2; pins(4|data); pins(5|data); }
    }
    void write(unsigned op,std::initializer_list<u8> data)
    { command(op); for(u8 value:data) byte(value); pins(0); }
    std::vector<u8> read(unsigned op,unsigned count)
    {
        command(op); gpio.WriteDirectionRegister(5); std::vector<u8> result(count);
        for(unsigned bit=0;bit<count*8;++bit)
        { pins(4); result[bit/8]|=gpio.GetPinState(1)<<(bit%8); pins(5); }
        pins(0); return result;
    }
};
void verify_no_rename_unlink() { check("journal uses zero rename/unlink calls",renames==0 && unlinks==0); }

int main(int argc,char** argv)
{
    for(int argument=1;argument+1<argc;++argument)
        if(std::string(argv[argument])=="--negative-control")
        {
            forceLegacyOverwrite=std::string(argv[argument+1])=="legacy-overwrite";
            forceBackupDelete=std::string(argv[argument+1])=="backup-delete";
        }

    for(bool mode24:{false,true})
    {
        reset(); hostSeconds=100; Bus bus;
        bus.write(0x62,{static_cast<u8>(mode24?0x40:0)});
        check("GPIO status read/write",bus.read(0x63,1)==
              std::vector<u8>({static_cast<u8>(mode24?0x40:0)}));
        bus.rtc._stateDirty=false; gRomGpioRtcStateDirty=0; schedules=0;
        const u8 hour=mode24?0x23:0x91;
        const u8 readHour=mode24?0xA3:0x91;
        bus.write(0x64,{0x24,0x02,0x28,3,hour,0x59,0x59});
        check("GPIO date/time round trip",bus.read(0x65,7)==
              std::vector<u8>({0x24,0x02,0x28,3,readHour,0x59,0x59}));
        check("GPIO write persists offset",bus.rtc._stateDirty &&
              gRomGpioRtcStateDirty && schedules>0);
        ++hostSeconds;
        check("GPIO leap-day rollover",bus.read(0x65,7)==
              std::vector<u8>({0x24,0x02,0x29,4,0,0,0}));
        bus.write(0x66,{static_cast<u8>(mode24?0x20:0x88),0x45,0x50});
        const auto time=bus.read(0x67,3);
        check("GPIO time-only write",time==
              std::vector<u8>({static_cast<u8>(mode24?0xA0:0x88),0x45,0x50}));
        const auto fullDate=bus.read(0x65,7);
        const auto offset=bus.rtc._rtcOffset;
        bus.command(0x64); bus.byte(0x99); bus.byte(0x12); bus.pins(0);
        check("incomplete command does not commit offset",
              bus.rtc._rtcOffset==offset && bus.read(0x65,7)==fullDate);
        bus.command(0x70); bus.pins(0);
        check("invalid command leaves time unchanged",bus.read(0x67,3)==time);
        bus.write(0x60,{});
        check("GPIO reset",bus.read(0x65,7)==
              std::vector<u8>({0,1,1,0,0,0,0}));
    }

    reset();
    put("legacy0",bytes(V1_PRIMARY)); put("legacy1",bytes(V1_TEMP));
    put("legacy2",bytes(V1_BACKUP));
    const auto originals=files;
    RomGpioRtc legacy;
    const auto legacyStatus=load(legacy);
    check("legacy-only blocks before guest",legacyStatus==LoadStatus::LegacyFound);
    check("legacy-only performs no write",files==originals && writes==0);
    verify_no_rename_unlink();
    const auto originalLegacy=bytes(V1_PRIMARY);
    for(unsigned byte=0;byte<originalLegacy.size();++byte)
        for(unsigned bit=0;bit<8;++bit)
        {
            reset(); put("legacy0",originalLegacy); put("legacy2",bytes(V1_BACKUP));
            files["legacy0"][byte]^=1u<<bit;
            const auto damaged=files.at("legacy0"), backup=files.at("legacy2");
            RomGpioRtc bitflip;
            const auto status=load(bitflip);
            const auto expected=(byte==4 || byte==5)
                ? LoadStatus::UnsupportedVersion : LoadStatus::Corrupt;
            check("v1 bitflip blocks without altering intact backup",
                  status==expected && files.at("legacy0")==damaged &&
                  files.at("legacy2")==backup && writes==0);
        }
    for(unsigned length=0;length<originalLegacy.size();++length)
    {
        reset(); put("legacy0",originalLegacy); put("legacy1",bytes(V1_TEMP));
        files["legacy1"].resize(length);
        const auto primary=files.at("legacy0"), truncated=files.at("legacy1");
        RomGpioRtc cut;
        check("v1 truncated sibling blocks without touching primary",
              load(cut)==LoadStatus::Corrupt && files.at("legacy0")==primary &&
              files.at("legacy1")==truncated && writes==0);
    }

    reset();
    check("production RomGpio wrapper returns fresh gate status",
          gRomGpio.LoadRtcState("legacy0","legacy1","legacy2",
                                "modern0","modern1","modern2",ID_A)==
          LoadStatus::FreshInitialized && files.empty() && writes==0);

    reset();
    put("legacy0",bytes(V1_PRIMARY)); put("legacy1",bytes(V1_TEMP));
    put("legacy2",bytes(V1_BACKUP)); put("modern0",bytes(V2_PENDING));
    const auto preserved0=files.at("legacy0"), preserved1=files.at("legacy1"),
               preserved2=files.at("legacy2"), pending=files.at("modern0");
    Bus adopted;
    check("pending consent anchors before guest",load(adopted.rtc)==LoadStatus::Ready &&
          files.count("modern1") && adopted.rtc._currentRecord.phase==RtcPersistence::PHASE_READY &&
          adopted.rtc._currentRecord.hostSecondsSince2000==FIXTURE_HOST);
    check("pending original and v1 triad unchanged",files.at("modern0")==pending &&
          files.at("legacy0")==preserved0 && files.at("legacy1")==preserved1 &&
          files.count("legacy2") && files.at("legacy2")==preserved2);
    check("adopted GPIO exposes stored snapshot",adopted.read(0x65,7)==
          std::vector<u8>({0x24,0x02,0x28,0,0x01,0x39,0x39}));
    const unsigned committedWrites=writes;
    Bus restarted;
    check("recreated object reloads ready without migrating twice",
          load(restarted.rtc)==LoadStatus::Ready && writes==committedWrites);
    ++hostSeconds;
    check("DS anchor advances adopted GPIO second",restarted.read(0x67,3)==
          std::vector<u8>({0x01,0x39,0x40}));
    verify_no_rename_unlink();

    reset();
    Bus fresh;
    check("all missing defaults in memory without boot write",
          load(fresh.rtc)==LoadStatus::FreshInitialized && files.empty() && writes==0);
    fresh.write(0x64,{0x24,0x02,0x28,3,0x22,0x45,0x45});
    check("GPIO date command marks dirty",fresh.rtc._stateDirty && schedules>0);
    check("fresh flush writes and validates",fresh.rtc.FlushStateIfDirty() &&
          files.count("modern0") && files.at("modern0").size()==200);
    check("verified flush clears dirty",!fresh.rtc._stateDirty);
    check("new record has fresh policy and ready phase",
          fresh.rtc._currentRecord.policy==RtcPersistence::POLICY_FRESH &&
          fresh.rtc._currentRecord.phase==RtcPersistence::PHASE_READY);
    const auto freshBytes=files.at("modern0");
    Bus freshReload;
    check("write close recreate reload",load(freshReload.rtc)==LoadStatus::Ready &&
          files.at("modern0")==freshBytes);
    check("reloaded GPIO-visible date/time",freshReload.read(0x65,7)==
          std::vector<u8>({0x24,0x02,0x28,3,0xA2,0x45,0x45}));
    verify_no_rename_unlink();

    reset(); put("modern0",bytes(V2_FRESH));
    auto corrupt=files.at("modern0"); corrupt[100]^=1; files["modern0"]=corrupt;
    put("modern2",bytes(V2_FRESH)); RomGpioRtc recovered;
    check("corrupt primary may use valid ready backup",load(recovered)==LoadStatus::Ready &&
          recovered._currentRecord.sequence==7 && files.at("modern0")==corrupt);
    const auto originalCurrent=bytes(V2_FRESH);
    for(unsigned byte=0;byte<originalCurrent.size();++byte)
        for(unsigned bit=0;bit<8;++bit)
        {
            reset(); put("modern0",originalCurrent); put("modern2",originalCurrent);
            files["modern0"][byte]^=1u<<bit;
            RomGpioRtc bitflip;
            const auto status=load(bitflip);
            const auto expected=(byte==4 || byte==5)
                ? LoadStatus::UnsupportedVersion : LoadStatus::Ready;
            check("bitflip classifies unknown version or uses intact backup",
                  status==expected && files.at("modern2")==originalCurrent);
        }
    for(unsigned length=0;length<originalCurrent.size();++length)
    {
        reset(); put("modern0",originalCurrent); put("modern1",originalCurrent);
        files["modern1"].resize(length);
        RomGpioRtc truncated;
        check("truncated temp preserves valid primary",
              load(truncated)==LoadStatus::Ready &&
              files.at("modern0")==originalCurrent);
    }
    reset(); put("modern0",bytes(V2_FRESH)); put("modern1",bytes(V2_FUTURE));
    RomGpioRtc future;
    check("unknown modern version blocks",load(future)==LoadStatus::UnsupportedVersion);
    reset(); put("modern0",bytes(V2_FRESH));
    RomGpioRtc foreign;
    check("ROM identity mismatch blocks",load(foreign,ID_B)==LoadStatus::IdentityMismatch);
    reset(); put("modern0",bytes(V2_PENDING)); put("modern1",bytes(V2_FRESH));
    RomGpioRtc mixed;
    check("different lineage blocks",load(mixed)==LoadStatus::Conflict);
    reset(); put("modern0",bytes(V2_FRESH)); put("modern1",bytes(V2_FRESH));
    RomGpioRtc identicalTie;
    check("equal identical sequence uses path order",load(identicalTie)==LoadStatus::Ready);
    reset(); put("modern0",bytes(V2_FRESH)); put("modern1",bytes(V2_EQUAL_DIFFERENT));
    RomGpioRtc unequalTie;
    check("equal differing sequence conflicts",load(unequalTie)==LoadStatus::Conflict);
    reset(); put("modern0",bytes(V2_OLD_WRAP)); put("modern1",bytes(V2_WRAP));
    RomGpioRtc wrapped;
    check("modular sequence wrap chooses zero",load(wrapped)==LoadStatus::Ready &&
          wrapped._currentRecord.sequence==0);
    reset(); put("modern0",bytes(V2_WRAP)); put("modern1",bytes(V2_HALF_CYCLE));
    RomGpioRtc half;
    check("half cycle sequence conflicts",load(half)==LoadStatus::Conflict);
    reset(); put("modern0",bytes(V2_WRAP)); put("modern1",bytes(V2_CYCLE_MID));
    put("modern2",bytes(V2_CYCLE_LAST)); RomGpioRtc cycle;
    check("cyclic three slot order conflicts",load(cycle)==LoadStatus::Conflict);
    verify_no_rename_unlink();

    for(const char* stage:{"open_read","read","short_read","close_read",
                           "open_write","write","short_write","sync","close_write","readback"})
    {
        reset(); put("modern0",bytes(V2_FRESH));
        Bus candidate;
        check("failure setup loads selected record",load(candidate.rtc)==LoadStatus::Ready);
        candidate.write(0x62,{0x40});
        const auto selected=files.at("modern0");
        failure=stage;
        bool faulted=false;
        try { candidate.rtc.FlushStateIfDirty(); }
        catch(const std::runtime_error&) { faulted=true; }
        check("injected operation latches terminal failure",faulted &&
              candidate.rtc._stateDirty && candidate.rtc._writeError);
        check("failed write preserves selected copy",files.at("modern0")==selected);
        const auto operationCount=opens+writes+closes;
        bool secondFault=false;
        try { candidate.rtc.FlushStateIfDirty(); }
        catch(const std::runtime_error&) { secondFault=true; }
        check("latched failure prevents automatic retry",secondFault &&
              opens+writes+closes==operationCount);
        if(liveHandles)
        {
            RomGpioRtc recreated;
            const auto beforeRecreate=opens+writes+closes;
            check("failed close blocks object recreation without reopening FIL",
                  load(recreated)==LoadStatus::IoError &&
                  opens+writes+closes==beforeRecreate);
        }
        verify_no_rename_unlink();

        // A process restart releases host handles but preserves media bytes.
        // A fully written new record may win even if sync/close/readback failed;
        // a partial record cannot displace the prior selected copy.
        rebootPreservingMedia();
        RomGpioRtc afterReboot;
        const bool completed=std::string(stage)=="sync" ||
            std::string(stage)=="close_write" || std::string(stage)=="readback";
        check("failed-write reboot selects only a complete valid record",
              load(afterReboot)==LoadStatus::Ready &&
              afterReboot._currentRecord.sequence==(completed?8u:7u) && writes==0 &&
              files.at("modern0")==selected);
        verify_no_rename_unlink();
    }

    for(const char* stage:{"open_write","write","short_write","sync",
                           "close_write","readback"})
    {
        reset();
        put("legacy0",bytes(V1_PRIMARY)); put("legacy1",bytes(V1_TEMP));
        put("legacy2",bytes(V1_BACKUP)); put("modern0",bytes(V2_PENDING));
        const auto beforeLegacy=files;
        failure=stage;
        RomGpioRtc interrupted;
        check("pending failure blocks guest execution",load(interrupted)==LoadStatus::WriteError);
        check("pending failure preserves all v1 bytes and consent",
              files.at("legacy0")==beforeLegacy.at("legacy0") &&
              files.at("legacy1")==beforeLegacy.at("legacy1") &&
              files.at("legacy2")==beforeLegacy.at("legacy2") &&
              files.at("modern0")==beforeLegacy.at("modern0"));
        rebootPreservingMedia();
        RomGpioRtc resumed;
        const std::string stageName=stage;
        if(stageName=="write" || stageName=="short_write")
            check("partial pending restart blocks instead of re-migrating",
                  load(resumed)==LoadStatus::Corrupt && writes==0 &&
                  files.at("modern0")==beforeLegacy.at("modern0"));
        else
        {
            const bool wasComplete=stageName!="open_write";
            check("pending restart reaches one verified ready record",
                  load(resumed)==LoadStatus::Ready &&
                  resumed._currentRecord.phase==RtcPersistence::PHASE_READY &&
                  resumed._currentRecord.sequence==1 &&
                  writes==(wasComplete?0u:1u));
            const unsigned firstReadyWrites=writes;
            rebootPreservingMedia();
            RomGpioRtc again;
            check("ready restart does not migrate twice",
                  load(again)==LoadStatus::Ready && writes==0 &&
                  firstReadyWrites==(wasComplete?0u:1u));
        }
        verify_no_rename_unlink();
    }

    std::cout<<checks<<" RTC v2 host cases, "<<failures
             <<" failures; synthetic FatFs/clock, hardware NOT RUN\n";
    return failures?1:0;
}
