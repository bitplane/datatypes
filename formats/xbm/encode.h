#ifndef BITPLANE_XBM_ENCODE_H
#define BITPLANE_XBM_ENCODE_H
#include <stddef.h>
#include <stdint.h>
#define XBM_MAX_NAME 64
#define XBM_HEADER_MAX (5u * (XBM_MAX_NAME + 40u))
struct xbm_encoder {
    size_t remaining;        /* bytes still to write */
    unsigned column;         /* bytes on the current output line */
};
/* A C identifier from a file name, without its path or extension. */
void xbm_make_name(const char *filename, char name[XBM_MAX_NAME + 1]);
/* The defines and the array opening. Pass a negative hot_x for no hotspot.
   Zero for bad dimensions or a too-small buffer. */
size_t xbm_make_header(const char *name, unsigned width, unsigned height,
                       long hot_x, long hot_y, char *output, size_t capacity);
void xbm_encoder_init(struct xbm_encoder *encoder, unsigned width, unsigned height);
size_t xbm_row_capacity(unsigned width);
/* Dark pixels, after compositing over white, become set bits. The last row
   also closes the array. SIZE_MAX signals a too-small output buffer. */
size_t xbm_encode_row(struct xbm_encoder *encoder, const uint8_t *rgba,
                      unsigned width, char *output, size_t capacity);
#endif
