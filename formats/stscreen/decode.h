#ifndef BITPLANE_STSCREEN_DECODE_H
#define BITPLANE_STSCREEN_DECODE_H
#include <stddef.h>
#include <stdint.h>
#include "common/result.h"
struct stscreen_image { unsigned width, height; uint8_t *rgba; };
/* Decodes an Atari ST screen dump to opaque RGBA. Most of these formats have
   no magic, so name (the file name, or NULL) picks the formats to try by its
   extension; an unknown extension only tries the ones with a signature.
   Medium resolution stays 640x200 and high resolution is black on white. */
enum codec_result stscreen_decode(const uint8_t *data, size_t length,
                                  const char *name,
                                  struct stscreen_image *image);
void stscreen_free(struct stscreen_image *image);
/* 8-bit level of one palette nibble, on an ST (3 bits) or an STE (4 bits). */
uint8_t stscreen_level(unsigned nibble, int ste);
#endif
