#ifndef BITPLANE_FTEX_ENCODE_H
#define BITPLANE_FTEX_ENCODE_H
#include <stdint.h>
/* Header, one directory entry and the level size: the pixels follow. */
#define FTEX_HEADER_SIZE 36u
/* Header of an uncompressed (FTU) file with one RGB format and one level.
   Returns 0 when the size can't be stored. */
int ftex_make_header(unsigned width, unsigned height, uint8_t header[FTEX_HEADER_SIZE]);
/* Writes width * 3 bytes of RGB to output, compositing alpha over white. */
void ftex_encode_row(const uint8_t *rgba, unsigned width, uint8_t *output);
#endif
