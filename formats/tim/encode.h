#ifndef BITPLANE_TIM_ENCODE_H
#define BITPLANE_TIM_ENCODE_H
#include <stddef.h>
#include <stdint.h>
/* Header for a 24-bit TIM; 0 if the image is too big for one. */
int tim_make_header(unsigned width, unsigned height, uint8_t header[20]);
/* Bytes in one 24-bit row, padded to 16 bits. */
size_t tim_row_size(unsigned width);
/* Write one RGBA row as 24-bit RGB composited over white, plus any padding. */
void tim_encode_row(const uint8_t *rgba, unsigned width, uint8_t *output);
#endif
