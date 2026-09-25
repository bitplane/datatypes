#ifndef BITPLANE_MTV_ENCODE_H
#define BITPLANE_MTV_ENCODE_H
#include <stddef.h>
#include <stdint.h>
#define MTV_HEADER_CAPACITY 16
/* Writes "width height\n"; returns its length, or 0 for a bad size. */
size_t mtv_make_header(unsigned width, unsigned height,
                       uint8_t header[MTV_HEADER_CAPACITY]);
/* Writes width * 3 bytes of RGB to output, composited over white. */
void mtv_encode_row(const uint8_t *rgba, unsigned width, uint8_t *output);
#endif
