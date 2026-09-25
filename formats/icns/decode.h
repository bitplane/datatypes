#ifndef BITPLANE_ICNS_DECODE_H
#define BITPLANE_ICNS_DECODE_H
#include <stddef.h>
#include <stdint.h>
#include "common/result.h"

/* Pass as the index to load the largest, then deepest, image. */
#define ICNS_BEST (-1L)

struct icns_image { unsigned width, height; uint8_t *rgba; };

/* Load one image, counting in file order the entries that can be loaded.
   *count is set whenever the file's entry list is sound, even if the chosen
   image then fails to load. An index past the end is CODEC_INVALID. */
enum codec_result icns_decode(const uint8_t *data, size_t length, long index,
                              struct icns_image *image, unsigned *count);
void icns_free(struct icns_image *image);
#endif
