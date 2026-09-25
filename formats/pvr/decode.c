#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "common/bcn.h"
#include "common/bptc.h"
#include "decode.h"
#include "etc.h"
#include "pvrtc.h"

#define HEADER_SIZE 52u
#define MAX_SIDE 65535u
#define MAX_PIXELS (16ul * 1024ul * 1024ul)

/* Version 3 */
#define V3_PREMULTIPLIED 0x2u
#define V3_META_ORIENTATION 3u
#define V3_META_CHANNEL_TYPES 6u
/* Version 2 flags */
#define V2_TWIDDLED 0x200u
#define V2_CUBE 0x1000u
#define V2_VOLUME 0x4000u
#define V2_ALPHA 0x8000u
#define V2_FLIPPED 0x10000u

enum kind {
    RAW, PVRTC2, PVRTC4, ETC_RGB, ETC_A1, ETC_RGBA, EAC_R, EAC_RG,
    BC1, BC2, BC3, BC4, BC5, BC7
};
enum alpha { OPAQUE, STRAIGHT, PREMULTIPLIED };

struct layout {
    enum kind kind;
    enum alpha alpha;
    /* RAW: channel names ('r', 'g', 'b', 'a', 'l' luminance, 'i' intensity,
       'x' unused) and widths, in order. Missing colours are 0, as in GL. Packed pixels are one little-endian
       word with the first channel in its top bits; otherwise each channel
       is its own little-endian field, in order. */
    unsigned channels;
    char name[4];
    unsigned bits[4];
    unsigned pixel_bytes;
    int packed, twiddled;
    uint32_t width, height, depth, surfaces, faces;
    unsigned levels;
    int mips_outside;           /* version 3 order */
    int flip_x, flip_y;
    size_t offset;
};

static uint32_t get32(const uint8_t *p)
{
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static int power_of_two(uint32_t v)
{
    return v != 0 && (v & (v - 1u)) == 0;
}

static unsigned bits_needed(uint32_t v)
{
    unsigned n = 0;
    while (v) {
        n++;
        v >>= 1;
    }
    return n;
}

/* Set up RAW channels from a name string and widths, and check them. */
static enum codec_result raw(struct layout *l, const char *names, const unsigned *bits,
                             int packed)
{
    unsigned total = 0, k;

    l->kind = RAW;
    l->alpha = OPAQUE;
    l->channels = 0;
    for (k = 0; k < 4 && names[k] != '\0'; k++) {
        if (!strchr("rgbalix", names[k]) || bits[k] == 0 || bits[k] > 32)
            return CODEC_INVALID;
        l->name[k] = names[k];
        l->bits[k] = bits[k];
        l->channels++;
        total += bits[k];
        if (names[k] == 'a' || names[k] == 'i')
            l->alpha = STRAIGHT;
        if (bits[k] % 8u != 0)
            packed = 1;
    }
    if (l->channels == 0 || total % 8u != 0)
        return CODEC_INVALID;
    if (packed && total != 8 && total != 16 && total != 32)
        return CODEC_INVALID;
    l->packed = packed;
    l->pixel_bytes = total / 8u;
    return CODEC_OK;
}

static int unsigned_type(uint32_t type)
{
    /* Normalised and plain unsigned byte, short and int. */
    return type <= 10 && type % 2u == 0;
}

static enum codec_result v3_format(struct layout *l, const uint8_t *h)
{
    uint32_t low = get32(h + 8), high = get32(h + 12), type = get32(h + 20);

    if (high != 0) {
        char names[5];
        unsigned bits[4], k;
        for (k = 0; k < 4; k++) {
            names[k] = (char)(low >> (8u * k));
            bits[k] = (high >> (8u * k)) & 0xffu;
        }
        names[4] = '\0';
        if (!unsigned_type(type))
            return CODEC_INVALID;
        return raw(l, names, bits, 0);
    }
    switch (low) {
    case 0: l->kind = PVRTC2; l->alpha = OPAQUE; break;
    case 1: l->kind = PVRTC2; break;
    case 2: l->kind = PVRTC4; l->alpha = OPAQUE; break;
    case 3: l->kind = PVRTC4; break;
    case 6: case 22: l->kind = ETC_RGB; l->alpha = OPAQUE; break;
    case 7: l->kind = BC1; break;
    case 8: l->kind = BC2; l->alpha = PREMULTIPLIED; break;
    case 9: l->kind = BC2; break;
    case 10: l->kind = BC3; l->alpha = PREMULTIPLIED; break;
    case 11: l->kind = BC3; break;
    case 12: l->kind = BC4; l->alpha = OPAQUE; break;
    case 13: l->kind = BC5; l->alpha = OPAQUE; break;
    case 15: l->kind = BC7; break;
    case 23: l->kind = ETC_RGBA; break;
    case 24: l->kind = ETC_A1; break;
    case 25: l->kind = EAC_R; l->alpha = OPAQUE; break;
    case 26: l->kind = EAC_RG; l->alpha = OPAQUE; break;
    default: return CODEC_INVALID;
    }
    /* Signed variants of the single and two-channel formats. */
    if ((l->kind == BC4 || l->kind == BC5 || l->kind == EAC_R || l->kind == EAC_RG) &&
        !unsigned_type(type))
        return CODEC_INVALID;
    return CODEC_OK;
}

/* Read the orientation, and reject channels whose own types aren't
   unsigned integers. Malformed metadata is ignored. */
static enum codec_result v3_metadata(struct layout *l, const uint8_t *p, size_t size)
{
    while (size >= 12) {
        uint32_t key = get32(p + 4), bytes = get32(p + 8);
        const uint8_t *d = p + 12;
        if (bytes > size - 12u)
            break;
        if (memcmp(p, "PVR\3", 4) == 0 && key == V3_META_ORIENTATION && bytes >= 2) {
            l->flip_x = d[0] != 0;
            l->flip_y = d[1] != 0;
        } else if (memcmp(p, "PVR\3", 4) == 0 && key == V3_META_CHANNEL_TYPES &&
                   l->kind == RAW) {
            unsigned k;
            for (k = 0; k < l->channels && k < bytes; k++)
                if (!unsigned_type(d[k]))
                    return CODEC_INVALID;
        }
        p += 12u + bytes;
        size -= 12u + bytes;
    }
    return CODEC_OK;
}

static enum codec_result parse_v3(const uint8_t *data, size_t length, struct layout *l)
{
    uint32_t meta, levels, largest;
    enum codec_result result;

    l->alpha = STRAIGHT;
    result = v3_format(l, data);
    if (result != CODEC_OK)
        return result;
    if ((get32(data + 4) & V3_PREMULTIPLIED) && l->alpha == STRAIGHT)
        l->alpha = PREMULTIPLIED;
    l->height = get32(data + 24);
    l->width = get32(data + 28);
    l->depth = get32(data + 32);
    l->surfaces = get32(data + 36);
    l->faces = get32(data + 40);
    levels = get32(data + 44);
    meta = get32(data + 48);
    if (meta > length - HEADER_SIZE)
        return CODEC_TRUNCATED;
    result = v3_metadata(l, data + HEADER_SIZE, meta);
    if (result != CODEC_OK)
        return result;
    l->offset = HEADER_SIZE + meta;
    l->mips_outside = 1;
    if (l->depth == 0)
        l->depth = 1;
    if (l->surfaces == 0)
        l->surfaces = 1;
    if (l->faces == 0)
        l->faces = 1;
    if (l->depth > MAX_SIDE || l->surfaces > MAX_SIDE || l->faces > MAX_SIDE)
        return CODEC_TOO_LARGE;
    largest = l->width > l->height ? l->width : l->height;
    if (l->depth > largest)
        largest = l->depth;
    if (levels == 0)
        levels = 1;
    if (levels > bits_needed(largest))
        return CODEC_INVALID;
    l->levels = levels;
    return CODEC_OK;
}

static enum codec_result v2_format(struct layout *l, uint32_t type, uint32_t amask)
{
    static const unsigned b4444[4] = { 4, 4, 4, 4 }, b5551[4] = { 5, 5, 5, 1 };
    static const unsigned b1555[4] = { 1, 5, 5, 5 }, b565[3] = { 5, 6, 5 };
    static const unsigned b8888[4] = { 8, 8, 8, 8 }, b88[2] = { 8, 8 }, b8[1] = { 8 };
    int alpha = amask != 0;

    switch (type) {
    /* Direct3D and early PowerVR SDK types: ARGB words, A on top. */
    case 0x00: return raw(l, "argb", b4444, 1);
    case 0x01: return raw(l, "argb", b1555, 1);
    case 0x02: case 0x13: return raw(l, "rgb", b565, 1);
    case 0x05: return raw(l, "argb", b8888, 1);
    case 0x07: case 0x16: return raw(l, "l", b8, 0);
    case 0x08: case 0x17: return raw(l, "la", b88, 0);
    /* OpenGL ES types */
    case 0x10: return raw(l, "rgba", b4444, 1);
    case 0x11: return raw(l, "rgba", b5551, 1);
    case 0x12: return raw(l, "rgba", b8888, 0);
    case 0x15: return raw(l, "rgb", b8888, 0);
    case 0x1a: return raw(l, "bgra", b8888, 0);
    case 0x1b: return raw(l, "a", b8, 0);
    case 0x0c: case 0x18: l->kind = PVRTC2; break;
    case 0x0d: case 0x19: l->kind = PVRTC4; break;
    case 0x20: l->kind = BC1; break;
    case 0x21: l->kind = BC2; l->alpha = PREMULTIPLIED; return CODEC_OK;
    case 0x22: l->kind = BC2; return CODEC_OK;
    case 0x23: l->kind = BC3; l->alpha = PREMULTIPLIED; return CODEC_OK;
    case 0x24: l->kind = BC3; return CODEC_OK;
    case 0x36: l->kind = ETC_RGB; l->alpha = OPAQUE; return CODEC_OK;
    default: return CODEC_INVALID;
    }
    /* PVRTC and DXT1 have alpha only when the header declares it. */
    l->alpha = alpha ? STRAIGHT : OPAQUE;
    return CODEC_OK;
}

static enum codec_result parse_v2(const uint8_t *data, struct layout *l)
{
    uint32_t flags = get32(data + 16), largest;
    unsigned levels;
    enum codec_result result;

    l->alpha = STRAIGHT;
    result = v2_format(l, flags & 0xffu, (flags & V2_ALPHA) ? 1u : get32(data + 40));
    if (result != CODEC_OK)
        return result;
    if (flags & V2_VOLUME)
        return CODEC_INVALID;
    l->height = get32(data + 4);
    l->width = get32(data + 8);
    l->twiddled = (flags & V2_TWIDDLED) && l->kind == RAW;
    l->flip_y = (flags & V2_FLIPPED) != 0;
    l->depth = 1;
    l->faces = 1;
    l->surfaces = get32(data + 48);
    if (l->surfaces == 0)
        l->surfaces = (flags & V2_CUBE) ? 6 : 1;
    if (l->surfaces > MAX_SIDE)
        return CODEC_TOO_LARGE;
    largest = l->width > l->height ? l->width : l->height;
    /* The count excludes the top level. */
    levels = get32(data + 12);
    if (levels >= bits_needed(largest))
        return CODEC_INVALID;
    l->levels = levels + 1u;
    l->offset = HEADER_SIZE;
    return CODEC_OK;
}

static enum codec_result parse(const uint8_t *data, size_t length, struct layout *l)
{
    enum codec_result result;
    uint32_t pot;

    memset(l, 0, sizeof *l);
    if (length >= 4 && memcmp(data, "PVR\3", 4) == 0) {
        if (length < HEADER_SIZE)
            return CODEC_TRUNCATED;
        result = parse_v3(data, length, l);
    } else if (length >= 4 && get32(data) == HEADER_SIZE) {
        if (length < HEADER_SIZE)
            return CODEC_TRUNCATED;
        if (memcmp(data + 44, "PVR!", 4) != 0)
            return CODEC_INVALID;
        result = parse_v2(data, l);
    } else {
        return length < 4 ? CODEC_TRUNCATED : CODEC_INVALID;
    }
    if (result != CODEC_OK)
        return result;
    if (l->width == 0 || l->height == 0)
        return CODEC_INVALID;
    if (l->width > MAX_SIDE || l->height > MAX_SIDE)
        return CODEC_TOO_LARGE;
    pot = power_of_two(l->width) && power_of_two(l->height);
    if ((l->kind == PVRTC2 || l->kind == PVRTC4 || l->twiddled) && !pot)
        return CODEC_INVALID;
    return CODEC_OK;
}

static uint32_t level_size(uint32_t size, unsigned level)
{
    size >>= level;
    return size ? size : 1;
}

/* Bytes in one image of a level. At most 2^32 * 16. */
static uint64_t image_bytes(const struct layout *l, unsigned level)
{
    uint64_t w = level_size(l->width, level), h = level_size(l->height, level);
    uint64_t blocks = ((w + 3u) / 4u) * ((h + 3u) / 4u);
    switch (l->kind) {
    case RAW: return w * h * l->pixel_bytes;
    case PVRTC2: return pvrtc_size((unsigned)w, (unsigned)h, 1);
    case PVRTC4: return pvrtc_size((unsigned)w, (unsigned)h, 0);
    case ETC_RGB: case ETC_A1: case EAC_R: case BC1: case BC4: return blocks * 8u;
    default: return blocks * 16u;
    }
}

/* Images in a level: every surface, face and depth slice. */
static uint64_t level_images(const struct layout *l, unsigned level)
{
    return (uint64_t)l->surfaces * l->faces * level_size(l->depth, level);
}

/* Bytes in each surface's mip chain, in version 2 order. */
static uint64_t chain_bytes(const struct layout *l)
{
    uint64_t chain = 0;
    unsigned lv;
    for (lv = 0; lv < l->levels; lv++)
        chain += image_bytes(l, lv);
    return chain;
}

/* Count the leading images wholly inside the file. */
static unsigned long present(const struct layout *l, size_t length)
{
    uint64_t available = length - l->offset, done = 0, n, fit, chain;
    unsigned lv;

    if (l->mips_outside) {
        for (lv = 0; lv < l->levels; lv++) {
            uint64_t bytes = image_bytes(l, lv);
            n = level_images(l, lv);
            fit = available / bytes;
            if (fit < n)
                return (unsigned long)(done + fit);
            done += n;
            available -= n * bytes;
        }
        return (unsigned long)done;
    }
    chain = chain_bytes(l);
    fit = available / chain;
    if (fit >= l->surfaces)
        return (unsigned long)l->surfaces * l->levels;
    done = fit * l->levels;
    available -= fit * chain;
    for (lv = 0; lv < l->levels && available >= image_bytes(l, lv); lv++) {
        available -= image_bytes(l, lv);
        done++;
    }
    return (unsigned long)done;
}

/* Find image index: its level and the offset of its data. The level is set
   even when the image is missing from the file. */
static enum codec_result find(const struct layout *l, size_t length, uint64_t index,
                              unsigned *level, uint64_t *offset)
{
    uint64_t available = length - l->offset, n, chain;
    int missing = 0;
    unsigned lv;

    *offset = 0;
    if (l->mips_outside) {
        for (lv = 0; lv < l->levels; lv++) {
            uint64_t bytes = image_bytes(l, lv);
            n = level_images(l, lv);
            if (index < n) {
                *level = lv;
                if (missing || index >= available / bytes)
                    return CODEC_TRUNCATED;
                *offset += index * bytes;
                return CODEC_OK;
            }
            index -= n;
            if (available / bytes < n) {
                missing = 1;
            } else {
                available -= n * bytes;
                *offset += n * bytes;
            }
        }
        return CODEC_INVALID;
    }
    if (index / l->levels >= l->surfaces)
        return CODEC_INVALID;
    *level = (unsigned)(index % l->levels);
    chain = chain_bytes(l);
    if (index / l->levels > available / chain)
        return CODEC_TRUNCATED;
    *offset = index / l->levels * chain;
    for (lv = 0; lv < *level; lv++)
        *offset += image_bytes(l, lv);
    if (*offset > available || image_bytes(l, lv) > available - *offset)
        return CODEC_TRUNCATED;
    return CODEC_OK;
}

enum codec_result pvr_count(const uint8_t *data, size_t length, unsigned long *count)
{
    struct layout l;
    enum codec_result result = parse(data, length, &l);

    *count = 0;
    if (result != CODEC_OK)
        return result;
    *count = present(&l, length);
    return CODEC_OK;
}

static uint8_t widen(uint32_t v, unsigned bits)
{
    uint64_t top = bits == 32 ? 0xffffffffu : (1u << bits) - 1u;
    if (bits == 8)
        return (uint8_t)v;
    return (uint8_t)(((uint64_t)v * 510u + top) / (top * 2u));
}

/* Position of pixel x, y in Morton order, y taking the lower bit of each
   pair, for power-of-two sides. */
static size_t twiddle(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
{
    uint32_t least = width < height ? width : height, bit;
    unsigned shift = 0;
    size_t index = 0;

    for (bit = 1; bit < least; bit <<= 1, shift++) {
        if (y & bit)
            index |= (size_t)1 << (2u * shift);
        if (x & bit)
            index |= (size_t)1 << (2u * shift + 1u);
    }
    return index | (size_t)((width < height ? y : x) >> shift) << (2u * shift);
}

static void decode_raw(const struct layout *l, const uint8_t *data, uint8_t *rgba,
                       uint32_t width, uint32_t height)
{
    uint32_t x, y;
    unsigned k, i;

    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++, rgba += 4) {
            size_t index = l->twiddled ? twiddle(x, y, width, height)
                                       : (size_t)y * width + x;
            const uint8_t *p = data + index * l->pixel_bytes;
            uint32_t word = 0;
            unsigned used = 0;
            uint8_t c;
            rgba[0] = rgba[1] = rgba[2] = 0;
            rgba[3] = 255;
            if (l->packed)
                for (i = 0; i < l->pixel_bytes; i++)
                    word |= (uint32_t)p[i] << (8u * i);
            for (k = 0; k < l->channels; k++) {
                unsigned bits = l->bits[k];
                uint32_t v = 0;
                if (l->packed) {
                    used += bits;
                    v = (uint32_t)((uint64_t)word >> (l->pixel_bytes * 8u - used));
                    v &= bits == 32 ? 0xffffffffu : (1u << bits) - 1u;
                } else {
                    for (i = 0; i < bits / 8u; i++)
                        v |= (uint32_t)*p++ << (8u * i);
                }
                c = widen(v, bits);
                switch (l->name[k]) {
                case 'r': rgba[0] = c; break;
                case 'g': rgba[1] = c; break;
                case 'b': rgba[2] = c; break;
                case 'a': rgba[3] = c; break;
                case 'i': rgba[3] = c; /* fall through */
                case 'l': rgba[0] = rgba[1] = rgba[2] = c; break;
                default: break;
                }
            }
        }
    }
}

static void decode_blocks(const struct layout *l, const uint8_t *p, uint8_t *rgba,
                          uint32_t width, uint32_t height)
{
    unsigned bx, by, x, y, i;
    unsigned block_bytes = 16;
    uint8_t block[64];

    if (l->kind == ETC_RGB || l->kind == ETC_A1 || l->kind == EAC_R ||
        l->kind == BC1 || l->kind == BC4)
        block_bytes = 8;
    for (by = 0; by < height; by += 4) {
        for (bx = 0; bx < width; bx += 4) {
            switch (l->kind) {
            case ETC_RGB: etc2_rgb_block(p, block, 0); break;
            case ETC_A1: etc2_rgb_block(p, block, 1); break;
            case ETC_RGBA:
                etc2_rgb_block(p + 8, block, 0);
                eac8_block(p, block, 3);
                break;
            case EAC_R:
            case EAC_RG:
                for (i = 0; i < 16; i++) {
                    block[i * 4u + 1u] = block[i * 4u + 2u] = 0;
                    block[i * 4u + 3u] = 255;
                }
                eac11_block(p, block, 0);
                if (l->kind == EAC_RG)
                    eac11_block(p + 8, block, 1);
                else
                    for (i = 0; i < 16; i++)
                        block[i * 4u + 1u] = block[i * 4u + 2u] = block[i * 4u];
                break;
            case BC1: bc1_block(p, block, l->alpha != OPAQUE); break;
            case BC2: bc2_block(p, block); break;
            case BC3: bc3_block(p, block); break;
            case BC4: bc4_block(p, block); break;
            case BC5: bc5_block(p, block, 0); break;
            default: bc7_block(p, block); break;
            }
            p += block_bytes;
            for (y = 0; y < 4 && by + y < height; y++)
                for (x = 0; x < 4 && bx + x < width; x++)
                    memcpy(rgba + ((size_t)(by + y) * width + bx + x) * 4u,
                           block + (y * 4u + x) * 4u, 4);
        }
    }
}

static void fix_alpha(enum alpha alpha, uint8_t *rgba, size_t pixels)
{
    size_t i;
    unsigned k;
    if (alpha == OPAQUE) {
        for (i = 0; i < pixels; i++)
            rgba[i * 4u + 3u] = 255;
        return;
    }
    if (alpha != PREMULTIPLIED)
        return;
    for (i = 0; i < pixels; i++, rgba += 4) {
        unsigned a = rgba[3];
        if (a == 0 || a == 255)
            continue;
        for (k = 0; k < 3; k++) {
            unsigned c = (rgba[k] * 255u + a / 2u) / a;
            rgba[k] = (uint8_t)(c > 255u ? 255u : c);
        }
    }
}

static void flip(uint8_t *rgba, uint32_t width, uint32_t height, int flip_x, int flip_y)
{
    uint32_t x, y;
    uint8_t t[4];

    for (y = 0; flip_x && y < height; y++) {
        uint8_t *row = rgba + (size_t)y * width * 4u;
        for (x = 0; x < width / 2u; x++) {
            memcpy(t, row + x * 4u, 4);
            memcpy(row + x * 4u, row + (width - 1u - x) * 4u, 4);
            memcpy(row + (width - 1u - x) * 4u, t, 4);
        }
    }
    for (y = 0; flip_y && y < height / 2u; y++) {
        uint8_t *a = rgba + (size_t)y * width * 4u;
        uint8_t *b = rgba + (size_t)(height - 1u - y) * width * 4u;
        for (x = 0; x < width * 4u; x++) {
            t[0] = a[x];
            a[x] = b[x];
            b[x] = t[0];
        }
    }
}

enum codec_result pvr_decode(const uint8_t *data, size_t length, unsigned long index,
                             struct pvr_image *image)
{
    struct layout l;
    unsigned level = UINT_MAX;
    uint64_t offset;
    uint32_t w, h;
    size_t pixels;
    const uint8_t *p;
    enum codec_result result = parse(data, length, &l);

    image->width = image->height = 0;
    image->rgba = NULL;
    if (result != CODEC_OK)
        return result;
    result = find(&l, length, index, &level, &offset);
    if (level == UINT_MAX)
        return result;
    w = level_size(l.width, level);
    h = level_size(l.height, level);
    if ((uint64_t)w * h > MAX_PIXELS)
        return CODEC_TOO_LARGE;
    if (result != CODEC_OK)
        return result;
    pixels = (size_t)w * h;
    image->rgba = malloc(pixels * 4u);
    if (image->rgba == NULL)
        return CODEC_NO_MEMORY;
    p = data + l.offset + offset;
    if (l.kind == RAW)
        decode_raw(&l, p, image->rgba, w, h);
    else if (l.kind == PVRTC2 || l.kind == PVRTC4)
        pvrtc_decode(p, w, h, l.kind == PVRTC2, image->rgba);
    else
        decode_blocks(&l, p, image->rgba, w, h);
    fix_alpha(l.alpha, image->rgba, pixels);
    flip(image->rgba, w, h, l.flip_x, l.flip_y);
    image->width = w;
    image->height = h;
    return CODEC_OK;
}

void pvr_free(struct pvr_image *image)
{
    free(image->rgba);
    image->rgba = NULL;
}
