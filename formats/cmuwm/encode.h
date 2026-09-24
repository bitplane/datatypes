#ifndef BITPLANE_CMUWM_ENCODE_H
#define BITPLANE_CMUWM_ENCODE_H
#include <stddef.h>
#include <stdint.h>
#include "decode.h"
#define CMUWM_ROW_MAX ((CMUWM_MAX_SIDE + 7) / 8)
/* A big-endian 14-byte header, as netpbm and the Andrew Toolkit write it.
   Returns zero if the size is out of range. */
int cmuwm_make_header(uint8_t header[CMUWM_HEADER_SIZE], unsigned width, unsigned height);
/* Threshold one RGBA row, composited over white, to 1-bit pixels where a set
   bit is white, padded to a byte with set bits. Returns the bytes written,
   or zero. */
size_t cmuwm_encode_row(const uint8_t *rgba, unsigned width, uint8_t *output, size_t capacity);
#endif
