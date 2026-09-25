#ifndef BITPLANE_DDS_ENCODE_H
#define BITPLANE_DDS_ENCODE_H
#include <stddef.h>
#include <stdint.h>
#define DDS_HEADER_SIZE 128u
/* Nonzero when any pixel in the row is not fully opaque. */
int dds_row_has_alpha(const uint8_t *rgba, unsigned width);
/* Header of an uncompressed single image: 24-bit RGB, or 32-bit ARGB when
   alpha is set. Returns 0 when the size can't be stored. */
int dds_make_header(unsigned width, unsigned height, int alpha,
                    uint8_t header[DDS_HEADER_SIZE]);
/* Writes width * 3 bytes, or width * 4 with alpha, to output. */
void dds_encode_row(const uint8_t *rgba, unsigned width, int alpha, uint8_t *output);
#endif
