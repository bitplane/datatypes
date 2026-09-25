#ifndef BITPLANE_XCURSOR_DECODE_H
#define BITPLANE_XCURSOR_DECODE_H
#include <stddef.h>
#include <stdint.h>
#include "common/result.h"

/* Pass as the index to load the largest image, the first of equals. */
#define XCURSOR_BEST (-1L)

struct xcursor_image { unsigned width, height; uint8_t *rgba; };

/* Load one image, counting image entries in table-of-contents order.
   *count is set whenever every image entry is sound, even if the chosen
   image then fails to load. An index past the end is CODEC_INVALID. */
enum codec_result xcursor_decode(const uint8_t *data, size_t length, long index,
                                 struct xcursor_image *image, unsigned *count);
void xcursor_free(struct xcursor_image *image);
#endif
