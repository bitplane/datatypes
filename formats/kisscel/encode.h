#ifndef BITPLANE_KISSCEL_ENCODE_H
#define BITPLANE_KISSCEL_ENCODE_H
#include <stdint.h>

#define KISSCEL_HEADER_SIZE 32u

/* Header of a 32-bit cel at offset 0, which needs no palette file.
   Returns 0 when the size doesn't fit. */
int kisscel_make_header(unsigned width, unsigned height,
                        uint8_t header[KISSCEL_HEADER_SIZE]);
/* One row of RGBA as blue, green, red and straight alpha: width * 4 bytes. */
void kisscel_encode_row(const uint8_t *rgba, unsigned width, uint8_t *out);
#endif
