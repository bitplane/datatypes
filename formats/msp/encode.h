#ifndef BITPLANE_MSP_ENCODE_H
#define BITPLANE_MSP_ENCODE_H
#include <stddef.h>
#include <stdint.h>
#define MSP_HEADER_SIZE 32
/* A version 1 (uncompressed) header. Returns 0 for an unsupported size. */
int msp_make_header(unsigned width, unsigned height, uint8_t header[MSP_HEADER_SIZE]);
/* Threshold one RGBA row, composited over white, to 1-bit pixels (1 is white).
   Returns the (width + 7) / 8 bytes written, or zero. */
size_t msp_encode_row(const uint8_t *rgba, unsigned width, uint8_t *output, size_t capacity);
#endif
