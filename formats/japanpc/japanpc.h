#ifndef BITPLANE_JAPANPC_JAPANPC_H
#define BITPLANE_JAPANPC_JAPANPC_H
/* Shared by the MAG, MKI, Pi and PIC decoders. */
#include <stddef.h>
#include <stdint.h>
#include "common/result.h"

#define JP_MAX_PIXELS (16u * 1024u * 1024u)
#define JP_MAX_SIDE 65535u

/* Most-significant-bit-first reader. A read past the end returns -1. */
struct jp_bits {
    const uint8_t *data;
    size_t pos, size;
    unsigned byte, left;
};

static inline void jp_bits_init(struct jp_bits *b, const uint8_t *data,
                                size_t pos, size_t size)
{
    b->data = data;
    b->pos = pos;
    b->size = size;
    b->left = 0;
    b->byte = 0;
}

static inline int jp_bit(struct jp_bits *b)
{
    if (b->left == 0) {
        if (b->pos >= b->size)
            return -1;
        b->byte = b->data[b->pos++];
        b->left = 8;
    }
    b->left--;
    return (int)(b->byte >> b->left) & 1;
}

/* Up to 24 bits. */
static inline long jp_read(struct jp_bits *b, unsigned count)
{
    long value = 0;
    while (count-- > 0) {
        int bit = jp_bit(b);
        if (bit < 0)
            return -1;
        value = value << 1 | bit;
    }
    return value;
}

/* How the machine's DAC shows 8-bit palette values. */
enum jp_dac {
    JP_DAC_8,       /* as stored */
    JP_DAC_4,       /* 4 bits per channel (PC-98, PC-88) */
    JP_DAC_3,       /* 3 bits per channel (MSX2) */
    JP_DAC_565,     /* 5, 6 and 5 bits (PC-88VA) */
    JP_DAC_X68K,    /* 5 bits and a shared intensity bit */
    JP_DAC_TOWNS    /* 5 bits (FM TOWNS) */
};

uint32_t jp_dac_colour(enum jp_dac dac, unsigned r, unsigned g, unsigned b);
/* X68000 16-bit colour: GGGGGRRRRRBBBBBI. */
uint32_t jp_x68k_colour(unsigned c);
/* PC-88VA 16-bit colour: GGGGGGRRRRRBBBBB. */
uint32_t jp_g6r5b5_colour(unsigned c);
/* Colours 0xRRGGBB from count 3-byte entries, red at red, green at green. */
void jp_palette(uint32_t *palette, const uint8_t *entries, unsigned count,
                unsigned red, unsigned green, enum jp_dac dac);

/* The DAC named by a 4-character machine code, as Pi and MKI store it, and
   whether its lines are doubled: always for PC-8801, when tall is set for
   others that have a 200-line mode. */
enum jp_dac jp_machine_dac(const uint8_t *code, int tall, unsigned colours,
                           unsigned *repeat_y);

/* Decoded pixels at the machine's resolution, 0xRRGGBB, and how many times
   to repeat each across and down for the intended aspect. */
struct jp_canvas {
    unsigned width, height, repeat_x, repeat_y;
    uint32_t *rgb;
};

/* Check the size once scaled and allocate the pixels. */
enum codec_result jp_canvas_init(struct jp_canvas *canvas, unsigned width,
                                 unsigned height, unsigned repeat_x,
                                 unsigned repeat_y);

enum codec_result jp_decode_mag(const uint8_t *data, size_t size,
                                struct jp_canvas *canvas);
enum codec_result jp_decode_mki(const uint8_t *data, size_t size,
                                struct jp_canvas *canvas);
enum codec_result jp_decode_pi(const uint8_t *data, size_t size,
                               struct jp_canvas *canvas);
enum codec_result jp_decode_pic(const uint8_t *data, size_t size,
                                struct jp_canvas *canvas);

#endif
