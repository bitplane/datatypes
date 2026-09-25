#ifndef BITPLANE_DCX_PCXDECODE_H
#define BITPLANE_DCX_PCXDECODE_H
/* A copy of formats/pcx/decode.[ch] that also accepts an odd bytes per line
   and 8-bit RGBA (four planes, the fourth alpha), and shows 1-bit images
   whose two palette colours match in black and white. Its default
   palette, for files without one, follows netpbm.
   Keep the two in step until the PCX codec moves to common/. */
#include <stddef.h>
#include <stdint.h>
#include "common/result.h"
struct pcx_image { unsigned width, height; uint8_t *rgba; };
enum codec_result pcx_decode(const uint8_t *data, size_t length, struct pcx_image *image);
void pcx_free(struct pcx_image *image);
#endif
