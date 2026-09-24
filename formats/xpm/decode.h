#ifndef BITPLANE_XPM_DECODE_H
#define BITPLANE_XPM_DECODE_H
#include <stddef.h>
#include <stdint.h>
#include "common/result.h"
#define XPM_MAX_CPP 32
/* Straight RGBA, four bytes per pixel. has_alpha is set when some pixels
   are transparent ("None") and others are not. hot_x and hot_y are -1
   unless the file has a hotspot inside the image. */
struct xpm_image {
    unsigned width, height;
    long hot_x, hot_y;
    int has_alpha;
    uint8_t *rgba;
};
/* Reads XPM3, XPM2 (natural and C syntax) and XPM1. */
enum codec_result xpm_decode(const uint8_t *data, size_t length, struct xpm_image *image);
void xpm_free(struct xpm_image *image);
#endif
