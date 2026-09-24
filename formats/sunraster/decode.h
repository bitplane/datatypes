#ifndef BITPLANE_SUNRASTER_DECODE_H
#define BITPLANE_SUNRASTER_DECODE_H
#include <stddef.h>
#include <stdint.h>
#include "common/result.h"
struct sunraster_image { unsigned width, height; uint8_t *rgba; };
enum codec_result sunraster_decode(const uint8_t *data, size_t length,
                                   struct sunraster_image *image);
void sunraster_free(struct sunraster_image *image);
#endif
