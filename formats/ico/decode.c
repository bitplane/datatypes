#include "decode.h"
#include <stdlib.h>
#include <string.h>

enum { BI_RGB = 0, BI_BITFIELDS = 3 };

/* What ico_entry learns from a BMP entry's header, for the decoder. */
struct dib {
    unsigned bpp, colors, palette_size;
    uint32_t masks[4];              /* red, green, blue, alpha */
    int bitfields;
    size_t palette, pixels, mask;   /* offsets within the entry */
    size_t stride, mask_stride;
    int has_mask;
};

static const uint8_t png_signature[8] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};

static unsigned le16(const uint8_t *p) { return (unsigned)p[0] | (unsigned)p[1] << 8; }

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint32_t be32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 |
           (uint32_t)p[2] << 8 | (uint32_t)p[3];
}

static enum codec_result check_size(uint32_t width, uint32_t height)
{
    if (width == 0 || height == 0)
        return CODEC_INVALID;
    if (width > ICO_MAX_SIDE || height > ICO_MAX_SIDE ||
        (uint64_t)width * height > ICO_MAX_PIXELS)
        return CODEC_TOO_LARGE;
    return CODEC_OK;
}

enum codec_result ico_directory(const uint8_t *data, size_t length,
                                unsigned *count, int *cursor)
{
    unsigned type;
    *count = 0;
    *cursor = 0;
    if (length < 6)
        return CODEC_TRUNCATED;
    type = le16(data + 2);
    if (le16(data) != 0 || (type != 1 && type != 2) || le16(data + 4) == 0)
        return CODEC_INVALID;
    if (length < 6u + 16u * (size_t)le16(data + 4))
        return CODEC_TRUNCATED;
    *count = le16(data + 4);
    *cursor = type == 2;
    return CODEC_OK;
}

static enum codec_result read_png(const uint8_t *p, size_t size, struct ico_entry *entry)
{
    static const unsigned channels[7] = {1, 0, 3, 1, 2, 0, 4};
    unsigned bits, type;
    enum codec_result result;
    if (size < 33)
        return CODEC_TRUNCATED;
    if (memcmp(p + 12, "IHDR", 4) != 0)
        return CODEC_INVALID;
    result = check_size(be32(p + 16), be32(p + 20));
    if (result != CODEC_OK)
        return result;
    bits = p[24];
    type = p[25];
    if (type > 6 || channels[type] == 0 ||
        (bits != 1 && bits != 2 && bits != 4 && bits != 8 && bits != 16))
        return CODEC_INVALID;
    entry->width = be32(p + 16);
    entry->height = be32(p + 20);
    entry->depth = type == 3 ? bits : bits * channels[type];
    entry->png = 1;
    return CODEC_OK;
}

/* Parse a BMP entry's header and find its palette, pixels and AND mask. size is
   the directory's idea of the entry's length; avail is what the file holds. */
static enum codec_result read_dib(const uint8_t *p, size_t size, size_t avail,
                                  struct ico_entry *entry, struct dib *dib)
{
    uint32_t header, width, compression = BI_RGB, used = 0;
    int32_t height;
    size_t pos, end, pixel_bytes, mask_bytes;
    enum codec_result result;

    memset(dib, 0, sizeof *dib);
    if (avail < 4)
        return CODEC_TRUNCATED;
    header = le32(p);
    if (header != 12 && header < 40)
        return CODEC_INVALID;
    if (avail < header)
        return CODEC_TRUNCATED;
    if (header == 12) {
        width = le16(p + 4);
        height = (int32_t)le16(p + 6);
        dib->bpp = le16(p + 10);
        if (dib->bpp == 16 || dib->bpp == 32)
            return CODEC_INVALID;
    } else {
        width = le32(p + 4);
        height = (int32_t)le32(p + 8);
        dib->bpp = le16(p + 14);
        compression = le32(p + 16);
        used = le32(p + 32);
    }
    /* The height covers the colour image and the AND mask below it. */
    if (width == 0 || width > 0x7fffffffu || height < 2)
        return CODEC_INVALID;
    result = check_size(width, (uint32_t)height / 2u);
    if (result != CODEC_OK)
        return result;
    if (dib->bpp != 1 && dib->bpp != 4 && dib->bpp != 8 && dib->bpp != 16 &&
        dib->bpp != 24 && dib->bpp != 32)
        return CODEC_INVALID;
    pos = header;
    if (compression == BI_BITFIELDS) {
        if (dib->bpp != 16 && dib->bpp != 32)
            return CODEC_INVALID;
        dib->bitfields = 1;
        /* A 40-byte header is followed by the masks; larger ones hold them. */
        if (header == 40) {
            if (avail - pos < 12)
                return CODEC_TRUNCATED;
            dib->masks[0] = le32(p + pos);
            dib->masks[1] = le32(p + pos + 4);
            dib->masks[2] = le32(p + pos + 8);
            pos += 12;
        } else {
            dib->masks[0] = le32(p + 40);
            dib->masks[1] = le32(p + 44);
            dib->masks[2] = le32(p + 48);
            if (header >= 56)
                dib->masks[3] = le32(p + 52);
        }
        /* 32-bit icons carry alpha in the byte that the colour masks leave. */
        if (dib->masks[3] == 0 && dib->bpp == 32 &&
            ((dib->masks[0] | dib->masks[1] | dib->masks[2]) & 0xff000000u) == 0)
            dib->masks[3] = 0xff000000u;
    } else if (compression != BI_RGB) {
        return CODEC_INVALID;
    }
    if (dib->bpp <= 8) {
        if (used > 256)
            return CODEC_INVALID;
        dib->palette_size = header == 12 ? 3 : 4;
        dib->colors = used != 0 ? used : 1u << dib->bpp;
        dib->palette = pos;
        if ((avail - pos) / dib->palette_size < dib->colors)
            return CODEC_TRUNCATED;
        pos += (size_t)dib->colors * dib->palette_size;
    }
    dib->stride = ((size_t)width * dib->bpp + 31u) / 32u * 4u;
    dib->mask_stride = ((size_t)width + 31u) / 32u * 4u;
    pixel_bytes = dib->stride * ((uint32_t)height / 2u);
    mask_bytes = dib->mask_stride * ((uint32_t)height / 2u);
    dib->pixels = pos;
    if (avail - pos < pixel_bytes)
        return CODEC_TRUNCATED;
    pos += pixel_bytes;
    /* An entry that ends with its pixels has no mask; a partial one is cut off.
       Some writers give a size too small even for the pixels: then the rest of
       the file is the entry, as other readers take it. */
    end = size >= pos ? size : avail;
    if (end - pos != 0) {
        if (end - pos < mask_bytes)
            return CODEC_TRUNCATED;
        dib->has_mask = 1;
        dib->mask = pos;
    }
    entry->width = width;
    entry->height = (uint32_t)height / 2u;
    entry->depth = dib->bpp;
    entry->png = 0;
    return CODEC_OK;
}

static enum codec_result find_entry(const uint8_t *data, size_t length, unsigned index,
                                    struct ico_entry *entry, struct dib *dib)
{
    const uint8_t *e;
    unsigned count;
    int cursor;
    uint32_t size, offset;
    enum codec_result result;

    memset(entry, 0, sizeof *entry);
    result = ico_directory(data, length, &count, &cursor);
    if (result != CODEC_OK)
        return result;
    if (index >= count)
        return CODEC_INVALID;
    e = data + 6 + 16u * (size_t)index;
    size = le32(e + 8);
    offset = le32(e + 12);
    if (offset > length || size > length - offset)
        return CODEC_TRUNCATED;
    if (cursor) {
        entry->hot_x = le16(e + 4);
        entry->hot_y = le16(e + 6);
    }
    entry->offset = offset;
    entry->size = size;
    if (length - offset >= 8 && memcmp(data + offset, png_signature, 8) == 0) {
        /* As with BMP entries, a size too small for the header means the rest. */
        if (size < 33)
            entry->size = length - offset;
        result = read_png(data + offset, entry->size, entry);
    }
    else
        result = read_dib(data + offset, size, length - offset, entry, dib);
    if (result != CODEC_OK)
        memset(entry, 0, sizeof *entry);
    return result;
}

enum codec_result ico_entry(const uint8_t *data, size_t length, unsigned index,
                            struct ico_entry *entry)
{
    struct dib dib;
    return find_entry(data, length, index, entry, &dib);
}

enum codec_result ico_best(const uint8_t *data, size_t length, unsigned *index)
{
    struct ico_entry entry;
    unsigned count, i;
    uint64_t best_area = 0, area;
    unsigned best_depth = 0;
    int cursor, found = 0;
    enum codec_result result, first = CODEC_OK;

    *index = 0;
    result = ico_directory(data, length, &count, &cursor);
    if (result != CODEC_OK)
        return result;
    for (i = 0; i < count; i++) {
        result = ico_entry(data, length, i, &entry);
        if (result != CODEC_OK) {
            if (first == CODEC_OK)
                first = result;
            continue;
        }
        area = (uint64_t)entry.width * entry.height;
        if (!found || area > best_area ||
            (area == best_area && entry.depth > best_depth)) {
            found = 1;
            best_area = area;
            best_depth = entry.depth;
            *index = i;
        }
    }
    return found ? CODEC_OK : first;
}

/* Scale the field that mask selects from value to 0..255. */
static uint8_t field(uint32_t value, uint32_t mask)
{
    uint64_t max;
    if (mask == 0)
        return 0;
    while ((mask & 1u) == 0) {
        mask >>= 1;
        value >>= 1;
    }
    max = mask;
    return (uint8_t)(((uint64_t)(value & mask) * 255u + max / 2u) / max);
}

void ico_free(struct ico_image *image)
{
    free(image->rgba);
    image->rgba = NULL;
    image->width = image->height = 0;
}

void ico_fix_alpha(uint8_t *rgba, size_t pixels)
{
    size_t i;
    for (i = 0; i < pixels; i++)
        if (rgba[i * 4 + 3] != 0)
            return;
    for (i = 0; i < pixels; i++)
        rgba[i * 4 + 3] = 255;
}

enum codec_result ico_decode_bmp(const uint8_t *data, size_t length,
                                 const struct ico_entry *wanted,
                                 struct ico_image *image)
{
    static const uint32_t masks555[4] = {0x7c00, 0x03e0, 0x001f, 0};
    struct ico_entry entry;
    struct dib dib;
    const uint8_t *p, *row, *palette, *c;
    const uint32_t *masks;
    uint8_t *out;
    size_t x, y, pixels, index;
    uint32_t value;
    int has_alpha = 0, alpha_seen = 0;
    enum codec_result result;
    unsigned i;

    image->width = image->height = 0;
    image->rgba = NULL;
    /* Parse again from the directory position, so a stale description can't mislead. */
    if (wanted->png || wanted->offset > length || wanted->size > length - wanted->offset)
        return CODEC_INVALID;
    memset(&entry, 0, sizeof entry);
    result = read_dib(data + wanted->offset, wanted->size,
                      length - wanted->offset, &entry, &dib);
    if (result != CODEC_OK)
        return result;
    p = data + wanted->offset;
    pixels = (size_t)entry.width * entry.height;
    out = malloc(pixels * 4u);
    if (out == NULL)
        return CODEC_NO_MEMORY;
    palette = p + dib.palette;
    masks = dib.bitfields ? dib.masks : masks555;
    if (dib.bpp == 32)
        has_alpha = !dib.bitfields || dib.masks[3] != 0;
    else if (dib.bpp == 16)
        has_alpha = dib.bitfields && dib.masks[3] != 0;
    for (y = 0; y < entry.height; y++) {
        uint8_t *o = out + y * entry.width * 4u;
        row = p + dib.pixels + (entry.height - 1u - y) * dib.stride;
        for (x = 0; x < entry.width; x++, o += 4) {
            switch (dib.bpp) {
            case 1: case 4: case 8:
                index = (row[x * dib.bpp / 8u] >> (8u - dib.bpp - x * dib.bpp % 8u)) &
                        ((1u << dib.bpp) - 1u);
                if (index < dib.colors) {
                    c = palette + index * dib.palette_size;
                    o[0] = c[2]; o[1] = c[1]; o[2] = c[0];
                } else {
                    o[0] = o[1] = o[2] = 0;
                }
                o[3] = 255;
                break;
            case 16:
                value = le16(row + x * 2u);
                for (i = 0; i < 3; i++)
                    o[i] = field(value, masks[i]);
                o[3] = has_alpha ? field(value, masks[3]) : 255;
                break;
            case 24:
                o[0] = row[x * 3u + 2]; o[1] = row[x * 3u + 1]; o[2] = row[x * 3u];
                o[3] = 255;
                break;
            default:
                if (dib.bitfields) {
                    value = le32(row + x * 4u);
                    for (i = 0; i < 3; i++)
                        o[i] = field(value, masks[i]);
                    o[3] = has_alpha ? field(value, masks[3]) : 255;
                } else {
                    o[0] = row[x * 4u + 2]; o[1] = row[x * 4u + 1]; o[2] = row[x * 4u];
                    o[3] = row[x * 4u + 3];
                }
                break;
            }
            if (has_alpha && o[3] != 0)
                alpha_seen = 1;
        }
    }
    /* Alpha that is all zero was never filled in, so the AND mask applies instead. */
    if (!has_alpha || !alpha_seen) {
        for (y = 0; y < entry.height; y++) {
            row = dib.has_mask ? p + dib.mask + (entry.height - 1u - y) * dib.mask_stride : NULL;
            for (x = 0; x < entry.width; x++)
                out[(y * entry.width + x) * 4u + 3] =
                    row != NULL && (row[x / 8u] & (0x80u >> (x % 8u))) ? 0 : 255;
        }
    }
    image->width = entry.width;
    image->height = entry.height;
    image->rgba = out;
    return CODEC_OK;
}
