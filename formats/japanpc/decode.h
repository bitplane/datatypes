#ifndef BITPLANE_JAPANPC_DECODE_H
#define BITPLANE_JAPANPC_DECODE_H
#include <stddef.h>
#include <stdint.h>
#include "common/result.h"

struct japanpc_image { unsigned width, height; uint8_t *rgba; };

/* Decode a Maki-chan MAG or MKI, Pi or X68000 PIC file, chosen by its
   magic, to opaque RGBA scaled to the machine's pixel aspect. */
enum codec_result japanpc_decode(const uint8_t *data, size_t size,
                                 struct japanpc_image *image);
void japanpc_free(struct japanpc_image *image);

#endif
