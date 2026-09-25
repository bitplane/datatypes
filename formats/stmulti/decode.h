#ifndef BITPLANE_STMULTI_DECODE_H
#define BITPLANE_STMULTI_DECODE_H
#include <stddef.h>
#include <stdint.h>
#include "common/result.h"

/* PhotoChrome's screen: 32000 bytes of separate bitplanes. Line 0 has no
   palette, so the picture starts at line 1. */
#define PCS_WIDTH 320u
#define PCS_HEIGHT 199u
#define PCS_BITMAP 32000u
/* 48 palette words for each shown line; the last line's colours run on
   into 16 more. Files often store another 48, which nothing shows. */
#define PCS_PALETTE_WORDS (PCS_HEIGHT * 48u + 16u)

struct stmulti_image { unsigned width, height; uint8_t *rgba; };

/* Reads Multi Palette Picture (MPP, all four modes) and PhotoChrome (PCS)
   pictures as RGBA. Pictures of two alternating screens are blended. */
enum codec_result stmulti_decode(const uint8_t *data, size_t length,
                                 struct stmulti_image *image);
void stmulti_free(struct stmulti_image *image);
#endif
