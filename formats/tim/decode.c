#include "decode.h"
#include <stdlib.h>

#define TIM_MAX_PIXELS (16u * 1024u * 1024u)
#define TIM_CLUT_FLAG 8u

/* Where one image's parts lie in the file. */
struct layout {
    unsigned mode, width, height;
    const uint8_t *clut, *pixels;
    size_t clut_entries, row_bytes, size;
};

static uint32_t le16(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8);
}

static uint32_t le32(const uint8_t *p)
{
    return le16(p) | (le16(p + 2) << 16);
}

/* A block header is a byte count, x, y, width in 16-bit units, and height.
   The dimensions decide the size; the byte count is not trusted. */
static enum codec_result parse(const uint8_t *data, size_t length,
                               struct layout *out)
{
    size_t pos = 8, units;
    uint32_t flag, w16, height, width;

    if (length < 8)
        return CODEC_TRUNCATED;
    if (le32(data) != 0x10)
        return CODEC_INVALID;
    /* Only the pixel mode and CLUT bits are defined; the rest are ignored. */
    flag = le32(data + 4);
    out->mode = flag & 7u;
    /* Mode 4 is a mixed-depth frame buffer dump with no defined layout. */
    if (out->mode > 3)
        return CODEC_INVALID;
    out->clut = NULL;
    out->clut_entries = 0;
    /* True-colour images may carry a CLUT they don't use; it is skipped. */
    if (flag & TIM_CLUT_FLAG) {
        if (length - pos < 12)
            return CODEC_TRUNCATED;
        units = (size_t)le16(data + pos + 8) * le16(data + pos + 10);
        if (units > (length - pos - 12) / 2)
            return CODEC_TRUNCATED;
        out->clut = data + pos + 12;
        out->clut_entries = units;
        pos += 12 + units * 2;
    }
    if (length - pos < 12)
        return CODEC_TRUNCATED;
    w16 = le16(data + pos + 8);
    height = le16(data + pos + 10);
    switch (out->mode) {
    case 0: width = w16 * 4u; break;
    case 1: width = w16 * 2u; break;
    case 2: width = w16; break;
    default: width = w16 * 2u / 3u; break; /* rows pad to 16 bits */
    }
    if (width == 0 || height == 0)
        return CODEC_INVALID;
    if (width > 65535u || (size_t)width * height > TIM_MAX_PIXELS)
        return CODEC_TOO_LARGE;
    out->row_bytes = (size_t)w16 * 2u;
    if (out->row_bytes > (length - pos - 12) / height)
        return CODEC_TRUNCATED;
    out->width = width;
    out->height = height;
    out->pixels = data + pos + 12;
    out->size = pos + 12 + out->row_bytes * height;
    return CODEC_OK;
}

static uint8_t five_to_eight(uint32_t v)
{
    v &= 31u;
    return (uint8_t)((v << 3) | (v >> 2));
}

/* The semi-transparency bit only means something to the PlayStation GPU. */
static void put15(uint8_t *dst, uint32_t word)
{
    dst[0] = five_to_eight(word);
    dst[1] = five_to_eight(word >> 5);
    dst[2] = five_to_eight(word >> 10);
    dst[3] = 255;
}

static void make_palette(const struct layout *l, unsigned colours,
                         uint8_t palette[256][4])
{
    unsigned i;
    for (i = 0; i < colours; i++) {
        if (l->clut == NULL) {
            uint8_t gray = (uint8_t)(i * 255u / (colours - 1u));
            palette[i][0] = palette[i][1] = palette[i][2] = gray;
            palette[i][3] = 255;
        } else if (i < l->clut_entries) {
            put15(palette[i], le16(l->clut + i * 2u));
        } else {
            /* A CLUT shorter than the index range: missing entries are black. */
            palette[i][0] = palette[i][1] = palette[i][2] = 0;
            palette[i][3] = 255;
        }
    }
}

void tim_free(struct tim_image *image)
{
    free(image->rgba);
    image->rgba = NULL;
    image->width = image->height = 0;
}

unsigned tim_count(const uint8_t *data, size_t length)
{
    struct layout l;
    size_t pos = 0;
    unsigned count = 0;

    if (data == NULL)
        return 0;
    while (count < 65535u && parse(data + pos, length - pos, &l) == CODEC_OK) {
        pos += l.size;
        count++;
    }
    return count;
}

enum codec_result tim_decode(const uint8_t *data, size_t length, unsigned index,
                             struct tim_image *image)
{
    uint8_t palette[256][4];
    struct layout l;
    enum codec_result result;
    size_t pos = 0;
    unsigned i, x, y;

    if (image == NULL)
        return CODEC_INVALID;
    image->width = image->height = 0;
    image->rgba = NULL;
    if (data == NULL)
        return CODEC_TRUNCATED;
    for (i = 0;; i++) {
        result = parse(data + pos, length - pos, &l);
        /* Anything but a whole image before the one asked for means it isn't there. */
        if (result != CODEC_OK)
            return i == 0 || i == index ? result : CODEC_INVALID;
        if (i == index)
            break;
        pos += l.size;
    }
    if (l.mode < 2)
        make_palette(&l, l.mode == 0 ? 16u : 256u, palette);
    image->rgba = malloc((size_t)l.width * l.height * 4u);
    if (image->rgba == NULL)
        return CODEC_NO_MEMORY;
    image->width = l.width;
    image->height = l.height;
    for (y = 0; y < l.height; y++) {
        const uint8_t *src = l.pixels + y * l.row_bytes;
        uint8_t *dst = image->rgba + (size_t)y * l.width * 4u;
        for (x = 0; x < l.width; x++, dst += 4) {
            const uint8_t *colour;
            switch (l.mode) {
            case 0: /* the low nibble is the left pixel */
                colour = palette[(src[x / 2u] >> (x & 1u ? 4 : 0)) & 15u];
                break;
            case 1:
                colour = palette[src[x]];
                break;
            case 2:
                put15(dst, le16(src + x * 2u));
                continue;
            default:
                dst[0] = src[x * 3u];
                dst[1] = src[x * 3u + 1u];
                dst[2] = src[x * 3u + 2u];
                dst[3] = 255;
                continue;
            }
            dst[0] = colour[0];
            dst[1] = colour[1];
            dst[2] = colour[2];
            dst[3] = colour[3];
        }
    }
    return CODEC_OK;
}
