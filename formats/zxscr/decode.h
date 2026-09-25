#ifndef BITPLANE_ZXSCR_DECODE_H
#define BITPLANE_ZXSCR_DECODE_H
#include <stddef.h>
#include <stdint.h>
#include "common/result.h"
/* 6144 bytes of bitmap then 768 attribute bytes, one per 8x8 cell. */
#define ZXSCR_FILE_SIZE 6912u
#define ZXSCR_WIDTH 256u
#define ZXSCR_HEIGHT 192u
struct zxscr_image { unsigned width, height; uint8_t *rgba; };
/* A 256x192 opaque RGBA picture. Files must be exactly ZXSCR_FILE_SIZE bytes:
   longer ones are other screen formats (ULA+, Timex) and are CODEC_INVALID.
   Flashing cells show their first phase, ink on paper. */
enum codec_result zxscr_decode(const uint8_t *data, size_t length,
                               struct zxscr_image *image);
void zxscr_free(struct zxscr_image *image);
/* RGB of colour 0-7 (bit 0 blue, 1 red, 2 green), bright or not. */
void zxscr_colour(unsigned colour, int bright, uint8_t rgb[3]);
/* Offset in the bitmap of the byte holding pixels x..x+7 of row y. */
size_t zxscr_offset(unsigned x, unsigned y);
#endif
