#include "encode.h"
#include <string.h>

#define MAX_PIXELS (16u * 1024u * 1024u)
#define LAYER_NAME "Layer"

static void put16(uint8_t *p, unsigned v, int big_endian)
{
    p[big_endian ? 0 : 1] = (uint8_t)(v >> 8);
    p[big_endian ? 1 : 0] = (uint8_t)v;
}

static void put32(uint8_t *p, uint32_t v, int big_endian)
{
    put16(p + (big_endian ? 0 : 2), v >> 16, big_endian);
    put16(p + (big_endian ? 2 : 0), v & 0xffffu, big_endian);
}

int lunapaint_native_big_endian(void)
{
    const uint16_t one = 1;
    return *(const uint8_t *)&one == 0;
}

enum codec_result lunapaint_make_header(unsigned width, unsigned height,
                                        int big_endian,
                                        uint8_t out[LUNAPAINT_WRITE_HEADER])
{
    if (width == 0 || height == 0 || width > 0x7fffu || height > 0x7fffu)
        return CODEC_INVALID;
    if ((uint64_t)width * height > MAX_PIXELS)
        return CODEC_TOO_LARGE;
    /* Version, then empty project name and author. */
    memset(out, 0, LUNAPAINT_WRITE_HEADER);
    memcpy(out, "Lunapaint_v1", 12);
    put16(out + 272, width, big_endian);
    put16(out + 274, height, big_endian);
    put16(out + 276, 1, big_endian);        /* layers */
    put16(out + 278, 1, big_endian);        /* frames */
    put16(out + 280, 3, big_endian);        /* objects: three per layer */
    /* A one-byte description, as Lunapaint writes it. */
    put32(out + 282, 1, big_endian);
    out[286] = ' ';
    return CODEC_OK;
}

size_t lunapaint_row_size(unsigned width)
{
    return (size_t)width * 8u;
}

void lunapaint_encode_row(const uint8_t *rgba, unsigned width, int big_endian,
                          uint8_t *out)
{
    unsigned x;
    for (x = 0; x < width; x++, rgba += 4, out += 8) {
        /* A 64-bit word with red on top and alpha at the bottom. */
        unsigned i;
        for (i = 0; i < 4; i++) {
            unsigned v = rgba[i] * 257u;
            put16(out + (big_endian ? i * 2u : 6u - i * 2u), v, big_endian);
        }
    }
}

static uint8_t *object(uint8_t *p, uint32_t type, uint32_t length,
                       int big_endian)
{
    put32(p, type, big_endian);
    put32(p + 4, 0, big_endian);            /* layer */
    put32(p + 8, 0, big_endian);            /* frame */
    put32(p + 12, length, big_endian);
    return p + 16;
}

void lunapaint_make_trailer(int big_endian,
                            uint8_t out[LUNAPAINT_WRITE_TRAILER])
{
    /* The header promises three objects and Lunapaint reads that many, so
       the layer gets a name too. */
    uint8_t *p = object(out, 1, 1, big_endian);
    *p++ = 100;                             /* opacity, percent */
    p = object(p, 2, 1, big_endian);
    *p++ = 1;                               /* visible */
    p = object(p, 3, sizeof LAYER_NAME - 1, big_endian);
    memcpy(p, LAYER_NAME, sizeof LAYER_NAME - 1);
}
