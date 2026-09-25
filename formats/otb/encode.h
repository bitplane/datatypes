#ifndef BITPLANE_OTB_ENCODE_H
#define BITPLANE_OTB_ENCODE_H
#include <stddef.h>
#include <stdint.h>
#define OTB_HEADER_MAX 6
/* One-picture header, with 8-bit sizes when both fit. Returns its length,
   or zero for an unsupported size. */
size_t otb_make_header(unsigned width, unsigned height, uint8_t header[OTB_HEADER_MAX]);
/* Threshold one RGBA row, composited over white, to 1-bit pixels where set
   is black. Rows are padded to a byte, as ImageMagick reads them.
   Returns the (width + 7) / 8 bytes written, or zero. */
size_t otb_encode_row(const uint8_t *rgba, unsigned width, uint8_t *output, size_t capacity);
#endif
