#include <string.h>

#include "encode.h"

#define MAX_SIDE 65535u
#define PALETTE_OFFSET 148u

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static uint32_t pack(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

/* Slot holding colour, or the empty slot where it belongs. */
static unsigned find(const struct blp_palette *palette, uint32_t colour)
{
    unsigned slot = (unsigned)((colour * 2654435761u) >> 22) & (BLP_TABLE_SIZE - 1u);

    while (palette->table[slot] != 0 && palette->colours[palette->table[slot] - 1u] != colour)
        slot = (slot + 1u) & (BLP_TABLE_SIZE - 1u);
    return slot;
}

void blp_palette_init(struct blp_palette *palette)
{
    memset(palette, 0, sizeof *palette);
}

void blp_palette_add_row(struct blp_palette *palette, const uint8_t *rgba, unsigned width)
{
    unsigned x, slot;
    uint32_t colour;

    for (x = 0; x < width; x++, rgba += 4) {
        if (rgba[3] != 255)
            palette->alpha = 1;
        if (palette->full)
            continue;
        colour = pack(rgba);
        slot = find(palette, colour);
        if (palette->table[slot] != 0)
            continue;
        if (palette->count == 256) {
            palette->full = 1;
            continue;
        }
        palette->colours[palette->count++] = colour;
        palette->table[slot] = (uint16_t)palette->count;
    }
}

int blp_make_header(unsigned width, unsigned height, const struct blp_palette *palette,
                    uint8_t header[BLP_HEADER_SIZE])
{
    unsigned long pixels = (unsigned long)width * height;
    unsigned long size = palette->full ? pixels * 4u : pixels * (palette->alpha ? 2u : 1u);
    unsigned i;

    if (width == 0 || height == 0 || width > MAX_SIDE || height > MAX_SIDE ||
        pixels > 0xffffffffu / 4u)
        return 0;
    memset(header, 0, BLP_HEADER_SIZE);
    memcpy(header, "BLP2", 4);
    put32(header + 4, 1);
    header[8] = palette->full ? 3 : 1;
    header[9] = palette->alpha ? 8 : 0;
    header[10] = 8;
    put32(header + 12, width);
    put32(header + 16, height);
    put32(header + 20, BLP_HEADER_SIZE);
    put32(header + 84, (uint32_t)size);
    if (palette->full)
        return 1;
    /* BGRA entries. The alpha channel after the indices is what counts, but
       Pillow reads palette alpha, so store it there too. */
    for (i = 0; i < palette->count; i++) {
        uint32_t c = palette->colours[i];
        uint8_t *p = header + PALETTE_OFFSET + i * 4u;
        p[0] = (uint8_t)(c >> 8);
        p[1] = (uint8_t)(c >> 16);
        p[2] = (uint8_t)(c >> 24);
        p[3] = (uint8_t)c;
    }
    return 1;
}

unsigned blp_row_size(const struct blp_palette *palette, unsigned width)
{
    return palette->full ? width * 4u : width;
}

void blp_encode_row(const struct blp_palette *palette, const uint8_t *rgba,
                    unsigned width, uint8_t *output)
{
    unsigned x;

    for (x = 0; x < width; x++, rgba += 4) {
        if (palette->full) {
            *output++ = rgba[2];
            *output++ = rgba[1];
            *output++ = rgba[0];
            *output++ = rgba[3];
        } else {
            *output++ = (uint8_t)(palette->table[find(palette, pack(rgba))] - 1u);
        }
    }
}

void blp_encode_alpha_row(const uint8_t *rgba, unsigned width, uint8_t *output)
{
    unsigned x;

    for (x = 0; x < width; x++)
        output[x] = rgba[x * 4u + 3u];
}
