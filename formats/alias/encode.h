#ifndef BITPLANE_ALIAS_ENCODE_H
#define BITPLANE_ALIAS_ENCODE_H
#include <stddef.h>
#include <stdint.h>
#include "rla.h"

/* 1 if any pixel of the RGBA row isn't opaque. */
int alias_row_has_alpha(const uint8_t *rgba, unsigned width);
/* RLA header for an 8-bit RGB image, with a matte channel when alpha is set.
   Returns 0 for a size RLA can't hold. */
int alias_make_header(unsigned width, unsigned height, int alpha,
                      uint8_t header[RLA_HEADER_SIZE]);
/* Output bytes one encoded scanline can need. */
size_t alias_row_capacity(unsigned width);
/* Encode one RGBA row as an RLA scanline: red, green, blue, and with alpha
   the matte, colour multiplied by it. Without alpha, colour is composited
   over white. Returns the size written. */
size_t alias_encode_row(const uint8_t *rgba, unsigned width, int alpha,
                        uint8_t *output);
/* Big-endian 32-bit value, for the scanline offset table. */
void alias_put32(uint8_t *p, unsigned long value);
#endif
