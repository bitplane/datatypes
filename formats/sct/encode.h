#ifndef BITPLANE_SCT_ENCODE_H
#define BITPLANE_SCT_ENCODE_H
#include <stddef.h>
#include <stdint.h>
#define SCT_HEADER_SIZE 2048
/* Writes the control and parameter blocks of a CT file at 72 dpi: black
   only when gray, otherwise cyan, magenta and yellow. Returns 0 for a bad
   size. */
int sct_make_header(unsigned width, unsigned height, int gray,
                    uint8_t header[SCT_HEADER_SIZE]);
/* Bytes that sct_encode_row writes for one line. */
size_t sct_line_size(unsigned width, int gray);
/* Whether a row of RGBA, composited over white, is all gray. */
int sct_row_is_gray(const uint8_t *rgba, unsigned width);
/* Writes one line of separations, composited over white. */
void sct_encode_row(const uint8_t *rgba, unsigned width, int gray,
                    uint8_t *output);
#endif
