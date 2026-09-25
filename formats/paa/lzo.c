#include "lzo.h"

/* An LZO1X stream alternates literal runs and matches. Each match
   instruction's low two bits (its "state") give the number of literals, 0
   to 3, that follow it before the next instruction; state 0 means the next
   instruction starts a longer literal run, or is a match. */

struct stream {
    const uint8_t *in;
    size_t in_len, ip;
    uint8_t *out;
    size_t out_len, op;
};

static int next(struct stream *s, unsigned *byte)
{
    if (s->ip >= s->in_len)
        return 0;
    *byte = s->in[s->ip++];
    return 1;
}

/* A length field of zero is extended by following bytes: each zero adds 255,
   and the first non-zero one ends it. base is the field's largest value. */
static int extend(struct stream *s, size_t base, size_t *length)
{
    unsigned byte;
    size_t n = base;
    for (;;) {
        if (!next(s, &byte))
            return 0;
        if (byte != 0)
            break;
        n += 255;
        /* No run can be longer than the output, so stop before overflow. */
        if (n > s->out_len)
            return 0;
    }
    *length = n + byte;
    return 1;
}

static enum codec_result literals(struct stream *s, size_t n)
{
    if (s->in_len - s->ip < n)
        return CODEC_INVALID;
    if (s->out_len - s->op < n)
        return CODEC_INVALID;
    while (n--)
        s->out[s->op++] = s->in[s->ip++];
    return CODEC_OK;
}

/* Copy n bytes from distance back; the source may overlap the destination. */
static enum codec_result copy(struct stream *s, size_t distance, size_t n)
{
    if (distance == 0 || distance > s->op)
        return CODEC_INVALID;
    if (s->out_len - s->op < n)
        return CODEC_INVALID;
    while (n--) {
        s->out[s->op] = s->out[s->op - distance];
        s->op++;
    }
    return CODEC_OK;
}

static int le16(struct stream *s, size_t *value)
{
    if (s->in_len - s->ip < 2)
        return 0;
    *value = (size_t)s->in[s->ip] | (size_t)s->in[s->ip + 1] << 8;
    s->ip += 2;
    return 1;
}

enum codec_result paa_lzo_expand(const uint8_t *in, size_t in_len,
                                 uint8_t *out, size_t out_len)
{
    struct stream s;
    enum codec_result r;
    unsigned t, state = 0, byte;
    size_t n, distance, field;
    /* A short match (t < 16) means different things after a literal run of
       four or more (a three-byte match up to 0xC00 back) and after a match or
       a short run from its state bits (a two-byte match up to 0x400 back). */
    int after_run = 0;

    s.in = in; s.in_len = in_len; s.ip = 0;
    s.out = out; s.out_len = out_len; s.op = 0;

    if (!next(&s, &t))
        return CODEC_INVALID;
    if (t > 17) {
        /* The first byte may start the stream with a literal run. */
        n = t - 17;
        if ((r = literals(&s, n)) != CODEC_OK)
            return r;
        state = n < 4 ? (unsigned)n : 0;
        after_run = n >= 4;
        if (!next(&s, &t))
            return CODEC_INVALID;
    }
    for (;;) {
        if (t < 16 && state == 0 && !after_run) {
            /* Literal run of 4 or more. */
            n = t;
            if (n == 0 && !extend(&s, 15, &n))
                return CODEC_INVALID;
            if ((r = literals(&s, n + 3)) != CODEC_OK)
                return r;
            after_run = 1;
            if (!next(&s, &t))
                return CODEC_INVALID;
            continue;
        }
        if (t >= 64) {
            /* M2: 3 to 8 bytes, up to 0x800 back. */
            if (!next(&s, &byte))
                return CODEC_INVALID;
            distance = 1 + ((t >> 2) & 7) + ((size_t)byte << 3);
            n = (t >> 5) + 1;
            state = t & 3;
        } else if (t >= 32) {
            /* M3: up to 0x4000 back. */
            n = t & 31;
            if (n == 0 && !extend(&s, 31, &n))
                return CODEC_INVALID;
            if (!le16(&s, &field))
                return CODEC_INVALID;
            distance = 1 + (field >> 2);
            n += 2;
            state = field & 3;
        } else if (t >= 16) {
            /* M4: 0x4000 to 0xBFFF back; distance 0x4000 is the end marker. */
            n = t & 7;
            if (n == 0 && !extend(&s, 7, &n))
                return CODEC_INVALID;
            if (!le16(&s, &field))
                return CODEC_INVALID;
            distance = ((size_t)(t & 8) << 11) + (field >> 2);
            if (distance == 0)
                return s.op == out_len ? CODEC_OK : CODEC_INVALID;
            distance += 0x4000;
            n += 2;
            state = field & 3;
        } else {
            /* M1: a short match straight after literals. */
            if (!next(&s, &byte))
                return CODEC_INVALID;
            distance = 1 + (t >> 2) + ((size_t)byte << 2);
            n = 2;
            if (after_run && state == 0) {
                distance += 0x800;
                n = 3;
            }
            state = t & 3;
        }
        if ((r = copy(&s, distance, n)) != CODEC_OK)
            return r;
        after_run = 0;
        if (state != 0 && (r = literals(&s, state)) != CODEC_OK)
            return r;
        if (!next(&s, &t))
            return CODEC_INVALID;
    }
}
