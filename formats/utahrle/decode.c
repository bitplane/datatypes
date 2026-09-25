#include "decode.h"
#include <stdlib.h>
#include <string.h>

#define UTAHRLE_MAX_PIXELS (16u * 1024u * 1024u)
#define UTAHRLE_HEADER 15u
#define UTAHRLE_MAX_CMAPLEN 16u

enum {
    FLAG_CLEARFIRST = 0x01,
    FLAG_NO_BACKGROUND = 0x02,
    FLAG_ALPHA = 0x04,
    FLAG_COMMENT = 0x08
};

enum {
    OP_SKIPLINES = 1,
    OP_SETCOLOR = 2,
    OP_SKIPPIXELS = 3,
    OP_BYTEDATA = 5,
    OP_RUNDATA = 6,
    OP_EOF = 7,
    OP_LONG = 0x40
};

struct header {
    size_t width, height;
    unsigned flags, ncolors, ncmap;
    uint8_t background[3];
    uint8_t map[3][256];    /* 8-bit lookup for each channel value */
};

static unsigned le16(const uint8_t *p)
{
    return (unsigned)p[0] | ((unsigned)p[1] << 8);
}

static int is_magic(const uint8_t *p)
{
    return p[0] == 0x52 && p[1] == 0xcc;
}

/* Parse the header at *pos and leave *pos at the first opcode. */
static enum codec_result parse_header(const uint8_t *data, size_t length,
                                      size_t *pos, struct header *h)
{
    const uint8_t *p = data + *pos;
    size_t left = length - *pos, need, entries, maplen, i;
    unsigned bits, cmaplen, m;

    if (left < 2 || !is_magic(p))
        return CODEC_INVALID;
    if (left < UTAHRLE_HEADER + 1u)
        return CODEC_TRUNCATED;
    /* xpos and ypos place the image in a larger space; show just the image. */
    h->width = le16(p + 6);
    h->height = le16(p + 8);
    h->flags = p[10];
    h->ncolors = p[11];
    bits = p[12];
    h->ncmap = p[13];
    cmaplen = p[14];
    if (h->width == 0 || h->height == 0 || bits != 8 ||
        (h->ncolors != 1 && h->ncolors != 3) ||
        (h->ncmap != 0 && h->ncmap != 3) ||
        cmaplen > UTAHRLE_MAX_CMAPLEN)
        return CODEC_INVALID;
    if (h->width * h->height > UTAHRLE_MAX_PIXELS)
        return CODEC_TOO_LARGE;

    /* The background colour, or one filler byte, pads the header to even. */
    need = UTAHRLE_HEADER;
    memset(h->background, 0, sizeof h->background);
    if (h->flags & FLAG_NO_BACKGROUND) {
        need += 1u;
    } else {
        need += h->ncolors / 2u * 2u + 1u;
        if (left < need)
            return CODEC_TRUNCATED;
        memcpy(h->background, p + UTAHRLE_HEADER, h->ncolors);
    }

    maplen = (size_t)1 << cmaplen;
    entries = h->ncmap * maplen;
    if (left - need < entries * 2u)
        return CODEC_TRUNCATED;
    /* 16-bit map entries, map after map; 8-bit pixels use the first 256.
       URT stores 8-bit values in the high byte (v << 8) and reads the high
       byte back, so an identity map stays exact. Like URT's applymap,
       values past the end of a short map pass through unchanged. */
    for (m = 0; m < h->ncmap; m++)
        for (i = 0; i < 256u; i++)
            h->map[m][i] = i < maplen ? p[need + (m * maplen + i) * 2u + 1u]
                                      : (uint8_t)i;
    need += entries * 2u;

    if (h->flags & FLAG_COMMENT) {
        if (left - need < 2u)
            return CODEC_TRUNCATED;
        i = le16(p + need);
        need += 2u;
        /* Comments are padded to an even length. */
        if (left - need < ((i + 1u) & ~(size_t)1u))
            return CODEC_TRUNCATED;
        need += (i + 1u) & ~(size_t)1u;
    }
    *pos += need;
    return CODEC_OK;
}

/* Paint the opcode stream at *pos into rgba (raw channel values, rows
   top down), or just walk it when rgba is NULL. *pos ends after EOF. */
static enum codec_result run_ops(const uint8_t *data, size_t length,
                                 size_t *pos, const struct header *h,
                                 uint8_t *rgba)
{
    size_t at = *pos, x = 0, y = 0, n, i;
    unsigned op, operand, slot = 0, value;
    int store = 1;
    uint8_t *row;

    for (;;) {
        if (at >= length)
            return CODEC_TRUNCATED;
        op = data[at] & 0x3fu;
        if (op == OP_EOF) {
            *pos = at + 1u;
            return CODEC_OK;
        }
        if (length - at < 2u)
            return CODEC_TRUNCATED;
        operand = data[at + 1];
        at += 2u;
        if ((data[at - 2] & OP_LONG) && op != OP_SETCOLOR) {
            if (length - at < 2u)
                return CODEC_TRUNCATED;
            operand = le16(data + at);
            at += 2u;
        }
        switch (op) {
        case OP_SKIPLINES:
            y = operand >= h->height - y ? h->height : y + operand;
            x = 0;
            break;
        case OP_SETCOLOR:
            /* 255 is the alpha channel. Data for channels the header
               doesn't declare is read and dropped. */
            if (operand < h->ncolors) {
                slot = operand;
                store = 1;
            } else if (operand == 255u && (h->flags & FLAG_ALPHA)) {
                slot = 3;
                store = 1;
            } else {
                store = 0;
            }
            x = 0;
            break;
        case OP_SKIPPIXELS:
            x = operand >= h->width - x ? h->width : x + operand;
            break;
        case OP_BYTEDATA:
        case OP_RUNDATA:
            n = (size_t)operand + 1u;
            if (op == OP_BYTEDATA) {
                /* Literal bytes, padded to an even count. */
                if (length - at < n + (n & 1u))
                    return CODEC_TRUNCATED;
            } else {
                /* The run's value is the low byte of a 16-bit word. */
                if (length - at < 2u)
                    return CODEC_TRUNCATED;
            }
            if (rgba != NULL && store && y < h->height) {
                row = rgba + (h->height - 1u - y) * h->width * 4u;
                /* Runs past the right edge are clipped. */
                for (i = 0; i < n && x + i < h->width; i++) {
                    value = op == OP_BYTEDATA ? data[at + i] : data[at];
                    row[(x + i) * 4u + slot] = (uint8_t)value;
                }
            }
            x = n >= h->width - x ? h->width : x + n;
            at += op == OP_BYTEDATA ? n + (n & 1u) : 2u;
            break;
        default:
            return CODEC_INVALID;
        }
    }
}

/* Offset of the image after the one ending at end, or 0 if there is none.
   The EOF opcode is normally followed by a filler byte. */
static size_t next_image(const uint8_t *data, size_t length, size_t end)
{
    if (length - end >= 3u && is_magic(data + end + 1u))
        return end + 1u;
    if (length - end >= 2u && is_magic(data + end))
        return end;
    return 0;
}

static enum codec_result skip_image(const uint8_t *data, size_t length,
                                    size_t *pos)
{
    struct header h;
    enum codec_result result = parse_header(data, length, pos, &h);
    if (result != CODEC_OK)
        return result;
    return run_ops(data, length, pos, &h, NULL);
}

enum codec_result utahrle_count(const uint8_t *data, size_t length,
                                unsigned *count)
{
    size_t pos = 0;
    enum codec_result result;
    unsigned n = 0;

    *count = 0;
    for (;;) {
        result = skip_image(data, length, &pos);
        if (result != CODEC_OK) {
            if (n == 0)
                return result;
            break;
        }
        n++;
        pos = next_image(data, length, pos);
        if (pos == 0)
            break;
    }
    *count = n;
    return CODEC_OK;
}

void utahrle_free(struct utahrle_image *image)
{
    free(image->rgba);
    image->rgba = NULL;
    image->width = image->height = 0;
}

enum codec_result utahrle_decode(const uint8_t *data, size_t length,
                                 unsigned index, struct utahrle_image *image)
{
    struct header h;
    size_t pos = 0, pixels, i;
    enum codec_result result;
    uint8_t fill[3] = {0, 0, 0}, *p;
    unsigned c;

    image->width = image->height = 0;
    image->rgba = NULL;
    for (; index > 0; index--) {
        result = skip_image(data, length, &pos);
        if (result != CODEC_OK)
            return result;
        pos = next_image(data, length, pos);
        if (pos == 0)
            return CODEC_INVALID;
    }
    result = parse_header(data, length, &pos, &h);
    if (result != CODEC_OK)
        return result;
    pixels = h.width * h.height;
    image->rgba = malloc(pixels * 4u);
    if (image->rgba == NULL)
        return CODEC_NO_MEMORY;

    /* Unwritten pixels take the background colour only when the header asks
       for a clear to it; otherwise they are zero. Alpha starts transparent. */
    if ((h.flags & (FLAG_CLEARFIRST | FLAG_NO_BACKGROUND)) == FLAG_CLEARFIRST)
        memcpy(fill, h.background, sizeof fill);
    for (i = 0, p = image->rgba; i < pixels; i++, p += 4) {
        p[0] = fill[0]; p[1] = fill[1]; p[2] = fill[2]; p[3] = 0;
    }
    result = run_ops(data, length, &pos, &h, image->rgba);
    if (result != CODEC_OK) {
        utahrle_free(image);
        return result;
    }

    /* Apply the colour maps: one per channel, or all three for a single
       channel (pseudocolour). Alpha is never mapped. */
    for (i = 0, p = image->rgba; i < pixels; i++, p += 4) {
        if (h.ncolors == 1)
            p[1] = p[2] = p[0];
        for (c = 0; c < 3 && h.ncmap != 0; c++)
            p[c] = h.map[c][p[c]];
        if (!(h.flags & FLAG_ALPHA))
            p[3] = 255;
    }
    image->width = (unsigned)h.width;
    image->height = (unsigned)h.height;
    return CODEC_OK;
}
