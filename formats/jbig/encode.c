#include "encode.h"
#include <stdlib.h>
#include <string.h>

enum { ESC = 0xff, SDNORM = 0x02 };

/* The pseudo-pixel contexts, laid out as in decode.c. */
#define TP_CONTEXT_THREE 0x0e5u
#define TP_CONTEXT_TWO 0x195u

static void put32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)(value >> 24);
    p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8);
    p[3] = (uint8_t)value;
}

static int px(const uint8_t *row, long x, unsigned width)
{
    if (row == NULL || x < 0 || (unsigned long)x >= width)
        return 0;
    return row[x >> 3] >> (7 - (x & 7)) & 1;
}

static uint8_t *slot(struct jbig_encoder *e, unsigned back)
{
    return e->lines + (size_t)((e->y + 3u - back) % 3u) * e->stride;
}

void jbig_make_header(uint8_t header[JBIG_HEADER_SIZE], unsigned width, unsigned height,
                      unsigned options)
{
    memset(header, 0, JBIG_HEADER_SIZE);
    header[2] = 1;
    put32(header + 4, width);
    put32(header + 8, height);
    put32(header + 12, height);
    header[19] = (uint8_t)options;
}

int jbig_encoder_init(struct jbig_encoder *e, unsigned width, unsigned height,
                      unsigned options, qm_sink *sink, void *sink_state)
{
    e->width = width;
    e->height = height;
    e->y = 0;
    e->options = options;
    e->stride = (width + 7u) / 8u;
    e->lines = calloc(3, e->stride);
    memset(e->contexts, 0, sizeof e->contexts);
    e->ltp = 1;
    qm_encode_init(&e->qm, sink, sink_state);
    return e->lines != NULL;
}

/* Code the line in the current slot. */
static void encode_line(struct jbig_encoder *e)
{
    unsigned w = e->width;
    const uint8_t *r0 = slot(e, 0);
    const uint8_t *r1 = e->y >= 1 ? slot(e, 1) : NULL;
    const uint8_t *r2 = e->y >= 2 ? slot(e, 2) : NULL;
    int two = (e->options & JBIG_TWO_LINE) != 0;
    long x;

    e->y++;
    if (e->options & JBIG_TYPICAL) {
        /* LNTP: the line differs from the one above, or from white at the top. */
        int lntp = 0;
        size_t i;
        for (i = 0; i < e->stride && !lntp; i++)
            lntp = r0[i] != (r1 != NULL ? r1[i] : 0);
        qm_encode(&e->qm, &e->contexts[two ? TP_CONTEXT_TWO : TP_CONTEXT_THREE],
                  !(lntp ^ e->ltp));
        e->ltp = lntp;
        if (!lntp)
            return;
    }
    for (x = 0; x < (long)w; x++) {
        unsigned cx;
        if (two)
            cx = (unsigned)(px(r1, x - 3, w) << 9 | px(r1, x - 2, w) << 8 |
                            px(r1, x - 1, w) << 7 | px(r1, x, w) << 6 |
                            px(r1, x + 1, w) << 5 | px(r1, x + 2, w) << 4 |
                            px(r0, x - 4, w) << 3 | px(r0, x - 3, w) << 2 |
                            px(r0, x - 2, w) << 1 | px(r0, x - 1, w));
        else
            cx = (unsigned)(px(r2, x - 1, w) << 9 | px(r2, x, w) << 8 |
                            px(r2, x + 1, w) << 7 | px(r1, x - 2, w) << 6 |
                            px(r1, x - 1, w) << 5 | px(r1, x, w) << 4 |
                            px(r1, x + 1, w) << 3 | px(r1, x + 2, w) << 2 |
                            px(r0, x - 2, w) << 1 | px(r0, x - 1, w));
        qm_encode(&e->qm, &e->contexts[cx], px(r0, x, w));
    }
}

void jbig_encode_bits(struct jbig_encoder *e, const uint8_t *bits)
{
    uint8_t *line = slot(e, 0);
    memcpy(line, bits, e->stride);
    /* Clear the padding, so that TP compares lines by bytes. */
    if (e->width % 8u)
        line[e->stride - 1] &= (uint8_t)(0xff00u >> (e->width % 8u));
    encode_line(e);
}

static unsigned over_white(const uint8_t *pixel, unsigned channel)
{
    unsigned a = pixel[3];
    return (pixel[channel] * a + 255u * (255u - a) + 127u) / 255u;
}

void jbig_encode_rgba(struct jbig_encoder *e, const uint8_t *rgba)
{
    uint8_t *line = slot(e, 0);
    unsigned x;

    memset(line, 0, e->stride);
    for (x = 0; x < e->width; x++) {
        const uint8_t *p = rgba + (size_t)x * 4u;
        unsigned luma = 77u * over_white(p, 0) + 150u * over_white(p, 1) +
                        29u * over_white(p, 2);
        if (luma < 128u * 256u)
            line[x / 8u] |= (uint8_t)(0x80u >> (x % 8u));
    }
    encode_line(e);
}

int jbig_encoder_end(struct jbig_encoder *e)
{
    int ok = qm_encode_flush(&e->qm) && e->qm.sink(e->qm.sink_state, ESC) &&
             e->qm.sink(e->qm.sink_state, SDNORM);
    jbig_encoder_free(e);
    return ok;
}

void jbig_encoder_free(struct jbig_encoder *e)
{
    free(e->lines);
    e->lines = NULL;
}
