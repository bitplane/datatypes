#ifndef BITPLANE_MSP_DECODE_H
#define BITPLANE_MSP_DECODE_H
#include <stddef.h>
#include <stdint.h>
#include "common/result.h"
/* One byte per pixel: 1 for white, 0 for black. */
struct msp_image { unsigned width, height; uint8_t *pixels; };
/* Reads version 1 ("DanM", uncompressed) and version 2 ("LinS", RLE) files. */
enum codec_result msp_decode(const uint8_t *data, size_t length, struct msp_image *image);
void msp_free(struct msp_image *image);
#endif
