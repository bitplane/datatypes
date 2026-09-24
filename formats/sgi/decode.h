#ifndef BITPLANE_SGI_DECODE_H
#define BITPLANE_SGI_DECODE_H

#include <stddef.h>
#include <stdint.h>

#include "common/result.h"

struct sgi_image {
    unsigned width;
    unsigned height;
    uint8_t *rgba;
};

enum codec_result sgi_decode(const uint8_t *data, size_t length,
                             struct sgi_image *image);
void sgi_free(struct sgi_image *image);

#endif
