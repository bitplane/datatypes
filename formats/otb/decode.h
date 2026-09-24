#ifndef BITPLANE_OTB_DECODE_H
#define BITPLANE_OTB_DECODE_H
#include <stddef.h>
#include <stdint.h>
#include "common/result.h"
#define OTB_MAX_PICTURES 16u
struct otb_image { unsigned width, height; uint8_t *rgba; };
/* Decode picture `index` of a 1-bit OTA bitmap, where 0 is the main image
   and 1 to 15 are its animation frames. Set bits are black. Once the header
   has been read, *count (if not NULL) holds the number of pictures, even if
   decoding then fails. */
enum codec_result otb_decode(const uint8_t *data, size_t length, unsigned index,
                             unsigned *count, struct otb_image *image);
void otb_free(struct otb_image *image);
#endif
