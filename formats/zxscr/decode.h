#ifndef BITPLANE_ZXSCR_DECODE_H
#define BITPLANE_ZXSCR_DECODE_H
#include <stddef.h>
#include <stdint.h>
#include "common/result.h"
/* 6144 bytes of bitmap then 768 attribute bytes, one per 8x8 cell. */
#define ZXSCR_FILE_SIZE 6912u
/* Timex hi-colour: the bitmap then 6144 attribute bytes, one per 8x1 span. */
#define ZXSCR_TIMEX_SIZE 12288u
#define ZXSCR_WIDTH 256u
#define ZXSCR_HEIGHT 192u
/* SXG pictures are the only ones whose size comes from a header. */
#define ZXSCR_MAX_PIXELS (16u * 1024u * 1024u)
struct zxscr_image { unsigned width, height; uint8_t *rgba; };
/* 12288-byte files are Timex hi-colour screens unless their name says they
   are 8x1 multicolour pictures: .mc (linear bitmap) or .mlt. */
enum zxscr_name { ZXSCR_NAME_OTHER, ZXSCR_NAME_MC, ZXSCR_NAME_MLT };
enum zxscr_name zxscr_name_kind(const char *name);
/* An opaque RGBA picture from any of the screen formats zxscr reads, told
   apart by magic (SXG, MultiArtist) or by file size (everything else).
   Flashing cells show their first phase, ink on paper. Two-frame
   Gigascreen pictures show the average of the frames. */
enum codec_result zxscr_decode(const uint8_t *data, size_t length,
                               enum zxscr_name name, struct zxscr_image *image);
void zxscr_free(struct zxscr_image *image);
/* RGB of colour 0-7 (bit 0 blue, 1 red, 2 green), bright or not. */
void zxscr_colour(unsigned colour, int bright, uint8_t rgb[3]);
/* Offset in the bitmap of the byte holding pixels x..x+7 of row y. */
size_t zxscr_offset(unsigned x, unsigned y);
#endif
