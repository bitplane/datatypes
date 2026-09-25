#ifndef BITPLANE_C64_DECODE_H
#define BITPLANE_C64_DECODE_H
#include <stddef.h>
#include <stdint.h>
#include "common/result.h"
/* Larger than any supported file, packed or not. */
#define C64_MAX_FILE 65536u
#define C64_WIDTH 320u
#define C64_HEIGHT 200u
/* FLI pictures lose the three leftmost character columns to the FLI bug. */
#define C64_FLI_WIDTH 296u
struct c64_image { unsigned width, height; uint8_t *rgba; };
/* A Commodore 64 bitmap picture as opaque RGBA in the Pepto palette:
   320x200, or 296x200 for FLI. Multicolour pixels are two pixels wide.
   Interlaced pictures blend their two frames, averaging each channel.
   Files carry no magic, so the format is found from the length, the load
   address and any signature. The file name, which may be NULL, only breaks
   ties: an extension naming a format that fits is tried first. */
enum codec_result c64_decode(const uint8_t *data, size_t length,
                             const char *name, struct c64_image *image);
void c64_free(struct c64_image *image);
/* RGB of colour 0-15. */
void c64_colour(unsigned index, uint8_t rgb[3]);
/* Colour 0-15 with exactly this RGB, or -1. */
int c64_index(const uint8_t rgb[3]);
#endif
