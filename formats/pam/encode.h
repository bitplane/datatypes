#ifndef BITPLANE_PAM_ENCODE_H
#define BITPLANE_PAM_ENCODE_H
#include <stddef.h>
#include <stdint.h>
/* Saves 8-bit PAM, choosing the smallest tuple type that holds the image. */
enum { PAM_NEEDS_COLOR = 1, PAM_NEEDS_ALPHA = 2 };
#define PAM_HEADER_CAPACITY 96u
/* PAM_NEEDS_* flags for one RGBA row. */
unsigned pam_row_needs(const uint8_t *rgba, unsigned width);
/* Channels for the combined needs: gray, gray and alpha, RGB or RGBA. */
unsigned pam_channels(unsigned needs);
/* Write the header text; returns its length, or 0 if it doesn't fit. */
size_t pam_make_header(unsigned width, unsigned height, unsigned channels,
                       uint8_t *output, size_t capacity);
/* Encode one RGBA row; returns width * channels, or 0 if it doesn't fit. */
size_t pam_encode_row(const uint8_t *rgba, unsigned width, unsigned channels,
                      uint8_t *output, size_t capacity);
#endif
