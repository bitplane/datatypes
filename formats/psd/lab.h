#ifndef BITPLANE_PSD_LAB_H
#define BITPLANE_PSD_LAB_H
#include <stdint.h>
/* CIE L*a*b* relative to D50, as Photoshop stores it (L 0-100, a and b
   -128 to 128), to 8-bit sRGB through a Bradford adaptation to D65.
   Out-of-gamut colours are clipped per channel. */
void lab_to_srgb(double l, double a, double b, uint8_t rgb[3]);
#endif
