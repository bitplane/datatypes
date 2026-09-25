#ifndef BITPLANE_SIXEL_DECODE_H
#define BITPLANE_SIXEL_DECODE_H
#include <stddef.h>
#include <stdint.h>
#include "common/result.h"
/* A stream holding DEC SIXEL images: each a DCS string, ESC P (or 0x90),
   parameters, q, the sixel data and ST. Other text and escape sequences
   around them, as terminal captures have, are skipped. */
struct sixel_image { unsigned width, height; uint8_t *rgba; };
/* Number of complete (terminated) sixel images in the stream. */
unsigned sixel_count(const uint8_t *data, size_t length);
/* Decode image index, counting from 0 in file order. */
enum codec_result sixel_decode(const uint8_t *data, size_t length, unsigned index,
                               struct sixel_image *image);
void sixel_free(struct sixel_image *image);
#endif
