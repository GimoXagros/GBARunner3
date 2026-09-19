#include <cstdint>
#include <iostream>
using u8=uint8_t; using u16=uint16_t; using u32=uint32_t;
using s16=int16_t; using s64=int64_t;
// Access only for executing the production conversion helpers, not replacing them.
#define private public
#include "Peripherals/RomGpio/RomGpioRtc.h"
#undef private
#define RTC_EWRAM
#include "production_calendar.h"
#include "date_vectors.h"

int main()
{
    RomGpioRtc rtc;
    unsigned failures=0, cases=0;
    auto check=[&](const char* name,u32 actual,u32 expected) {
        ++cases;
        if(actual!=expected) {
            if(failures<8) std::cout<<name<<" expected="<<expected<<" actual="<<actual<<'\n';
            ++failures;
        }
    };
    for(bool mode24:{false,true}) {
        for(u32 second=0;second<86400;++second) {
            u32 hour=second/3600;
            RomGpioRtc::rio_rtc_datetime_t dt{};
            dt.date.month=1; dt.date.monthDay=1;
            dt.time.hour=rtc.ToBcd(mode24?hour:hour%12)|(hour>=12?0x80:0);
            dt.time.minute=rtc.ToBcd(second/60%60);
            dt.time.second=rtc.ToBcd(second%60);
            check(mode24?"24h BCD":"12h BCD",rtc.ToSecondsSinceJanuary2000(dt,mode24),second);
            RomGpioRtc::rio_rtc_datetime_t output{};
            rtc.FromSecondsSinceJanuary2000(second,output,mode24);
            check("round-trip",rtc.ToSecondsSinceJanuary2000(output,mode24),second);
        }
    }
    for(const auto& v:dates) {
        RomGpioRtc::rio_rtc_datetime_t dt{};
        dt.date.year=rtc.ToBcd(v[0]); dt.date.month=rtc.ToBcd(v[1]); dt.date.monthDay=rtc.ToBcd(v[2]);
        check("Python datetime oracle",rtc.ToSecondsSinceJanuary2000(dt,true),v[3]);
        rtc.FromSecondsSinceJanuary2000(v[3],dt,true);
        check("year",dt.date.year,rtc.ToBcd(v[0]));
        check("month",dt.date.month,rtc.ToBcd(v[1]));
        check("day",dt.date.monthDay,rtc.ToBcd(v[2]));
    }
    for(u32 day=0;day<36525;++day) {
        RomGpioRtc::rio_rtc_datetime_t dt{}, next{};
        rtc.FromSecondsSinceJanuary2000(day*86400u,dt,true);
        check("daily rollover round-trip",rtc.ToSecondsSinceJanuary2000(dt,true),day*86400u);
        if(day<36524) {
            rtc.FromSecondsSinceJanuary2000((day+1)*86400u,next,true);
            check("weekday progression",next.date.weekDay,(dt.date.weekDay+1)%7);
        }
    }
    check("century wrap",rtc.NormalizeSecondsSinceJanuary2000(RtcPersistence::CYCLE_SECONDS),0);
    check("negative wrap",rtc.NormalizeSecondsSinceJanuary2000(-1),RtcPersistence::CYCLE_SECONDS-1);
    std::cout<<cases<<" cases, "<<failures<<" failures; hardware NOT RUN\n";
    return failures?1:0;
}
