#ifndef BITPLANE_BLP_ENCODE_H
#define BITPLANE_BLP_ENCODE_H
#include <stdint.h>
#define BLP_HEADER_SIZE 1172u
#define BLP_TABLE_SIZE 1024u

/* Colours seen so far. Past 256 distinct colours the image is saved as
   uncompressed BGRA instead of palettized. */
struct blp_palette {
    uint32_t colours[256];
    uint16_t table[BLP_TABLE_SIZE]; /* colour index + 1, or 0 when empty */
    unsigned count;
    int full, alpha;
};

void blp_palette_init(struct blp_palette *palette);
/* Add a row's colours and note whether any pixel is not fully opaque. */
void blp_palette_add_row(struct blp_palette *palette, const uint8_t *rgba, unsigned width);
/* BLP2 header with one level: palettized, or BGRA when the palette is full,
   with 8-bit alpha when any pixel has it. Returns 0 when the size can't be
   stored. */
int blp_make_header(unsigned width, unsigned height, const struct blp_palette *palette,
                    uint8_t header[BLP_HEADER_SIZE]);
/* Bytes of a pixel row: indices, or BGRA when the palette is full. */
unsigned blp_row_size(const struct blp_palette *palette, unsigned width);
void blp_encode_row(const struct blp_palette *palette, const uint8_t *rgba,
                    unsigned width, uint8_t *output);
/* Palettized images with alpha store it after all the index rows, one byte
   per pixel. Writes width bytes. */
void blp_encode_alpha_row(const uint8_t *rgba, unsigned width, uint8_t *output);
#endif
