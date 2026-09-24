#ifndef BITPLANE_SGI_ENCODE_H
#define BITPLANE_SGI_ENCODE_H

#include <stddef.h>
#include <stdint.h>

/* An RLE file is the header, the offset and length tables, then the rows.
   The tables locate every row, so rows can be written top down. */
#define SGI_HEADER_SIZE 512u

enum { SGI_NEEDS_COLOR = 1, SGI_NEEDS_ALPHA = 2 };

/* SGI_NEEDS_* flags for one RGBA row. */
unsigned sgi_row_needs(const uint8_t *rgba, unsigned width);
/* Channels to save for the combined needs: gray, RGB or RGBA.
   Channel n holds RGBA component n; gray is stored from red. */
unsigned sgi_channels(unsigned needs);
int sgi_make_header(unsigned width, unsigned height, unsigned channels,
                    uint8_t header[SGI_HEADER_SIZE]);
/* Worst-case encoded size of one channel of a row. */
size_t sgi_rle_capacity(unsigned width);
/* RLE-encode one channel of an RGBA row. Zero means output is too small. */
size_t sgi_encode_rle(const uint8_t *rgba, unsigned width, unsigned channel,
                      uint8_t *output, size_t capacity);
/* lengths holds four encoded sizes per row, top row first, indexed by
   channel. Fills tables with height * channels * 8 bytes, for rows written
   top down with their channels in order. Zero if an offset overflows. */
int sgi_make_tables(const uint32_t *lengths, unsigned height, unsigned channels,
                    uint8_t *tables);

#endif
