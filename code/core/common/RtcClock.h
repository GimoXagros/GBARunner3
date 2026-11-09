#pragma once

#include <cstdint>
#include <stddef.h>

constexpr uint32_t RTC_CLOCK_HZ = 33513982;
constexpr uint32_t RTC_CLOCK_DIVIDER = 1024;
constexpr uint32_t RTC_PHASE_UNITS = 32768;

struct RtcDate
{
    uint8_t year, month, monthDay, weekDay;
};

struct RtcTime
{
    uint8_t hour, minute, second;
};

struct RtcDateTime
{
    RtcDate date;
    RtcTime time;
};

struct RtcClockSample
{
    RtcDateTime dateTime;
    uint8_t padding;
    uint64_t ticks;
};

static_assert(sizeof(RtcDateTime) == 7);
static_assert(sizeof(RtcClockSample) == 16);
static_assert(offsetof(RtcClockSample, ticks) == 8);
