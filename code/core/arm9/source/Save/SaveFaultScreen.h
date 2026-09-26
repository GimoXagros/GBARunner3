#pragma once

// Tiny embedded 5x7 uppercase font: no allocator, ROM, filesystem or GUI state.
[[gnu::always_inline]] inline void sav_renderPersistenceFaultScreen(volatile unsigned short* pixels)
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
    static const char* const lines[] = {
        "SAVE ERROR", "GAME STOPPED", "SAVE ACCESS FAILED",
        "DATA MAY BE MISSING", "NO AUTOMATIC RETRY",
        "KEEP THE SD CARD INSERTED", "UNSAVED DATA WILL BE LOST",
        "HOLD POWER TO TURN OFF", "RESTORE A BACKUP IF NEEDED"
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
