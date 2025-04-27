#pragma once
#include "ColorProfiles.h"

#define COLOR_LUT_SIZE      (1 << 15)

void clut_initColorCorrection(const ColorProfile* preset);
void clut_disableColorCorrection();
