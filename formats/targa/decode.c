#include "decode.h"

#include <stdlib.h>
#include <string.h>

/* Keep the decoded image below 64 MiB on small AROS machines. */
#define TGA_MAX_PIXELS (16u * 1024u * 1024u)

struct reader {
    const uint8_t *data;
    size_t size;
    size_t pos;
};

static unsigned le16(const uint8_t *p)
{
    return (unsigned)p[0] | ((unsigned)p[1] << 8);
}

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int take(struct reader *r, uint8_t *out, size_t count)
{
    if (count > r->size - r->pos)
        return 0;
    if (out != NULL)
        memcpy(out, r->data + r->pos, count);
    r->pos += count;
    return 1;
}

static void color555(uint8_t *rgba, unsigned value, int alpha)
{
    unsigned r = (value >> 10) & 31u;
    unsigned g = (value >> 5) & 31u;
    unsigned b = value & 31u;
    rgba[0] = (uint8_t)((r << 3) | (r >> 2));
    rgba[1] = (uint8_t)((g << 3) | (g >> 2));
    rgba[2] = (uint8_t)((b << 3) | (b >> 2));
    rgba[3] = alpha && !(value & 0x8000u) ? 0 : 255;
}

static int color(uint8_t *rgba, const uint8_t *src, unsigned depth,
                 int grayscale, unsigned attribute_bits)
{
    if (grayscale) {
        if (depth != 8 && depth != 16)
            return 0;
        rgba[0] = rgba[1] = rgba[2] = src[0];
        rgba[3] = depth == 16 && attribute_bits != 0 ? src[1] : 255;
        return 1;
    }
    switch (depth) {
    case 8:
        rgba[0] = rgba[1] = rgba[2] = src[0];
        rgba[3] = 255;
        return 1;
    case 15:
    case 16:
        color555(rgba, le16(src), depth == 16 && attribute_bits != 0);
        return 1;
    case 24:
    case 32:
        rgba[0] = src[2];
        rgba[1] = src[1];
        rgba[2] = src[0];
        rgba[3] = depth == 32 && attribute_bits != 0 ? src[3] : 255;
        return 1;
    default:
        return 0;
    }
}

static enum tga_result read_pixel(struct reader *r, uint8_t *rgba,
                                  unsigned image_type, unsigned depth,
                                  unsigned attribute_bits,
                                  const uint8_t *palette,
                                  unsigned palette_first,
                                  unsigned palette_count)
{
    uint8_t raw[4];
    unsigned bytes = (depth + 7u) / 8u;
    unsigned index;
    if (bytes == 0 || bytes > sizeof raw)
        return TGA_INVALID;
    if (!take(r, raw, bytes))
        return TGA_TRUNCATED;
    if (image_type == 1) {
        index = depth == 8 ? raw[0] : le16(raw);
        if (index < palette_first || index - palette_first >= palette_count)
            return TGA_INVALID;
        memcpy(rgba, palette + (index - palette_first) * 4u, 4);
    } else if (!color(rgba, raw, depth, image_type == 3, attribute_bits)) {
        return TGA_INVALID;
    }
    return TGA_OK;
}

void tga_free(struct tga_image *image)
{
    free(image->rgba);
    image->rgba = NULL;
    image->width = image->height = 0;
}

enum tga_result tga_decode(const uint8_t *data, size_t length,
                           struct tga_image *image)
{
    struct reader r;
    uint8_t *palette = NULL;
    uint8_t pixel[4];
    unsigned type, base_type, depth, width, height, descriptor;
    unsigned palette_first, palette_count, palette_depth, palette_bytes;
    unsigned i, n, packet, run, source_x, source_y, dest_x, dest_y;
    unsigned alpha_type = 3;
    size_t pixels, offset;
    enum tga_result result;

    if (image == NULL)
        return TGA_INVALID;
    image->width = image->height = 0;
    image->rgba = NULL;
    if (data == NULL || length < 18)
        return TGA_TRUNCATED;
    type = data[2];
    base_type = type >= 8 ? type - 8 : type;
    if ((type != 1 && type != 2 && type != 3 &&
         type != 9 && type != 10 && type != 11) ||
        data[1] > 1u || (base_type == 1 && data[1] != 1u))
        return TGA_INVALID;
    palette_first = le16(data + 3);
    palette_count = le16(data + 5);
    palette_depth = data[7];
    width = le16(data + 12);
    height = le16(data + 14);
    depth = data[16];
    descriptor = data[17];
    if (length >= 26 &&
        memcmp(data + length - 18, "TRUEVISION-XFILE.\0", 18) == 0) {
        uint32_t extension = le32(data + length - 26);
        if (extension != 0) {
            if (extension > length - 26 || length - 26 - extension < 495 ||
                le16(data + extension) < 495)
                return TGA_INVALID;
            alpha_type = data[extension + 494];
        }
    }
    if (width == 0 || height == 0 || (descriptor & 0xc0u) != 0)
        return TGA_INVALID;
    if ((base_type == 1 && (depth != 8 && depth != 16)) ||
        (base_type == 2 && (depth != 15 && depth != 16 &&
                            depth != 24 && depth != 32)) ||
        (base_type == 3 && (depth != 8 && depth != 16)))
        return TGA_INVALID;
    if (base_type == 1 && (palette_count == 0 ||
        (palette_depth != 8 && palette_depth != 15 &&
         palette_depth != 16 && palette_depth != 24 && palette_depth != 32) ||
        palette_first + palette_count > 65536u))
        return TGA_INVALID;
    pixels = (size_t)width * height;
    if (pixels > TGA_MAX_PIXELS)
        return TGA_TOO_LARGE;
    r.data = data;
    r.size = length;
    r.pos = 18;
    if (!take(&r, NULL, data[0]))
        return TGA_TRUNCATED;
    /* True-colour and grayscale images may carry an unused colour map. */
    if (base_type != 1 && data[1] == 1u &&
        !take(&r, NULL, (size_t)palette_count * ((palette_depth + 7u) / 8u)))
        return TGA_TRUNCATED;
    if (base_type == 1) {
        palette = malloc((size_t)palette_count * 4u);
        if (palette == NULL)
            return TGA_NO_MEMORY;
        palette_bytes = (palette_depth + 7u) / 8u;
        for (i = 0; i < palette_count; i++) {
            uint8_t raw[4];
            if (!take(&r, raw, palette_bytes)) {
                result = TGA_TRUNCATED;
                goto fail;
            }
            /* Palette alpha counts only when the pixels declare attribute bits. */
            if (!color(palette + i * 4u, raw, palette_depth, 0,
                       (descriptor & 15u) == 0 ? 0u :
                       palette_depth == 16 ? 1u : palette_depth == 32 ? 8u : 0u)) {
                result = TGA_INVALID;
                goto fail;
            }
        }
    }
    image->rgba = malloc(pixels * 4u);
    if (image->rgba == NULL) {
        result = TGA_NO_MEMORY;
        goto fail;
    }
    image->width = width;
    image->height = height;
    for (i = 0; i < pixels;) {
        if (type >= 8) {
            if (!take(&r, pixel, 1)) {
                result = TGA_TRUNCATED;
                goto fail;
            }
            packet = pixel[0];
            n = (packet & 0x7fu) + 1u;
            run = packet & 0x80u;
        } else {
            n = 1;
            run = 0;
        }
        if (n > pixels - i) {
            result = TGA_INVALID;
            goto fail;
        }
        if (run) {
            result = read_pixel(&r, pixel, base_type, depth,
                                descriptor & 15u, palette,
                                palette_first, palette_count);
            if (result != TGA_OK)
                goto fail;
        }
        while (n-- != 0) {
            if (!run) {
                result = read_pixel(&r, pixel, base_type, depth,
                                    descriptor & 15u, palette,
                                    palette_first, palette_count);
                if (result != TGA_OK)
                    goto fail;
            }
            source_x = i % width;
            source_y = i / width;
            dest_x = descriptor & 0x10u ? width - source_x - 1u : source_x;
            dest_y = descriptor & 0x20u ? source_y : height - source_y - 1u;
            offset = ((size_t)dest_y * width + dest_x) * 4u;
            memcpy(image->rgba + offset, pixel, 4);
            i++;
        }
    }
    if (alpha_type != 2 && alpha_type != 3) {
        for (i = 0; i < pixels; i++) {
            uint8_t *rgba = image->rgba + (size_t)i * 4u;
            unsigned a = rgba[3];
            if (alpha_type == 4 && a != 0 && a != 255) {
                unsigned channel;
                for (channel = 0; channel < 3; channel++) {
                    unsigned straight = (rgba[channel] * 255u + a / 2u) / a;
                    rgba[channel] = (uint8_t)(straight > 255u ? 255u : straight);
                }
            } else if (alpha_type == 4 && a == 0) {
                rgba[0] = rgba[1] = rgba[2] = 0;
            }
            if (alpha_type != 4)
                rgba[3] = 255;
        }
    }
    free(palette);
    return TGA_OK;
fail:
    free(palette);
    tga_free(image);
    return result;
}
