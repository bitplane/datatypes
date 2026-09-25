#ifndef BITPLANE_STPAINT_DECODE_H
#define BITPLANE_STPAINT_DECODE_H
#include <stddef.h>
#include <stdint.h>
#include "common/result.h"

/* Every format here unpacks to one 32000-byte ST screen, except PaintShop. */
#define STPAINT_SCREEN 32000u

struct stpaint_image { unsigned width, height; uint8_t *rgba; };

/* Reads Tiny, CrackArt, Imagic, STAD, compressed Dali, Pablo, Picworks and
   PaintShop pictures as opaque RGBA: 320x200 (low), 640x200 (medium) or
   640x400 (high resolution, black on white), or PaintShop's own size.
   Formats with a signature are found by it. Tiny, Dali and Picworks have
   none, so name (the file name, or NULL) must carry their extension; Dali
   keeps its resolution only there. */
enum codec_result stpaint_decode(const uint8_t *data, size_t length,
                                 const char *name, struct stpaint_image *image);
void stpaint_free(struct stpaint_image *image);
#endif
