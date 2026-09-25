#include "decode.h"
#include "common/bcn.h"
#include "lzo.h"
#include "lzss.h"
#include <stdlib.h>
#include <string.h>

#define MAX_PIXELS (16ul * 1024ul * 1024ul)
/* OFP index-palette files mark LZSS mip levels with this size. */
#define LZSS_WIDTH 1234u
#define LZSS_HEIGHT 8765u

enum kind { DXT1, DXT2, DXT3, DXT4, DXT5, ARGB4444, ARGB1555, ARGB8888, AI88, INDEX };
enum packing { RAW, LZO, LZSS, RLE };

struct header {
    enum kind kind;
    const uint8_t *palette;
    unsigned colours;
    size_t mips;
};

struct mip {
    unsigned width, height;
    enum packing packing;
    const uint8_t *data;
    size_t size;
};

static unsigned le16(const uint8_t *p)
{
    return p[0] | (unsigned)p[1] << 8;
}

static uint32_t le32(const uint8_t *p)
{
    return le16(p) | (uint32_t)le16(p + 2) << 16;
}

static int is_dxt(enum kind kind)
{
    return kind <= DXT5;
}

static enum codec_result parse_header(const uint8_t *data, size_t length, struct header *h)
{
    static const struct { unsigned type; enum kind kind; } types[] = {
        { 0xFF01, DXT1 }, { 0xFF02, DXT2 }, { 0xFF03, DXT3 }, { 0xFF04, DXT4 },
        { 0xFF05, DXT5 }, { 0x4444, ARGB4444 }, { 0x1555, ARGB1555 },
        { 0x8888, ARGB8888 }, { 0x8080, AI88 },
    };
    size_t pos = 0, i, tagg;

    if (length < 2)
        return CODEC_TRUNCATED;
    /* OFP index-palette files have no type word: they start with their
       taggs, or, in the 1997 demo, with the palette itself. */
    h->kind = INDEX;
    for (i = 0; i < sizeof types / sizeof types[0]; i++)
        if (le16(data) == types[i].type) {
            h->kind = types[i].kind;
            pos = 2;
        }
    /* Taggs: "GGAT", a reversed four-letter name, a length and the data.
       None changes how the pixels decode, so all are skipped. */
    while (length - pos >= 4 && memcmp(data + pos, "GGAT", 4) == 0) {
        if (length - pos < 12)
            return CODEC_TRUNCATED;
        tagg = le32(data + pos + 8);
        if (tagg > length - pos - 12)
            return CODEC_TRUNCATED;
        pos += 12 + tagg;
    }
    if (length - pos < 2)
        return CODEC_TRUNCATED;
    h->colours = le16(data + pos);
    h->palette = data + pos + 2;
    pos += 2;
    if ((size_t)h->colours * 3 > length - pos)
        return CODEC_TRUNCATED;
    pos += (size_t)h->colours * 3;
    /* Other types carry an empty palette; one that isn't is skipped. */
    if (h->kind == INDEX && h->colours == 0)
        return CODEC_INVALID;
    h->mips = pos;
    return CODEC_OK;
}

/* Read the mip level at *pos and move past it. *end is set instead at the
   end marker or the end of the file. */
static enum codec_result next_mip(const uint8_t *data, size_t length, size_t *pos,
                                  const struct header *h, struct mip *m, int *end)
{
    size_t p = *pos;

    *end = 0;
    if (p == length) {
        *end = 1;
        return CODEC_OK;
    }
    if (length - p < 4)
        return CODEC_TRUNCATED;
    m->width = le16(data + p);
    m->height = le16(data + p + 2);
    p += 4;
    if (m->width == 0 && m->height == 0) {
        *end = 1;
        return CODEC_OK;
    }
    if (h->kind == INDEX) {
        m->packing = RLE;
        if (m->width == LZSS_WIDTH && m->height == LZSS_HEIGHT) {
            if (length - p < 4)
                return CODEC_TRUNCATED;
            m->width = le16(data + p);
            m->height = le16(data + p + 2);
            m->packing = LZSS;
            p += 4;
        }
    } else if (is_dxt(h->kind)) {
        /* Arma 2 and later flag LZO-packed levels in the top bit of the width. */
        m->packing = m->width & 0x8000u ? LZO : RAW;
        m->width &= 0x7FFFu;
    } else {
        m->packing = LZSS;
    }
    if (m->width == 0 || m->height == 0)
        return CODEC_INVALID;
    if (length - p < 3)
        return CODEC_TRUNCATED;
    m->size = data[p] | (size_t)data[p + 1] << 8 | (size_t)data[p + 2] << 16;
    p += 3;
    if (m->size > length - p)
        return CODEC_TRUNCATED;
    m->data = data + p;
    *pos = p + m->size;
    return CODEC_OK;
}

enum codec_result paa_count(const uint8_t *data, size_t length, unsigned long *count)
{
    struct header h;
    struct mip m;
    size_t pos;
    int end = 0;
    enum codec_result r = parse_header(data, length, &h);

    *count = 0;
    if (r != CODEC_OK)
        return r;
    pos = h.mips;
    while ((r = next_mip(data, length, &pos, &h, &m, &end)) == CODEC_OK && !end)
        ++*count;
    return *count > 0 || r != CODEC_OK ? r : CODEC_INVALID;
}

/* Index-palette levels: a byte n < 0x80 starts n + 1 literals, and n >= 0x80
   repeats the next byte n - 0x7F times. A run past the end is clamped. */
static enum codec_result unpack_rle(const uint8_t *in, size_t in_len,
                                    uint8_t *out, size_t out_len)
{
    size_t ip = 0, op = 0, n;

    while (op < out_len) {
        if (ip >= in_len)
            return CODEC_INVALID;
        n = (in[ip] & 0x7Fu) + 1;
        if (n > out_len - op)
            n = out_len - op;
        if (in[ip++] & 0x80u) {
            if (ip >= in_len)
                return CODEC_INVALID;
            memset(out + op, in[ip++], n);
        } else {
            if (n > in_len - ip)
                return CODEC_INVALID;
            memcpy(out + op, in + ip, n);
            ip += n;
        }
        op += n;
    }
    return CODEC_OK;
}

static void unpremultiply(uint8_t *p)
{
    unsigned c, a = p[3];
    if (a == 0 || a == 255)
        return;
    for (c = 0; c < 3; c++) {
        unsigned v = (p[c] * 255u + a / 2) / a;
        p[c] = (uint8_t)(v > 255 ? 255 : v);
    }
}

static void decode_blocks(enum kind kind, const uint8_t *in, unsigned w, unsigned h,
                          uint8_t *rgba)
{
    uint8_t block[64];
    unsigned bx, by, x, y, size = kind == DXT1 ? 8 : 16;

    for (by = 0; by < (h + 3) / 4; by++)
        for (bx = 0; bx < (w + 3) / 4; bx++, in += size) {
            switch (kind) {
            /* The Direct3D rule: DXT1 black in three-colour blocks is clear. */
            case DXT1: bc1_block(in, block, 1); break;
            case DXT2: case DXT3: bc2_block(in, block); break;
            default: bc3_block(in, block); break;
            }
            for (y = 0; y < 4 && by * 4 + y < h; y++)
                for (x = 0; x < 4 && bx * 4 + x < w; x++) {
                    uint8_t *p = rgba + ((size_t)(by * 4 + y) * w + bx * 4 + x) * 4;
                    memcpy(p, block + (y * 4 + x) * 4, 4);
                    if (kind == DXT2 || kind == DXT4)
                        unpremultiply(p);
                }
        }
}

/* Widen a 5-bit channel by repeating its top bits, as the hardware does. */
static uint8_t five(unsigned v)
{
    v &= 31;
    return (uint8_t)(v << 3 | v >> 2);
}

/* Direct3D layouts, little-endian: A4R4G4B4, A1R5G5B5, A8R8G8B8 (bytes
   B, G, R, A) and A8L8 (gray, then alpha). */
static void decode_pixels(const struct header *h, const uint8_t *in, size_t pixels,
                          uint8_t *rgba)
{
    size_t i;
    unsigned v;

    for (i = 0; i < pixels; i++, rgba += 4)
        switch (h->kind) {
        case ARGB4444:
            v = le16(in + i * 2);
            rgba[0] = (uint8_t)((v >> 8 & 15) * 17);
            rgba[1] = (uint8_t)((v >> 4 & 15) * 17);
            rgba[2] = (uint8_t)((v & 15) * 17);
            rgba[3] = (uint8_t)((v >> 12) * 17);
            break;
        case ARGB1555:
            v = le16(in + i * 2);
            rgba[0] = five(v >> 10);
            rgba[1] = five(v >> 5);
            rgba[2] = five(v);
            rgba[3] = v & 0x8000u ? 255 : 0;
            break;
        case ARGB8888:
            rgba[0] = in[i * 4 + 2];
            rgba[1] = in[i * 4 + 1];
            rgba[2] = in[i * 4];
            rgba[3] = in[i * 4 + 3];
            break;
        case AI88:
            rgba[0] = rgba[1] = rgba[2] = in[i * 2];
            rgba[3] = in[i * 2 + 1];
            break;
        default:
            /* Palette entries are B, G, R; an index past the end is black. */
            v = in[i];
            if (v < h->colours) {
                rgba[0] = h->palette[v * 3 + 2];
                rgba[1] = h->palette[v * 3 + 1];
                rgba[2] = h->palette[v * 3];
            } else {
                rgba[0] = rgba[1] = rgba[2] = 0;
            }
            rgba[3] = 255;
            break;
        }
}

enum codec_result paa_decode(const uint8_t *data, size_t length, unsigned long index,
                             struct paa_image *image)
{
    struct header h;
    struct mip m;
    size_t pos, pixels, size;
    unsigned long i;
    int end;
    uint8_t *unpacked = NULL;
    const uint8_t *in;
    enum codec_result r;

    image->rgba = NULL;
    if ((r = parse_header(data, length, &h)) != CODEC_OK)
        return r;
    pos = h.mips;
    for (i = 0; i <= index; i++) {
        if ((r = next_mip(data, length, &pos, &h, &m, &end)) != CODEC_OK)
            return r;
        if (end)
            return i == 0 ? CODEC_TRUNCATED : CODEC_INVALID;
    }
    pixels = (size_t)m.width * m.height;
    if (pixels > MAX_PIXELS)
        return CODEC_TOO_LARGE;
    switch (h.kind) {
    case DXT1: size = (size_t)((m.width + 3) / 4) * ((m.height + 3) / 4) * 8; break;
    case DXT2: case DXT3: case DXT4: case DXT5:
        size = (size_t)((m.width + 3) / 4) * ((m.height + 3) / 4) * 16; break;
    case ARGB8888: size = pixels * 4; break;
    case INDEX: size = pixels; break;
    default: size = pixels * 2; break;
    }
    in = m.data;
    if (m.packing == RAW) {
        if (m.size < size)
            return CODEC_INVALID;
    } else {
        if ((unpacked = malloc(size)) == NULL)
            return CODEC_NO_MEMORY;
        switch (m.packing) {
        case LZO: r = paa_lzo_expand(m.data, m.size, unpacked, size); break;
        case LZSS: r = paa_lzss_expand(m.data, m.size, unpacked, size); break;
        default: r = unpack_rle(m.data, m.size, unpacked, size); break;
        }
        if (r == CODEC_OK) {
            in = unpacked;
        } else if (m.packing == LZSS && h.kind != INDEX && m.size == size) {
            /* Some third-party writers store these levels raw. */
            free(unpacked);
            unpacked = NULL;
        } else {
            free(unpacked);
            return r;
        }
    }
    image->rgba = malloc(pixels * 4);
    if (image->rgba == NULL) {
        free(unpacked);
        return CODEC_NO_MEMORY;
    }
    image->width = m.width;
    image->height = m.height;
    if (is_dxt(h.kind))
        decode_blocks(h.kind, in, m.width, m.height, image->rgba);
    else
        decode_pixels(&h, in, pixels, image->rgba);
    free(unpacked);
    return CODEC_OK;
}

void paa_free(struct paa_image *image)
{
    free(image->rgba);
    image->rgba = NULL;
}
