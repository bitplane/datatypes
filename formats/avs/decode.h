#ifndef BITPLANE_AVS_DECODE_H
#define BITPLANE_AVS_DECODE_H
#include <stddef.h>
#include <stdint.h>
#include "common/result.h"
/* AVS X images are big-endian ARGB; AAI (Dune HD) images are little-endian
   BGRA. A file may hold several images of one variant back to back. */
enum avs_variant { AVS_VARIANT_AVS, AVS_VARIANT_AAI };
struct avs_image { unsigned width, height; uint8_t *rgba; };
/* The variant, told apart by the first header. 0 when it is neither. */
int avs_detect(const uint8_t *data, size_t length, enum avs_variant *variant);
/* Number of complete images before the end, a zero-sized header or damage. */
unsigned avs_count(const uint8_t *data, size_t length);
/* Decodes image index. An AVS image whose alpha is zero everywhere loads
   opaque; AAI alpha 254 loads as 255. */
enum codec_result avs_decode(const uint8_t *data, size_t length, unsigned index,
                             struct avs_image *image);
void avs_free(struct avs_image *image);
#endif
