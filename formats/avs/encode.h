#ifndef BITPLANE_AVS_ENCODE_H
#define BITPLANE_AVS_ENCODE_H
#include <stdint.h>
#include "decode.h"
int avs_make_header(enum avs_variant variant, unsigned width, unsigned height,
                    uint8_t header[8]);
/* Writes width * 4 bytes: ARGB for AVS, BGRA for AAI, alpha as given. */
void avs_encode_row(enum avs_variant variant, const uint8_t *rgba,
                    unsigned width, uint8_t *output);
#endif
