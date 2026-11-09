#include "common.h"
#include <libtwl/rtos/rtosIrq.h>
#include <libtwl/sio/sioRtc.h>
#include <libtwl/timer/timer.h>
#include "RtcClock.h"
#include "RtcIpcService.h"

// ARM7 sound owns timers 0, 1 and 3; ARM9 timers belong to the guest.
static constexpr int RTC_TIMER = 2;
static volatile u32 sRtcTimerOverflows;

static void rtcTimerIrq(u32)
{
    ++sRtcTimerOverflows;
}

static u64 readRtcTicks()
{
    const u32 irqState = rtos_disableIrqs();
    u32 high = sRtcTimerOverflows;
    u16 low = tmr_getCounter(RTC_TIMER);
    if (rtos_getIrqFlags() & RTOS_IRQ_TIMER(RTC_TIMER))
    {
        // Include a wrap whose handler has not run, including one after low.
        ++high;
        low = tmr_getCounter(RTC_TIMER);
    }
    rtos_restoreIrqs(irqState);
    return (static_cast<u64>(high) << 16) | low;
}

void RtcIpcService::Start()
{
    rtc_init();
    tmr_stop(RTC_TIMER);
    tmr_configure(RTC_TIMER, TMCNT_H_CLK_SYS_DIV_1024, 0, true);
    sRtcTimerOverflows = 0;
    const u32 irq = RTOS_IRQ_TIMER(RTC_TIMER);
    rtos_disableIrqMask(irq);
    rtos_setIrqFunc(irq, rtcTimerIrq);
    rtos_ackIrqMask(irq);
    rtos_enableIrqMask(irq);
    tmr_start(RTC_TIMER);
    ThreadIpcService::Start();
}

void RtcIpcService::HandleMessage(u32 data)
{
    auto* sample = reinterpret_cast<RtcClockSample*>(data << 2);
    static_assert(sizeof(rtc_datetime_t) == sizeof(RtcDateTime));
    rtc_readDateTime(reinterpret_cast<rtc_datetime_t*>(&sample->dateTime));
    sample->padding = 0;
    sample->ticks = readRtcTicks();
    SendResponseMessage(1);
}
