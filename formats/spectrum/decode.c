#include "decode.h"
#include <stdlib.h>
#include <string.h>

/* An SPC bitmap holds lines 1-199 of each plane in turn. */
#define SPC_PLANE_SIZE (199u * 40u)

static unsigned be16(const uint8_t *p)
{
    return ((unsigned)p[0] << 8) | p[1];
}

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

uint8_t spectrum_level(unsigned nibble, int ste)
{
    if (ste)
        return (uint8_t)((((nibble & 7u) << 1) | ((nibble >> 3) & 1u)) * 17u);
    /* round(v * 255 / 7), as netpbm scales maxval 7. */
    return (uint8_t)(((nibble & 7u) * 255u + 3u) / 7u);
}

unsigned spectrum_slot(unsigned c, unsigned x)
{
    /* The display reloads each colour twice per line, staggered by index. */
    unsigned x1 = (c & 1u) ? 10u * c - 5u : 10u * c + 1u;

    if (x < x1)
        return c;
    return x < x1 + 160u ? c + 16u : c + 32u;
}

void spectrum_free(struct spectrum_image *image)
{
    free(image->rgba);
    image->rgba = NULL;
    image->width = image->height = 0;
}

/* Unpack the SPC bitmap into lines 1-199 of screen. */
static enum codec_result unpack_bitmap(const uint8_t *in, size_t length,
                                       uint8_t *screen)
{
    size_t pos = 0, n = 0;

    while (n < 4u * SPC_PLANE_SIZE) {
        unsigned count, i;
        int literal;
        if (pos >= length)
            return CODEC_TRUNCATED;
        literal = in[pos] < 128u;
        count = literal ? in[pos] + 1u : 258u - in[pos];
        pos++;
        if (length - pos < (literal ? count : 1u))
            return CODEC_TRUNCATED;
        /* A run past the last plane is cut off. */
        for (i = 0; i < count && n < 4u * SPC_PLANE_SIZE; i++, n++) {
            /* A plane's bytes fill its word of each 16-pixel group in turn. */
            size_t plane = n / SPC_PLANE_SIZE, k = n % SPC_PLANE_SIZE;
            screen[160u + plane * 2u + (k / 2u) * 8u + (k & 1u)] =
                in[literal ? pos + i : pos];
        }
        pos += literal ? count : 1u;
    }
    return CODEC_OK;
}

/* Each SPC palette is a mask whose bits 0-14 flag the colours stored, then
   those colours. Other colours, including colour 15, are black. */
static enum codec_result unpack_palettes(const uint8_t *in, size_t length,
                                         unsigned *words)
{
    size_t pos = 0;
    unsigned p, b;

    for (p = 0; p < SPU_PALETTE_WORDS / 16u; p++) {
        unsigned mask;
        if (length - pos < 2u)
            return CODEC_TRUNCATED;
        mask = be16(in + pos);
        pos += 2;
        for (b = 0; b < 16; b++) {
            unsigned word = 0;
            if (b < 15 && (mask >> b & 1u)) {
                if (length - pos < 2u)
                    return CODEC_TRUNCATED;
                word = be16(in + pos);
                pos += 2;
            }
            words[p * 16u + b] = word;
        }
    }
    return CODEC_OK;
}

static enum codec_result read_spc(const uint8_t *data, size_t length,
                                  uint8_t *screen, unsigned *words)
{
    uint32_t bitmap_length;
    enum codec_result result;

    if (length < 12)
        return CODEC_TRUNCATED;
    bitmap_length = be32(data + 4);
    /* The palette's length isn't needed: its masks say how long it is. */
    if (bitmap_length > length - 12u)
        return CODEC_TRUNCATED;
    result = unpack_bitmap(data + 12, bitmap_length, screen);
    if (result != CODEC_OK)
        return result;
    return unpack_palettes(data + 12 + bitmap_length,
                           length - 12u - bitmap_length, words);
}

static enum codec_result read_spu(const uint8_t *data, size_t length,
                                  uint8_t *screen, unsigned *words)
{
    unsigned i;

    /* Enhanced SPU files keep more bits per gun than the STE has. */
    if (length >= 4 && memcmp(data, "5BIT", 4) == 0)
        return CODEC_INVALID;
    if (length < SPU_FILE_SIZE)
        return CODEC_TRUNCATED;
    /* Line 0 is never shown, so whatever it holds is ignored. */
    memcpy(screen + 160, data + 160, SPU_SCREEN_SIZE - 160u);
    for (i = 0; i < SPU_PALETTE_WORDS; i++)
        words[i] = be16(data + SPU_SCREEN_SIZE + i * 2u);
    return CODEC_OK;
}

static void render(const uint8_t *screen, const unsigned *words, uint8_t *rgba)
{
    unsigned x, y, i;
    int ste = 0;

    /* An STE palette is recognised by a fourth bit in any of its colours. */
    for (i = 0; i < SPU_PALETTE_WORDS; i++)
        if (words[i] & 0x888u)
            ste = 1;
    for (x = 0; x < SPECTRUM_WIDTH; x++) {
        rgba[x * 4u] = rgba[x * 4u + 1u] = rgba[x * 4u + 2u] = 0;
        rgba[x * 4u + 3u] = 255;
    }
    for (y = 1; y < SPECTRUM_HEIGHT; y++) {
        const uint8_t *line = screen + y * 160u;
        const unsigned *palette = words + (y - 1u) * 48u;
        uint8_t *dst = rgba + (size_t)y * SPECTRUM_WIDTH * 4u;
        for (x = 0; x < SPECTRUM_WIDTH; x++) {
            const uint8_t *group = line + (x / 16u) * 8u;
            unsigned bit = 15u - x % 16u, c = 0, p, word;
            for (p = 0; p < 4; p++)
                c |= ((be16(group + p * 2u) >> bit) & 1u) << p;
            word = palette[spectrum_slot(c, x)];
            dst[0] = spectrum_level(word >> 8, ste);
            dst[1] = spectrum_level(word >> 4, ste);
            dst[2] = spectrum_level(word, ste);
            dst[3] = 255;
            dst += 4;
        }
    }
}

enum codec_result spectrum_decode(const uint8_t *data, size_t length,
                                  struct spectrum_image *image)
{
    uint8_t *screen;
    unsigned *words;
    enum codec_result result;

    if (image == NULL)
        return CODEC_INVALID;
    image->width = image->height = 0;
    image->rgba = NULL;
    if (data == NULL || length < 4)
        return CODEC_TRUNCATED;

    screen = calloc(SPU_SCREEN_SIZE, 1);
    words = malloc(SPU_PALETTE_WORDS * sizeof *words);
    image->rgba = malloc((size_t)SPECTRUM_WIDTH * SPECTRUM_HEIGHT * 4u);
    if (screen == NULL || words == NULL || image->rgba == NULL) {
        result = CODEC_NO_MEMORY;
    } else if (memcmp(data, "SP\0\0", 4) == 0) {
        result = read_spc(data, length, screen, words);
        /* An SPU whose line 0 happens to start with the SPC key. */
        if (result != CODEC_OK && length == SPU_FILE_SIZE)
            result = read_spu(data, length, screen, words);
    } else {
        result = read_spu(data, length, screen, words);
    }
    if (result == CODEC_OK) {
        render(screen, words, image->rgba);
        image->width = SPECTRUM_WIDTH;
        image->height = SPECTRUM_HEIGHT;
    } else {
        free(image->rgba);
        image->rgba = NULL;
    }
    free(screen);
    free(words);
    return result;
}
