#ifndef BITPLANE_SUNICON_ENCODE_H
#define BITPLANE_SUNICON_ENCODE_H
#include <stddef.h>
#include <stdint.h>
#define SUNICON_HEADER_MAX 96u
struct sunicon_encoder {
    size_t remaining;        /* items still to write */
    unsigned column;         /* items on the current output line */
};
/* The header comment for a Depth=1 icon with 16-bit items. The declared
   width is rounded up to whole items, as XView requires and as netpbm needs
   to read the rows; the extra columns are white. Zero for bad dimensions
   (including widths above 65520) or a too-small buffer. */
size_t sunicon_make_header(unsigned width, unsigned height, char *output, size_t capacity);
void sunicon_encoder_init(struct sunicon_encoder *encoder, unsigned width, unsigned height);
size_t sunicon_row_capacity(unsigned width);
/* Dark pixels, after compositing over white, become set bits, and the
   padding bits stay clear. SIZE_MAX signals a too-small output buffer. */
size_t sunicon_encode_row(struct sunicon_encoder *encoder, const uint8_t *rgba,
                          unsigned width, char *output, size_t capacity);
#endif
