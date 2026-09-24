#ifndef BITPLANE_PCX_DECODE_H
#define BITPLANE_PCX_DECODE_H
#include <stddef.h>
#include <stdint.h>
#include "common/result.h"
struct pcx_image { unsigned width, height; uint8_t *rgba; };
enum codec_result pcx_decode(const uint8_t *data, size_t length, struct pcx_image *image);
void pcx_free(struct pcx_image *image);
#endif
