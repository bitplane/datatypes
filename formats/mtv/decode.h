#ifndef BITPLANE_MTV_DECODE_H
#define BITPLANE_MTV_DECODE_H
#include <stddef.h>
#include <stdint.h>
#include "common/result.h"
struct mtv_image { unsigned width, height; uint8_t *rgba; };
/* Number of images: MTV files may hold several one after another, QRT one.
   0 when the data is neither. */
unsigned mtv_count(const uint8_t *data, size_t length);
/* Decodes image index of an MTV file, or a QRT file (index 0 only),
   telling them apart by content. Images load opaque. */
enum codec_result mtv_decode(const uint8_t *data, size_t length, unsigned index,
                             struct mtv_image *image);
void mtv_free(struct mtv_image *image);
#endif
