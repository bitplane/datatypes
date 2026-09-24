#ifndef BITPLANE_XPM_ENCODE_H
#define BITPLANE_XPM_ENCODE_H
#include <stddef.h>
#include <stdint.h>
#include "common/result.h"
#define XPM_MAX_NAME 64
#define XPM_HEADER_MAX (XPM_MAX_NAME + 96u)
#define XPM_COLOR_LINE_MAX 32u
/* Writing takes two passes over the rows: xpm_encoder_add_row for each row
   to collect the colours, then xpm_encoder_finish, the header, one line per
   colour, and xpm_encode_row for each row. Pixels with alpha below 128 are
   written as "None"; the rest are composited over white. */
struct xpm_encoder {
    uint32_t *colors;        /* in first-seen order; XPM_NONE for transparent */
    int32_t *slots;          /* open addressing into colors, -1 for empty */
    size_t count, capacity, mask;
    unsigned cpp;
    unsigned rows_left;
};
#define XPM_NONE 0x01000000u
/* A C identifier from a file name, without its path or extension. */
void xpm_make_name(const char *filename, char name[XPM_MAX_NAME + 1]);
void xpm_encoder_init(struct xpm_encoder *encoder);
void xpm_encoder_free(struct xpm_encoder *encoder);
enum codec_result xpm_encoder_add_row(struct xpm_encoder *encoder,
                                      const uint8_t *rgba, unsigned width);
/* Chooses the characters per pixel. Call once, after every row is added. */
void xpm_encoder_finish(struct xpm_encoder *encoder, unsigned height);
/* The array opening and the values string. Pass a negative hot_x for no
   hotspot. Zero for bad dimensions or a too-small buffer. */
size_t xpm_make_header(const struct xpm_encoder *encoder, const char *name,
                       unsigned width, unsigned height, long hot_x, long hot_y,
                       char *output, size_t capacity);
/* The colour table line for colour index. Zero for a too-small buffer. */
size_t xpm_color_line(const struct xpm_encoder *encoder, size_t index,
                      char *output, size_t capacity);
size_t xpm_row_capacity(const struct xpm_encoder *encoder, unsigned width);
/* One pixel row; the last row also closes the array. SIZE_MAX signals a
   too-small buffer or a colour that was not added. */
size_t xpm_encode_row(struct xpm_encoder *encoder, const uint8_t *rgba,
                      unsigned width, char *output, size_t capacity);
#endif
