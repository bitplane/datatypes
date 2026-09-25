#ifndef BITPLANE_MGR_DECODE_H
#define BITPLANE_MGR_DECODE_H
#include <stddef.h>
#include <stdint.h>
#include "common/result.h"
/* Each side is two characters of six bits, offset by ' '. */
#define MGR_MAX_SIDE 4095
#define MGR_HEADER_SIZE 8
#define MGR_OLD_HEADER_SIZE 6
/* One byte per pixel: 0 is white, 1 is black. */
struct mgr_image { unsigned width, height; uint8_t *pixels; };
/* "yz" with depth 1 (rows padded to 8 bits), or the old "zz" and "xz"
   (rows padded to 16 and 32 bits). Other depths are rejected. */
enum codec_result mgr_decode(const uint8_t *data, size_t length, struct mgr_image *image);
void mgr_free(struct mgr_image *image);
#endif
