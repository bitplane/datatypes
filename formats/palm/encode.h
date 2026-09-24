#ifndef BITPLANE_PALM_ENCODE_H
#define BITPLANE_PALM_ENCODE_H
#include <stddef.h>
#include <stdint.h>

/* Header, a full colour table and direct colour info. */
#define PALM_HEADER_MAX (16u + 2u + 256u * 4u + 8u)

/* Chooses between an 8-bit bitmap with a colour table, which is lossless, and
   16-bit RGB565 when there are more than 256 colours. Pixels with zero alpha
   become the transparent colour; others are composited over white. */
struct palm_encoder {
    unsigned width, height, colors, key;
    int transparent, direct;
    uint32_t palette[257];
    uint8_t used[65536u / 8u];
};

void palm_encode_begin(struct palm_encoder *e, unsigned width, unsigned height);
/* First pass: look at every row, top down. */
void palm_encode_scan(struct palm_encoder *e, const uint8_t *rgba);
/* Choose the variant and write the header. Returns its size, or 0 when the
   image can't be saved. */
size_t palm_encode_header(struct palm_encoder *e, uint8_t *output, size_t capacity);
size_t palm_encode_row_size(const struct palm_encoder *e);
/* Second pass: encode each row, top down. Returns the row size or 0. */
size_t palm_encode_row(const struct palm_encoder *e, const uint8_t *rgba,
                       uint8_t *output, size_t capacity);
#endif
