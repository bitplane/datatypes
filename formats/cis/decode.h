#ifndef BITPLANE_CIS_DECODE_H
#define BITPLANE_CIS_DECODE_H
#include <stddef.h>
#include <stdint.h>
#include "common/result.h"
#define CIS_ESC 0x1b
#define CIS_MEDIUM_WIDTH 128
#define CIS_MEDIUM_HEIGHT 96
#define CIS_HIGH_WIDTH 256
#define CIS_HIGH_HEIGHT 192
/* One byte per pixel: 0 is white, 1 is black. */
struct cis_image { unsigned width, height; uint8_t *pixels; };
/* ESC G M (128x96) or ESC G H (256x192), after any leading junk, then runs
   of alternating colour, black first, each one character: its 7-bit code
   minus 32. Other control characters are skipped. Any ESC, normally
   ESC G N, ends the image, and pixels it leaves unset are white. Without an
   ESC the runs must fill the image; runs past its end are ignored. */
enum codec_result cis_decode(const uint8_t *data, size_t length, struct cis_image *image);
void cis_free(struct cis_image *image);
#endif
