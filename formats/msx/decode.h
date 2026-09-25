#ifndef BITPLANE_MSX_DECODE_H
#define BITPLANE_MSX_DECODE_H
#include <stddef.h>
#include <stdint.h>
#include "common/result.h"

/* The most a Graph Saurus palette file holds that we use: 16 colours. */
#define MSX_PALETTE_SIZE 32u

struct msx_image { unsigned width, height; uint8_t *rgba; };

/* Decodes an MSX screen dump to opaque RGBA. These have no magic beyond
   the BSAVE header, so name (the file name, or NULL) picks the screen mode
   by its extension; an unknown extension or NULL is CODEC_INVALID.
   palette is the start of a Graph Saurus .PL5, .PL6 or .PL7 file, or NULL;
   other modes ignore it. 512-pixel modes keep their 212 lines. */
enum codec_result msx_decode(const uint8_t *data, size_t length,
                             const char *name, const uint8_t *palette,
                             size_t palette_length, struct msx_image *image);
void msx_free(struct msx_image *image);

/* The extension of the palette file that goes with name ("pl5", "pl6" or
   "pl7"), or NULL when its mode keeps the palette in the picture. */
const char *msx_palette_ext(const char *name);
#endif
