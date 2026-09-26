#include "common.h"
#include "Cpsr.h"
#include "cp15.h"
#include "RtcFault.h"

// A boot/runtime stop screen independent of the save fault state and the VM.
[[gnu::section(".ewram")]] static void renderRtcFault(
    volatile unsigned short* pixels, RtcPersistence::LoadStatus status)
{
    static const unsigned char glyphs[26][5] = {
        {126,17,17,17,126}, {127,73,73,73,54}, {62,65,65,65,34},
        {127,65,65,34,28}, {127,73,73,73,65}, {127,9,9,9,1},
        {62,65,73,73,122}, {127,8,8,8,127}, {65,65,127,65,65},
        {32,64,65,63,1}, {127,8,20,34,65}, {127,64,64,64,64},
        {127,2,12,2,127}, {127,4,8,16,127}, {62,65,65,65,62},
        {127,9,9,9,6}, {62,65,81,33,94}, {127,9,25,41,70},
        {38,73,73,73,50}, {1,1,127,1,1}, {63,64,64,64,63},
        {31,32,64,32,31}, {127,32,24,32,127}, {99,20,8,20,99},
        {3,4,120,4,3}, {97,81,73,69,67}
    };
    const char* detail = "RTC FILE ERROR";
    const char* action = "INSPECT FILES ON PC";
    switch (status)
    {
        case RtcPersistence::LoadStatus::LegacyFound:
            detail = "OLD RTC FILE FOUND";
            action = "RUN RTC MIGRATE TOOL";
            break;
        case RtcPersistence::LoadStatus::Conflict:
            detail = "RTC FILES CONFLICT";
            action = "INSPECT BOTH FILE SETS";
            break;
        case RtcPersistence::LoadStatus::UnsupportedVersion:
            detail = "NEWER RTC FILE FOUND";
            break;
        case RtcPersistence::LoadStatus::IdentityMismatch:
            detail = "RTC GAME DOES NOT MATCH";
            break;
        case RtcPersistence::LoadStatus::Corrupt:
            detail = "RTC FILE IS DAMAGED";
            break;
        case RtcPersistence::LoadStatus::IoError:
        case RtcPersistence::LoadStatus::WriteError:
            detail = "RTC ACCESS FAILED";
            action = "CHECK SD CARD ON PC";
            break;
        default: break;
    }
    const char* lines[9] = {
        "RTC ERROR", "GAME STOPPED", detail, action,
        "KEEP ALL RTC FILES", "DO NOT DELETE OR RENAME",
        "NO AUTOMATIC RESET", "CHECK FILES BEFORE RESTART",
        "HOLD POWER TO TURN OFF"
    };
    for (unsigned i = 0; i < 256 * 192; ++i) pixels[i] = 0x8000;
    for (unsigned line = 0; line < 9; ++line)
    {
        const unsigned scale = line == 0 ? 2 : 1;
        const unsigned y = line == 0 ? 16 : 40 + line * 16;
        unsigned x = 12;
        for (const char* c = lines[line]; *c; ++c, x += 6 * scale)
        {
            if (*c < 'A' || *c > 'Z') continue;
            for (unsigned col = 0; col < 5; ++col)
                for (unsigned row = 0; row < 7; ++row)
                    if (glyphs[*c - 'A'][col] & (1u << row))
                        for (unsigned dy = 0; dy < scale; ++dy)
                            for (unsigned dx = 0; dx < scale; ++dx)
                                pixels[(y + row * scale + dy) * 256 + x + col * scale + dx] = 0xFFFF;
        }
    }
}

[[gnu::noreturn, gnu::section(".ewram")]] void rtc_persistenceFault(
    RtcPersistence::LoadStatus status)
{
    arm_disableIrqs();
    *(vu32*)0x04000208 = 0; // IME
    for (u32 channel = 0; channel < 4; ++channel)
        *(vu32*)(0x040000B8 + channel * 12) = 0;
    *(vu32*)0x04000064 = 0; // display capture
    *(vu32*)0x04000000 = 1u << 7;
    *(vu8*)0x04000241 = 0x80; // VRAM B -> LCDC
    auto* pixels = reinterpret_cast<volatile unsigned short*>(0x06820000);
    renderRtcFault(pixels, status);
    dc_flushRange((const void*)pixels, 256 * 192 * 2);
    dc_drainWriteBuffer();
    *(vu16*)0x04000304 |= 0x8003;
    *(vu16*)0x0400006C = 0;
    *(vu32*)0x04001000 = 1u << 7;
    *(vu32*)0x04000000 = (2u << 16) | (1u << 18);
    for (;;) asm volatile("nop" ::: "memory");
}
