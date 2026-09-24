#ifndef BITPLANE_XBM_DECODE_H
#define BITPLANE_XBM_DECODE_H
#include <stddef.h>
#include <stdint.h>
#include "common/result.h"
/* One byte per pixel: 1 for a set (foreground) bit, 0 for background.
   hot_x and hot_y are -1 unless the file has a hotspot inside the image. */
struct xbm_image {
    unsigned width, height;
    long hot_x, hot_y;
    uint8_t *pixels;
};
/* Reads X11 (char) and X10 (short) bitmaps. */
enum codec_result xbm_decode(const uint8_t *data, size_t length, struct xbm_image *image);
void xbm_free(struct xbm_image *image);
#endif
