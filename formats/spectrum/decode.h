#ifndef BITPLANE_SPECTRUM_DECODE_H
#define BITPLANE_SPECTRUM_DECODE_H
#include <stddef.h>
#include <stdint.h>
#include "common/result.h"

#define SPECTRUM_WIDTH 320
#define SPECTRUM_HEIGHT 200
/* 32000 bytes of screen, then 48 palette words for each of lines 1-199. */
#define SPU_SCREEN_SIZE 32000u
#define SPU_PALETTE_WORDS (199u * 48u)
#define SPU_FILE_SIZE (SPU_SCREEN_SIZE + SPU_PALETTE_WORDS * 2u)

struct spectrum_image { unsigned width, height; uint8_t *rgba; };

/* Reads uncompressed (SPU) and compressed (SPC) Spectrum 512 pictures as
   320x200 RGBA. Line 0 has no palette, so it is black. */
enum codec_result spectrum_decode(const uint8_t *data, size_t length,
                                  struct spectrum_image *image);
void spectrum_free(struct spectrum_image *image);
/* The palette slot (0-47) that colour c uses at column x of a line. */
unsigned spectrum_slot(unsigned c, unsigned x);
#endif
