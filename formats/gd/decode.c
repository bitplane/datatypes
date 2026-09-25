#include "decode.h"
#include "common/zlib.h"
#include <stdlib.h>
#include <string.h>

#define GD_MAX_PIXELS (16u * 1024u * 1024u)
#define GD2_HEADER 18u

/* A parsed colour block. Palette alpha is already 8-bit and straight, and
   the transparent index is already cleared. */
struct gd_colors {
    int truecolor;
    int has_transparent;
    uint32_t transparent;
    uint8_t palette[256][4];
};

static unsigned get16(const uint8_t *p)
{
    return ((unsigned)p[0] << 8) | p[1];
}

static uint32_t get32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

/* gd alpha is 7-bit, 0 opaque to 127 clear; this is libgd's own PNG mapping. */
static uint8_t gd_alpha(unsigned alpha)
{
    alpha &= 0x7fu;
    return (uint8_t)(255u - ((alpha << 1) + (alpha >> 6)));
}

static enum codec_result check_size(unsigned width, unsigned height)
{
    if (width == 0 || height == 0)
        return CODEC_INVALID;
    if ((uint32_t)width * height > GD_MAX_PIXELS)
        return CODEC_TOO_LARGE;
    return CODEC_OK;
}

/* libgd's colour block at *pos: the 2.x layout when extended, else 1.x's. */
static enum codec_result read_colors(const uint8_t *data, size_t length, size_t *pos,
                                     int extended, struct gd_colors *colors)
{
    size_t p = *pos, entry = extended ? 4u : 3u;
    unsigned count = 0, i;
    uint32_t transparent;

    if (extended) {
        if (length - p < 1)
            return CODEC_TRUNCATED;
        if (data[p++] != (colors->truecolor ? 1 : 0))
            return CODEC_INVALID;
        if (!colors->truecolor) {
            if (length - p < 2)
                return CODEC_TRUNCATED;
            count = get16(data + p);
            p += 2;
            if (count > 256)
                return CODEC_INVALID;
        }
        if (length - p < 4)
            return CODEC_TRUNCATED;
        transparent = get32(data + p);
        p += 4;
    } else {
        if (length - p < 3)
            return CODEC_TRUNCATED;
        count = data[p];
        transparent = get16(data + p + 1);
        p += 3;
    }
    colors->transparent = transparent;
    if (colors->truecolor) {
        /* -1 is libgd's "none"; any other value is a whole ARGB pixel. */
        colors->has_transparent = transparent != 0xffffffffu;
        *pos = p;
        return CODEC_OK;
    }
    colors->has_transparent = transparent < count;
    if (length - p < 256u * entry)
        return CODEC_TRUNCATED;
    for (i = 0; i < 256; i++, p += entry) {
        memcpy(colors->palette[i], data + p, 3);
        colors->palette[i][3] = extended ? gd_alpha(data[p + 3]) : 255;
    }
    if (colors->has_transparent)
        colors->palette[transparent][3] = 0;
    *pos = p;
    return CODEC_OK;
}

static void put_pixel(uint8_t *out, const struct gd_colors *colors, const uint8_t *src)
{
    if (!colors->truecolor) {
        memcpy(out, colors->palette[src[0]], 4);
        return;
    }
    out[0] = src[1];
    out[1] = src[2];
    out[2] = src[3];
    if (colors->has_transparent && get32(src) == colors->transparent)
        out[3] = 0;
    else
        out[3] = gd_alpha(src[0]);
}

static enum codec_result allocate(struct gd_image *image, unsigned width, unsigned height)
{
    image->rgba = malloc((size_t)width * height * 4u);
    if (image->rgba == NULL)
        return CODEC_NO_MEMORY;
    image->width = width;
    image->height = height;
    return CODEC_OK;
}

static enum codec_result decode_gd(const uint8_t *data, size_t length, struct gd_image *image)
{
    struct gd_colors colors;
    size_t pos = 0, count, bpp, i;
    unsigned width, height, signature;
    int extended = 0;
    enum codec_result result;

    colors.truecolor = 0;
    if (length < 2)
        return CODEC_TRUNCATED;
    signature = get16(data);
    if (signature == 0xffffu || signature == 0xfffeu) {
        extended = 1;
        colors.truecolor = signature == 0xfffeu;
        pos = 2;
    }
    if (length - pos < 4)
        return CODEC_TRUNCATED;
    width = get16(data + pos);
    height = get16(data + pos + 2);
    pos += 4;
    if ((result = check_size(width, height)) != CODEC_OK ||
        (result = read_colors(data, length, &pos, extended, &colors)) != CODEC_OK)
        return result;
    bpp = colors.truecolor ? 4u : 1u;
    count = (size_t)width * height;
    if ((length - pos) / bpp < count)
        return CODEC_TRUNCATED;
    if ((result = allocate(image, width, height)) != CODEC_OK)
        return result;
    for (i = 0; i < count; i++)
        put_pixel(image->rgba + i * 4u, &colors, data + pos + i * bpp);
    return CODEC_OK;
}

static enum codec_result decode_gd2(const uint8_t *data, size_t length, struct gd_image *image)
{
    struct gd_colors colors;
    unsigned version, width, height, chunk, format, across, down, cx, cy, x, y, w, h;
    size_t pos = GD2_HEADER, index = GD2_HEADER, bpp, size, capacity = 0, written;
    uint32_t offset, packed;
    int compressed;
    uint8_t *buffer = NULL;
    const uint8_t *src;
    enum codec_result result = CODEC_OK;

    if (length < GD2_HEADER)
        return CODEC_TRUNCATED;
    version = get16(data + 4);
    width = get16(data + 6);
    height = get16(data + 8);
    chunk = get16(data + 10);
    format = get16(data + 12);
    across = get16(data + 14);
    down = get16(data + 16);
    if ((version != 1 && version != 2) || chunk < 64 || chunk > 4096 ||
        format < 1 || format > 4)
        return CODEC_INVALID;
    if ((result = check_size(width, height)) != CODEC_OK)
        return result;
    /* The chunk grid must cover the image; libgd writes it exactly. */
    if ((uint32_t)across * chunk < width || (uint32_t)down * chunk < height)
        return CODEC_INVALID;
    colors.truecolor = format >= 3;
    compressed = format == 2 || format == 4;
    bpp = colors.truecolor ? 4u : 1u;
    if (compressed) {
        if ((length - pos) / 8u / across < down)
            return CODEC_TRUNCATED;
        pos += (size_t)across * down * 8u;
    }
    if ((result = read_colors(data, length, &pos, version == 2, &colors)) != CODEC_OK)
        return result;
    if (compressed) {
        capacity = (size_t)(chunk < width ? chunk : width) *
                   (chunk < height ? chunk : height) * bpp;
        if ((buffer = malloc(capacity)) == NULL)
            return CODEC_NO_MEMORY;
    }
    if ((result = allocate(image, width, height)) != CODEC_OK) {
        free(buffer);
        return result;
    }
    for (cy = 0; cy < down && result == CODEC_OK; cy++) {
        for (cx = 0; cx < across; cx++, index += 8u) {
            /* Chunks past the image hold no pixels; libgd skips them too. */
            if (cx * chunk >= width || cy * chunk >= height)
                continue;
            w = width - cx * chunk < chunk ? width - cx * chunk : chunk;
            h = height - cy * chunk < chunk ? height - cy * chunk : chunk;
            size = (size_t)w * h * bpp;
            if (compressed) {
                offset = get32(data + index);
                packed = get32(data + index + 4);
                if ((offset | packed) & 0x80000000u) {
                    result = CODEC_INVALID;
                    break;
                }
                if (offset > length || packed > length - offset) {
                    result = CODEC_TRUNCATED;
                    break;
                }
                result = zlib_inflate(data + offset, packed, buffer, size, &written);
                if (result == CODEC_NO_MEMORY)
                    break;
                if (result != CODEC_OK || written != size) {
                    result = CODEC_INVALID;
                    break;
                }
                src = buffer;
            } else {
                if (length - pos < size) {
                    result = CODEC_TRUNCATED;
                    break;
                }
                src = data + pos;
                pos += size;
            }
            for (y = 0; y < h; y++)
                for (x = 0; x < w; x++)
                    put_pixel(image->rgba +
                                  (((size_t)cy * chunk + y) * width + cx * chunk + x) * 4u,
                              &colors, src + ((size_t)y * w + x) * bpp);
        }
    }
    free(buffer);
    if (result != CODEC_OK)
        gd_free(image);
    return result;
}

enum codec_result gd_decode(const uint8_t *data, size_t length, struct gd_image *image)
{
    if (image == NULL)
        return CODEC_INVALID;
    image->width = image->height = 0;
    image->rgba = NULL;
    if (data == NULL)
        return CODEC_INVALID;
    if (length >= 4 && memcmp(data, "gd2", 4) == 0)
        return decode_gd2(data, length, image);
    return decode_gd(data, length, image);
}

void gd_free(struct gd_image *image)
{
    if (image == NULL)
        return;
    free(image->rgba);
    image->rgba = NULL;
    image->width = image->height = 0;
}
