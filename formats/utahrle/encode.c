#include "encode.h"

const uint8_t utahrle_end[2] = {0x07, 0x00};

unsigned utahrle_row_needs(const uint8_t *rgba, unsigned width)
{
    unsigned needs = 0, x;
    for (x = 0; x < width; x++, rgba += 4) {
        if (rgba[0] != rgba[1] || rgba[0] != rgba[2])
            needs |= UTAHRLE_NEEDS_COLOUR;
        if (rgba[3] != 255)
            needs |= UTAHRLE_NEEDS_ALPHA;
    }
    return needs;
}

static void put16(uint8_t *p, unsigned value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

int utahrle_make_header(unsigned width, unsigned height, unsigned needs,
                        uint8_t header[UTAHRLE_HEADER_SIZE])
{
    if (width == 0 || height == 0 || width > 65535u || height > 65535u)
        return 0;
    header[0] = 0x52;
    header[1] = 0xcc;
    put16(header + 2, 0);
    put16(header + 4, 0);
    put16(header + 6, width);
    put16(header + 8, height);
    /* No background: every pixel is written. */
    header[10] = 0x02 | (needs & UTAHRLE_NEEDS_ALPHA ? 0x04 : 0);
    header[11] = needs ? 3 : 1;
    header[12] = 8;
    header[13] = 0;
    header[14] = 8;
    header[15] = 0;
    return 1;
}

size_t utahrle_row_capacity(unsigned width)
{
    /* Per channel: SetColor, then runs of 3 or more pixels cost at most
       2 bytes a pixel and each literal at most 5 bytes more than its
       length, with one more literal than runs. */
    return 2u + 4u * (2u + 4u * (size_t)width + 5u);
}

struct out {
    uint8_t *p;
    size_t used, capacity;
};

static int emit(struct out *o, unsigned op, unsigned count)
{
    unsigned operand = count - 1u;
    if (operand < 256u) {
        if (o->capacity - o->used < 2u)
            return 0;
        o->p[o->used++] = (uint8_t)op;
        o->p[o->used++] = (uint8_t)operand;
    } else {
        if (o->capacity - o->used < 4u)
            return 0;
        o->p[o->used++] = (uint8_t)(op | 0x40u);
        o->p[o->used++] = 0;
        put16(o->p + o->used, operand);
        o->used += 2u;
    }
    return 1;
}

static int literal(struct out *o, const uint8_t *rgba, unsigned slot,
                   unsigned start, unsigned end)
{
    unsigned x;
    if (start == end)
        return 1;
    if (!emit(o, 0x05, end - start) ||
        o->capacity - o->used < (size_t)(end - start) + 1u)
        return 0;
    for (x = start; x < end; x++)
        o->p[o->used++] = rgba[x * 4u + slot];
    if ((end - start) & 1u)
        o->p[o->used++] = 0;
    return 1;
}

static int channel(struct out *o, const uint8_t *rgba, unsigned width,
                   unsigned id, unsigned slot)
{
    unsigned x = 0, start = 0, run;

    if (o->capacity - o->used < 2u)
        return 0;
    o->p[o->used++] = 0x02;
    o->p[o->used++] = (uint8_t)id;
    while (x < width) {
        for (run = 1; x + run < width &&
             rgba[(x + run) * 4u + slot] == rgba[x * 4u + slot]; run++)
            ;
        if (run < 3u) {
            x += run;
            continue;
        }
        if (!literal(o, rgba, slot, start, x) || !emit(o, 0x06, run) ||
            o->capacity - o->used < 2u)
            return 0;
        o->p[o->used++] = rgba[x * 4u + slot];
        o->p[o->used++] = 0;
        x += run;
        start = x;
    }
    return literal(o, rgba, slot, start, width);
}

size_t utahrle_encode_row(const uint8_t *rgba, unsigned width, unsigned needs,
                          int first, uint8_t *output, size_t capacity)
{
    struct out o;
    unsigned c;

    o.p = output;
    o.used = 0;
    o.capacity = capacity;
    if (!first) {
        if (capacity < 2u)
            return 0;
        o.p[o.used++] = 0x01; /* SkipLines 1 */
        o.p[o.used++] = 1;
    }
    if ((needs & UTAHRLE_NEEDS_ALPHA) && !channel(&o, rgba, width, 255, 3))
        return 0;
    for (c = 0; c < (needs ? 3u : 1u); c++)
        if (!channel(&o, rgba, width, c, c))
            return 0;
    return o.used;
}
