#ifndef BITPLANE_UTAHRLE_ENCODE_H
#define BITPLANE_UTAHRLE_ENCODE_H
#include <stddef.h>
#include <stdint.h>

#define UTAHRLE_HEADER_SIZE 16u
#define UTAHRLE_NEEDS_COLOUR 1u
#define UTAHRLE_NEEDS_ALPHA 2u

/* UTAHRLE_NEEDS_* bits for one RGBA row: colour if any pixel isn't gray,
   alpha if any pixel isn't opaque. */
unsigned utahrle_row_needs(const uint8_t *rgba, unsigned width);
/* Header for what needs asks for: gray, RGB, or RGB with alpha. Gray with
   alpha is saved as RGB with alpha, which more readers accept. */
int utahrle_make_header(unsigned width, unsigned height, unsigned needs,
                        uint8_t header[UTAHRLE_HEADER_SIZE]);
/* Output bytes one encoded row can need. */
size_t utahrle_row_capacity(unsigned width);
/* Encode one RGBA row; rows go bottom up. Every row but the first starts
   by moving to the next line. Returns 0 if output is too small. */
size_t utahrle_encode_row(const uint8_t *rgba, unsigned width, unsigned needs,
                          int first, uint8_t *output, size_t capacity);
/* The end-of-image opcode and its filler byte. */
extern const uint8_t utahrle_end[2];
#endif
