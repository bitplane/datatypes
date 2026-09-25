#ifndef BITPLANE_MGR_ENCODE_H
#define BITPLANE_MGR_ENCODE_H
#include <stddef.h>
#include <stdint.h>
#include "decode.h"
#define MGR_ROW_MAX ((MGR_MAX_SIDE + 7) / 8)
/* A "yz" header for a 1-bit bitmap. Returns zero if a side is out of range. */
int mgr_make_header(uint8_t header[MGR_HEADER_SIZE], unsigned width, unsigned height);
/* Threshold one RGBA row, composited over white, to 1-bit pixels padded to
   a byte. Returns the bytes written, or zero. */
size_t mgr_encode_row(const uint8_t *rgba, unsigned width, uint8_t *output, size_t capacity);
#endif
