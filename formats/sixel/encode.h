#ifndef BITPLANE_SIXEL_ENCODE_H
#define BITPLANE_SIXEL_ENCODE_H
#include <stddef.h>
#include <stdint.h>
#include "common/result.h"
/* Writes one SIXEL image with RGB colour registers, in two passes over the
   rows. SIXEL colours are percentages, so channels round to 101 levels.
   Images with more than 256 colours are reduced by median cut. Pixels
   with alpha below 128 are left undrawn (P2 = 1); the rest are composited
   over white. */
#define SIXEL_MAX_COLORS 256u
/* Enough for the introducer, raster attributes and every colour. */
#define SIXEL_HEADER_MAX (40u + SIXEL_MAX_COLORS * 20u)

struct sixel_table;

struct sixel_encoder {
    unsigned width, height, rows, band_rows, colors, cursor, lines;
    int transparent, quantised;
    struct sixel_table *table;   /* exact colours while there are few */
    uint32_t *histogram;         /* per 15-bit bin: count, r, g, b sums */
    uint8_t *bin_color;          /* per 15-bit bin: register */
    uint8_t *band;               /* six rows of registers */
    uint16_t *first, *last;      /* per register: columns used in the band */
    uint8_t palette[SIXEL_MAX_COLORS][3]; /* percent */
};

enum codec_result sixel_encoder_init(struct sixel_encoder *e, unsigned width, unsigned height);
/* First pass: every row, top down. */
enum codec_result sixel_encoder_scan(struct sixel_encoder *e, const uint8_t *rgba);
/* Choose the registers after the first pass. */
enum codec_result sixel_encoder_plan(struct sixel_encoder *e);
/* The DCS introducer, raster attributes and colour registers. 0 if out
   is too small. */
size_t sixel_encoder_header(const struct sixel_encoder *e, char *out, size_t capacity);
/* Second pass: every row, top down. Returns 1 when a band of six rows (or
   the last, shorter band) is ready for sixel_encoder_next. */
int sixel_encoder_add_row(struct sixel_encoder *e, const uint8_t *rgba);
/* The next piece of the ready band, at most sixel_line_capacity bytes.
   Returns 0 once the band is written. */
size_t sixel_encoder_next(struct sixel_encoder *e, char *out, size_t capacity);
size_t sixel_line_capacity(unsigned width);
/* The string terminator. */
size_t sixel_encoder_end(char *out, size_t capacity);
void sixel_encoder_free(struct sixel_encoder *e);
#endif
