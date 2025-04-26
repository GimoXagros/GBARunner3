#pragma once

#define COLOR_LUT_SIZE      (1 << 15)

struct ColorProfile {
    int matrix[3][3]; // coeficients multiplied by 1000
    int luminance;    // multiplier *100, ie. 93
};

void clut_initColorCorrection(const ColorProfile* preset);
void clut_disableColorCorrection();
