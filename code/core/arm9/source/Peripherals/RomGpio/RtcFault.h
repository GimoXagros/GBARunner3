#pragma once

#include "RtcPersistence.h"

[[gnu::noreturn]] void rtc_persistenceFault(
    RtcPersistence::LoadStatus status = RtcPersistence::LoadStatus::WriteError);
