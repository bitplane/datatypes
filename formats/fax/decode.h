#ifndef BITPLANE_FAX_DECODE_H
#define BITPLANE_FAX_DECODE_H
#include <stddef.h>
#include <stdint.h>
#include "common/result.h"

#define CALS_HEADER_SIZE 2048

/* Rows of stride bytes, most significant bit first: a set bit is black. */
struct fax_image { unsigned width, height; size_t stride; uint8_t *pixels; };

/* A CALS type 1 raster, or else raw Group 3 one-dimensional (MH) fax data. */
enum codec_result fax_decode(const uint8_t *data, size_t length, struct fax_image *image);
/* Raw Group 3 MH: first page, bits in either order, as wide as its longest line. */
enum codec_result fax_decode_g3(const uint8_t *data, size_t length, struct fax_image *image);
/* Raw Group 4 (T.6) data of a known size, most significant bit first. */
enum codec_result fax_decode_g4(const uint8_t *data, size_t length,
                                unsigned width, unsigned height, struct fax_image *image);
int fax_is_cals(const uint8_t *data, size_t length);
void fax_free(struct fax_image *image);

static inline int fax_pixel(const struct fax_image *image, unsigned x, unsigned y)
{
    return image->pixels[(size_t)y * image->stride + x / 8u] >> (7u - x % 8u) & 1;
}
#endif
