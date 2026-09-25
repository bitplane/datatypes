#ifndef BITPLANE_COMMON_ATARIST_H
#define BITPLANE_COMMON_ATARIST_H
/* Atari ST screen helpers for codecs: palette words and interleaved bitplanes. */
#include <stdint.h>

/* 8-bit level of one palette gun: 3-bit ST, or 4-bit STE, which keeps its
   extra, least significant bit in bit 3. */
static inline uint8_t st_level(unsigned nibble, int ste)
{
    if (ste)
        return (uint8_t)((((nibble & 7u) << 1) | ((nibble >> 3) & 1u)) * 17u);
    /* round(v * 255 / 7), as netpbm scales maxval 7. */
    return (uint8_t)(((nibble & 7u) * 255u + 3u) / 7u);
}

/* The palette gun that st_level turns into level, or -1 if there is none. */
static inline int st_nibble(uint8_t level, int ste)
{
    unsigned n;

    for (n = 0; n < 16; n++)
        if ((ste || n < 8) && st_level(n, ste) == level)
            return (int)n;
    return -1;
}

/* Whether any of count big-endian palette words has an STE fourth bit. Pass
   only the colours the resolution uses: the rest often hold garbage. */
static inline int st_is_ste(const uint8_t *words, unsigned count)
{
    unsigned i;

    for (i = 0; i < count; i++)
        if ((words[i * 2u] & 0x08u) || (words[i * 2u + 1u] & 0x88u))
            return 1;
    return 0;
}

/* Converts count big-endian palette words to RGB, as STE if any uses a fourth
   bit and as ST otherwise. */
static inline void st_palette(const uint8_t *words, unsigned count,
                              uint8_t rgb[][3])
{
    int ste = st_is_ste(words, count);
    unsigned i;

    for (i = 0; i < count; i++) {
        rgb[i][0] = st_level(words[i * 2u], ste);
        rgb[i][1] = st_level(words[i * 2u + 1u] >> 4, ste);
        rgb[i][2] = st_level(words[i * 2u + 1u], ste);
    }
}

/* Colour index of pixel x of a screen line stored as groups of 16 pixels,
   one big-endian word per plane, as the ST's low, medium and high
   resolutions are. */
static inline unsigned st_pixel(const uint8_t *line, unsigned planes, unsigned x)
{
    const uint8_t *group = line + (x / 16u) * planes * 2u;
    unsigned bit = 15u - x % 16u, index = 0, p;

    for (p = 0; p < planes; p++) {
        unsigned word = ((unsigned)group[p * 2u] << 8) | group[p * 2u + 1u];
        index |= ((word >> bit) & 1u) << p;
    }
    return index;
}
#endif
