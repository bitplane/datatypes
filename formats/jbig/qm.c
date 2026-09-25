#include "qm.h"

struct qm_state { uint16_t lsz; uint8_t nlps, nmps, swtch; };

/* Table 24 of T.82: LSZ, NLPS, NMPS and SWTCH for each state. */
static const struct qm_state states[113] = {
    { 0x5a1d, 1, 1, 1 }, { 0x2586, 14, 2, 0 }, { 0x1114, 16, 3, 0 },
    { 0x080b, 18, 4, 0 }, { 0x03d8, 20, 5, 0 }, { 0x01da, 23, 6, 0 },
    { 0x00e5, 25, 7, 0 }, { 0x006f, 28, 8, 0 }, { 0x0036, 30, 9, 0 },
    { 0x001a, 33, 10, 0 }, { 0x000d, 35, 11, 0 }, { 0x0006, 9, 12, 0 },
    { 0x0003, 10, 13, 0 }, { 0x0001, 12, 13, 0 }, { 0x5a7f, 15, 15, 1 },
    { 0x3f25, 36, 16, 0 }, { 0x2cf2, 38, 17, 0 }, { 0x207c, 39, 18, 0 },
    { 0x17b9, 40, 19, 0 }, { 0x1182, 42, 20, 0 }, { 0x0cef, 43, 21, 0 },
    { 0x09a1, 45, 22, 0 }, { 0x072f, 46, 23, 0 }, { 0x055c, 48, 24, 0 },
    { 0x0406, 49, 25, 0 }, { 0x0303, 51, 26, 0 }, { 0x0240, 52, 27, 0 },
    { 0x01b1, 54, 28, 0 }, { 0x0144, 56, 29, 0 }, { 0x00f5, 57, 30, 0 },
    { 0x00b7, 59, 31, 0 }, { 0x008a, 60, 32, 0 }, { 0x0068, 62, 33, 0 },
    { 0x004e, 63, 34, 0 }, { 0x003b, 32, 35, 0 }, { 0x002c, 33, 9, 0 },
    { 0x5ae1, 37, 37, 1 }, { 0x484c, 64, 38, 0 }, { 0x3a0d, 65, 39, 0 },
    { 0x2ef1, 67, 40, 0 }, { 0x261f, 68, 41, 0 }, { 0x1f33, 69, 42, 0 },
    { 0x19a8, 70, 43, 0 }, { 0x1518, 72, 44, 0 }, { 0x1177, 73, 45, 0 },
    { 0x0e74, 74, 46, 0 }, { 0x0bfb, 75, 47, 0 }, { 0x09f8, 77, 48, 0 },
    { 0x0861, 78, 49, 0 }, { 0x0706, 79, 50, 0 }, { 0x05cd, 48, 51, 0 },
    { 0x04de, 50, 52, 0 }, { 0x040f, 50, 53, 0 }, { 0x0363, 51, 54, 0 },
    { 0x02d4, 52, 55, 0 }, { 0x025c, 53, 56, 0 }, { 0x01f8, 54, 57, 0 },
    { 0x01a4, 55, 58, 0 }, { 0x0160, 56, 59, 0 }, { 0x0125, 57, 60, 0 },
    { 0x00f6, 58, 61, 0 }, { 0x00cb, 59, 62, 0 }, { 0x00ab, 61, 63, 0 },
    { 0x008f, 61, 32, 0 }, { 0x5b12, 65, 65, 1 }, { 0x4d04, 80, 66, 0 },
    { 0x412c, 81, 67, 0 }, { 0x37d8, 82, 68, 0 }, { 0x2fe8, 83, 69, 0 },
    { 0x293c, 84, 70, 0 }, { 0x2379, 86, 71, 0 }, { 0x1edf, 87, 72, 0 },
    { 0x1aa9, 87, 73, 0 }, { 0x174e, 72, 74, 0 }, { 0x1424, 72, 75, 0 },
    { 0x119c, 74, 76, 0 }, { 0x0f6b, 74, 77, 0 }, { 0x0d51, 75, 78, 0 },
    { 0x0bb6, 77, 79, 0 }, { 0x0a40, 77, 48, 0 }, { 0x5832, 80, 81, 1 },
    { 0x4d1c, 88, 82, 0 }, { 0x438e, 89, 83, 0 }, { 0x3bdd, 90, 84, 0 },
    { 0x34ee, 91, 85, 0 }, { 0x2eae, 92, 86, 0 }, { 0x299a, 93, 87, 0 },
    { 0x2516, 86, 71, 0 }, { 0x5570, 88, 89, 1 }, { 0x4ca9, 95, 90, 0 },
    { 0x44d9, 96, 91, 0 }, { 0x3e22, 97, 92, 0 }, { 0x3824, 99, 93, 0 },
    { 0x32b4, 99, 94, 0 }, { 0x2e17, 93, 86, 0 }, { 0x56a8, 95, 96, 1 },
    { 0x4f46, 101, 97, 0 }, { 0x47e5, 102, 98, 0 }, { 0x41cf, 103, 99, 0 },
    { 0x3c3d, 104, 100, 0 }, { 0x375e, 99, 93, 0 }, { 0x5231, 105, 102, 0 },
    { 0x4c0f, 106, 103, 0 }, { 0x4639, 107, 104, 0 }, { 0x415e, 103, 99, 0 },
    { 0x5627, 105, 106, 1 }, { 0x50e7, 108, 107, 0 }, { 0x4b85, 109, 103, 0 },
    { 0x5597, 110, 109, 0 }, { 0x504f, 111, 107, 0 }, { 0x5a10, 110, 111, 1 },
    { 0x5522, 112, 109, 0 }, { 0x59eb, 112, 111, 1 },
};

/* A context byte past state 112 can't arise from a zeroed start, but keep
   one in range regardless. */
static const struct qm_state *state_of(uint8_t context)
{
    unsigned index = context & 0x7fu;
    return &states[index < 113 ? index : 112];
}

static void byte_in(struct qm_decoder *d)
{
    uint32_t byte = 0;
    if (d->next < d->end) {
        byte = *d->next++;
        /* Whoever found the stripe checked that each 0xff has its 0x00. */
        if (byte == 0xffu)
            d->next++;
    }
    d->c += byte << 8;
    d->ct = 8;
}

void qm_decode_init(struct qm_decoder *d, const uint8_t *pscd, size_t length)
{
    d->next = pscd;
    d->end = pscd + length;
    d->c = 0;
    byte_in(d);
    d->c <<= 8;
    byte_in(d);
    d->c <<= 8;
    byte_in(d);
    d->a = 0x10000u;
}

/* C holds CHIGH in its top 16 bits and CLOW below, as in Table 25. */
int qm_decode(struct qm_decoder *d, uint8_t *context)
{
    const struct qm_state *s = state_of(*context);
    int mps = *context >> 7, pixel;

    d->a -= s->lsz;
    if ((d->c >> 16) < d->a) {
        if (d->a >= 0x8000u)
            return mps;
        if (d->a < s->lsz) {
            pixel = !mps;
            *context = (uint8_t)((s->swtch ? pixel : mps) << 7 | s->nlps);
        } else {
            pixel = mps;
            *context = (uint8_t)(mps << 7 | s->nmps);
        }
    } else {
        d->c -= d->a << 16;
        if (d->a < s->lsz) {
            pixel = mps;
            *context = (uint8_t)(mps << 7 | s->nmps);
        } else {
            pixel = !mps;
            *context = (uint8_t)((s->swtch ? pixel : mps) << 7 | s->nlps);
        }
        d->a = s->lsz;
    }
    do {
        if (d->ct == 0)
            byte_in(d);
        d->a <<= 1;
        d->c <<= 1;
        d->ct--;
    } while (d->a < 0x8000u);
    if (d->ct == 0)
        byte_in(d);
    return pixel;
}

/* Pass a byte of stripe coded data on, stuffed, holding zeros back so that
   the trailing ones can be dropped. */
static void emit(struct qm_encoder *e, unsigned byte)
{
    if (e->first) {
        e->first = 0;
        return;
    }
    if (byte == 0) {
        e->zeros++;
        return;
    }
    for (; e->zeros > 0; e->zeros--)
        if (!e->sink(e->sink_state, 0))
            e->failed = 1;
    if (!e->sink(e->sink_state, (uint8_t)byte))
        e->failed = 1;
    if (byte == 0xffu && !e->sink(e->sink_state, 0))
        e->failed = 1;
}

/* Write the buffered byte, and the 0xff bytes stacked after it, with or
   without a carry into them. */
static void emit_buffer(struct qm_encoder *e, int carry)
{
    emit(e, (e->buffer + (unsigned)carry) & 0xffu);
    for (; e->sc > 0; e->sc--)
        emit(e, carry ? 0x00u : 0xffu);
}

static void byte_out(struct qm_encoder *e)
{
    uint32_t temp = e->c >> 19;
    if (temp > 0xffu) {
        emit_buffer(e, 1);
        e->buffer = temp & 0xffu;
    } else if (temp == 0xffu) {
        e->sc++;
    } else {
        emit_buffer(e, 0);
        e->buffer = temp;
    }
    e->c &= 0x7ffffu;
    e->ct = 8;
}

void qm_encode_init(struct qm_encoder *e, qm_sink *sink, void *sink_state)
{
    e->c = 0;
    e->a = 0x10000u;
    e->ct = 11;
    e->sc = 0;
    e->zeros = 0;
    e->buffer = 0;
    e->first = 1;
    e->failed = 0;
    e->sink = sink;
    e->sink_state = sink_state;
}

void qm_encode(struct qm_encoder *e, uint8_t *context, int pixel)
{
    const struct qm_state *s = state_of(*context);
    int mps = *context >> 7;

    e->a -= s->lsz;
    if (pixel == mps) {
        if (e->a >= 0x8000u)
            return;
        if (e->a < s->lsz) {
            e->c += e->a;
            e->a = s->lsz;
        }
        *context = (uint8_t)(mps << 7 | s->nmps);
    } else {
        if (e->a >= s->lsz) {
            e->c += e->a;
            e->a = s->lsz;
        }
        *context = (uint8_t)((s->swtch ? !mps : mps) << 7 | s->nlps);
    }
    do {
        e->a <<= 1;
        e->c <<= 1;
        if (--e->ct == 0)
            byte_out(e);
    } while (e->a < 0x8000u);
}

int qm_encode_flush(struct qm_encoder *e)
{
    uint32_t temp = (e->a - 1u + e->c) & 0xffff0000u;
    e->c = temp < e->c ? temp + 0x8000u : temp;
    e->c <<= e->ct;
    emit_buffer(e, e->c > 0x7ffffffu);
    emit(e, (e->c >> 19) & 0xffu);
    emit(e, (e->c >> 11) & 0xffu);
    return !e->failed;
}
