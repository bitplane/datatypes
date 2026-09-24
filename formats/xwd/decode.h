#ifndef BITPLANE_XWD_DECODE_H
#define BITPLANE_XWD_DECODE_H
#include <stddef.h>
#include <stdint.h>
#include "common/result.h"
struct xwd_image { unsigned width, height; uint8_t *rgba; };
/* Decode an X11 (version 7) window dump to opaque RGBA. */
enum codec_result xwd_decode(const uint8_t *data, size_t length,
                             struct xwd_image *image);
void xwd_free(struct xwd_image *image);
#endif
