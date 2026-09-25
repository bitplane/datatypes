#ifndef BITPLANE_TIM2_ENCODE_H
#define BITPLANE_TIM2_ENCODE_H
#include <stddef.h>
#include <stdint.h>
#define TIM2_WRITE_HEADER 64u
/* File and picture header for one 24-bit picture, or 32-bit with alpha;
   0 if the image is too big for one. */
int tim2_make_header(unsigned width, unsigned height, int alpha,
                     uint8_t header[TIM2_WRITE_HEADER]);
size_t tim2_row_size(unsigned width, int alpha);
/* Write one RGBA row as 24-bit RGB, or as 32-bit with alpha scaled to the
   GS's 0-0x80 range. */
void tim2_encode_row(const uint8_t *rgba, unsigned width, int alpha,
                     uint8_t *output);
#endif
