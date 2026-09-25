#include "decode.h"
#include <stdlib.h>
#include <string.h>

#define PALM_MAX_PIXELS (16u * 1024u * 1024u)

#define FLAG_COMPRESSED 0x8000u
#define FLAG_COLORMAP 0x4000u
#define FLAG_TRANSPARENT 0x2000u

enum { COMPRESS_SCANLINE = 0, COMPRESS_RLE = 1, COMPRESS_PACKBITS = 2,
       COMPRESS_NONE = 0xff };
enum { FORMAT_INDEXED = 0, FORMAT_565 = 1 };

struct header {
    size_t table, data;
    unsigned long key, next;
    unsigned width, height, row_bytes, flags, depth, version, compression;
    unsigned table_count;
    int direct, has_table;
};

struct family {
    const uint8_t *data;
    size_t length, offset;
    unsigned count;
    int done;
};

static unsigned be16(const uint8_t *p)
{
    return ((unsigned)p[0] << 8) | p[1];
}

static unsigned long be32(const uint8_t *p)
{
    return ((unsigned long)p[0] << 24) | ((unsigned long)p[1] << 16) |
           ((unsigned long)p[2] << 8) | (unsigned long)p[3];
}

static unsigned rgb565(unsigned r, unsigned g, unsigned b)
{
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
}

/* ImageMagick flags 1, 2 and 4-bit bitmaps as having a colour table but
   writes none. Believe the flag only when a table of at most 2^depth
   entries fits in the bitmap and, uncompressed, still leaves room for the
   rows. `left` ends at the next bitmap when there is one. */
static int plausible_table(const uint8_t *p, size_t left, const struct header *h)
{
    unsigned count;
    size_t size;

    if (h->depth >= 8 || left < 2)
        return 1;
    count = be16(p);
    size = 2u + (size_t)count * 4u;
    return count >= 1 && count <= (1u << h->depth) && size <= left &&
           (h->compression != COMPRESS_NONE ||
            left - size >= (size_t)h->row_bytes * h->height);
}

/* Read the header, colour table and direct colour info of the bitmap at base. */
static enum codec_result parse(const uint8_t *data, size_t length, size_t base,
                               struct header *h)
{
    const uint8_t *p = data + base;
    size_t left = length - base, pos, size, span;

    if (left < 16)
        return CODEC_TRUNCATED;
    memset(h, 0, sizeof *h);
    h->width = be16(p);
    h->height = be16(p + 2);
    h->row_bytes = be16(p + 4);
    h->flags = be16(p + 6);
    h->depth = p[8] == 0 ? 1u : p[8];
    h->version = p[9];
    if (h->version > 3 || h->width == 0 || h->height == 0 ||
        (h->depth != 1 && h->depth != 2 && h->depth != 4 &&
         h->depth != 8 && h->depth != 16))
        return CODEC_INVALID;
    if ((unsigned long)h->width * h->height > PALM_MAX_PIXELS)
        return CODEC_TOO_LARGE;
    if ((unsigned long)h->row_bytes * 8u < (unsigned long)h->width * h->depth)
        return CODEC_INVALID;
    if (h->version < 3) {
        h->next = (unsigned long)be16(p + 10) * 4u;
        h->key = p[12];
        h->compression = p[13];
        /* The depth decides: ImageMagick sets the direct colour flag on
           8-bit bitmaps that use the system palette. */
        h->direct = h->depth == 16;
        pos = 16;
    } else {
        if (left < 24)
            return CODEC_TRUNCATED;
        /* The size byte gives the header length; anything past 24 is skipped. */
        pos = p[10] > 24 ? p[10] : 24;
        if (p[11] != FORMAT_INDEXED && p[11] != FORMAT_565)
            return CODEC_INVALID;
        h->direct = p[11] == FORMAT_565;
        if (h->direct != (h->depth == 16))
            return CODEC_INVALID;
        h->compression = p[13];
        h->key = be32(p + 16);
        if (h->direct)
            h->key &= 0xffffu;
        h->next = be32(p + 20);
    }
    if (!(h->flags & FLAG_COMPRESSED))
        h->compression = COMPRESS_NONE;
    else if (h->compression != COMPRESS_SCANLINE && h->compression != COMPRESS_RLE &&
             h->compression != COMPRESS_PACKBITS && h->compression != COMPRESS_NONE)
        return CODEC_INVALID;
    if (left < pos)
        return CODEC_TRUNCATED;
    span = h->next != 0 && h->next < left ? (size_t)h->next : left;
    if ((h->flags & FLAG_COLORMAP) &&
        plausible_table(p + pos, span > pos ? span - pos : 0, h)) {
        if (left - pos < 2)
            return CODEC_TRUNCATED;
        h->table_count = be16(p + pos);
        size = 2u + (size_t)h->table_count * 4u;
        if (left - pos < size)
            return CODEC_TRUNCATED;
        h->table = base + pos + 2u;
        h->has_table = 1;
        pos += size;
    }
    if (h->direct && h->version < 3) {
        if (left - pos < 8)
            return CODEC_TRUNCATED;
        if (p[pos] != 5 || p[pos + 1] != 6 || p[pos + 2] != 5)
            return CODEC_INVALID;
        h->key = rgb565(p[pos + 5], p[pos + 6], p[pos + 7]);
        pos += 8;
    }
    /* Compressed data starts with its size, which the rows make redundant. */
    if (h->compression != COMPRESS_NONE) {
        size = h->version < 3 ? 2u : 4u;
        if (left - pos < size)
            return CODEC_TRUNCATED;
        pos += size;
    }
    h->data = base + pos;
    return CODEC_OK;
}

/* Step to the next bitmap in the family. Returns 0 at the end, with *error
   saying why the family ended. */
static int next_bitmap(struct family *f, struct header *h, enum codec_result *error)
{
    *error = CODEC_INVALID;
    while (!f->done && f->count < PALM_MAX_BITMAPS) {
        if (f->length - f->offset < 16) {
            *error = CODEC_TRUNCATED;
            break;
        }
        /* A high-density separator is a 16-byte header with pixel size 255. */
        if (f->data[f->offset + 8] == 0xff) {
            f->offset += 16;
            continue;
        }
        *error = parse(f->data, f->length, f->offset, h);
        if (*error != CODEC_OK)
            break;
        if (h->next == 0 || h->next >= f->length - f->offset)
            f->done = 1;
        else
            f->offset += h->next;
        f->count++;
        return 1;
    }
    f->done = 1;
    return 0;
}

static void start(struct family *f, const uint8_t *data, size_t length)
{
    f->data = data;
    f->length = length;
    f->offset = 0;
    f->count = 0;
    f->done = 0;
}

enum codec_result palm_count(const uint8_t *data, size_t length, unsigned *count)
{
    struct family f;
    struct header h;
    enum codec_result error;

    if (count == NULL)
        return CODEC_INVALID;
    *count = 0;
    if (data == NULL)
        return CODEC_TRUNCATED;
    start(&f, data, length);
    while (next_bitmap(&f, &h, &error))
        ++*count;
    return *count != 0 ? CODEC_OK : error;
}

enum codec_result palm_best(const uint8_t *data, size_t length, unsigned *index)
{
    struct family f;
    struct header h;
    enum codec_result error;
    unsigned long area, best_area = 0;
    unsigned best_depth = 0, i = 0;

    if (index == NULL)
        return CODEC_INVALID;
    *index = 0;
    if (data == NULL)
        return CODEC_TRUNCATED;
    start(&f, data, length);
    while (next_bitmap(&f, &h, &error)) {
        area = (unsigned long)h.width * h.height;
        if (area > best_area || (area == best_area && h.depth > best_depth)) {
            best_area = area;
            best_depth = h.depth;
            *index = i;
        }
        i++;
    }
    return i != 0 ? CODEC_OK : error;
}

static void system_color(unsigned i, uint8_t *rgb)
{
    static const uint8_t grays[10] = {
        0x11, 0x22, 0x44, 0x55, 0x77, 0x88, 0xaa, 0xbb, 0xdd, 0xee};
    static const uint8_t extras[5][3] = {
        {192,192,192}, {128,0,0}, {128,0,128}, {0,128,0}, {0,128,128}};
    unsigned j = i % 108u;

    rgb[0] = rgb[1] = rgb[2] = 0;
    if (i < 215) {
        /* A 6x6x6 cube from white down, blue's high half first; black is left out. */
        rgb[0] = (uint8_t)(255u - 51u * (j / 18u));
        rgb[1] = (uint8_t)(255u - 51u * (j % 6u));
        rgb[2] = (uint8_t)(255u - 51u * ((i / 108u) * 3u + (j % 18u) / 6u));
    } else if (i < 225) {
        rgb[0] = rgb[1] = rgb[2] = grays[i - 215];
    } else if (i < 230) {
        memcpy(rgb, extras[i - 225], 3);
    }
}

static void make_palette(const uint8_t *data, const struct header *h,
                         uint8_t palette[256][3])
{
    unsigned i, colors = 1u << (h->depth > 8 ? 8 : h->depth);

    memset(palette, 0, 256 * 3);
    if (h->has_table) {
        /* Entries are used in table order; their index bytes are ignored. */
        for (i = 0; i < h->table_count && i < 256; i++)
            memcpy(palette[i], data + h->table + i * 4u + 1u, 3);
    } else if (h->depth == 8) {
        for (i = 0; i < 256; i++)
            system_color(i, palette[i]);
    } else {
        for (i = 0; i < colors; i++)
            palette[i][0] = palette[i][1] = palette[i][2] =
                (uint8_t)(255u - i * 255u / (colors - 1u));
    }
}

/* Unpack one row into `row`, which still holds the previous row. */
static enum codec_result unpack_row(const uint8_t *data, size_t length, size_t *pos,
                                    const struct header *h, unsigned y, uint8_t *row)
{
    size_t p = *pos, n = h->row_bytes, j = 0, k, run, unit, copy;
    unsigned mask;

    switch (h->compression) {
    case COMPRESS_NONE:
        if (length - p < n)
            return CODEC_TRUNCATED;
        memcpy(row, data + p, n);
        p += n;
        break;
    case COMPRESS_SCANLINE:
        /* Each flag byte marks which of the next 8 bytes are new; the rest repeat
           the row above. The first row is always stored in full. */
        for (j = 0; j < n; j += 8) {
            if (p == length)
                return CODEC_TRUNCATED;
            mask = data[p++];
            for (k = 0; k < 8 && j + k < n; k++) {
                if (y == 0 || (mask & (0x80u >> k))) {
                    if (p == length)
                        return CODEC_TRUNCATED;
                    row[j + k] = data[p++];
                }
            }
        }
        break;
    case COMPRESS_RLE:
        /* Count and value pairs; a run past the end of the row is cut short. */
        while (j < n) {
            if (length - p < 2)
                return CODEC_TRUNCATED;
            run = data[p];
            if (run > n - j)
                run = n - j;
            memset(row + j, data[p + 1], run);
            j += run;
            p += 2;
        }
        break;
    default:
        /* PackBits over bytes, or over 16-bit words for direct colour.
           0x80 is a run of 129, as netpbm reads it. */
        unit = h->depth == 16 ? 2u : 1u;
        while (j < n) {
            if (p == length)
                return CODEC_TRUNCATED;
            run = data[p++];
            if (run >= 128) {
                run = 257u - run;
                if (length - p < unit)
                    return CODEC_TRUNCATED;
                for (k = 0; k < run && j < n; k++) {
                    row[j++] = data[p];
                    if (unit == 2 && j < n)
                        row[j++] = data[p + 1];
                }
                p += unit;
            } else {
                run = (run + 1u) * unit;
                if (length - p < run)
                    return CODEC_TRUNCATED;
                copy = run < n - j ? run : n - j;
                memcpy(row + j, data + p, copy);
                j += copy;
                p += run;
            }
        }
        break;
    }
    *pos = p;
    return CODEC_OK;
}

static enum codec_result decode_bitmap(const uint8_t *data, size_t length,
                                       const struct header *h, uint8_t *rgba)
{
    uint8_t palette[256][3], *row, *out = rgba;
    size_t pos = h->data;
    unsigned x, y, value, shift, mask = (1u << (h->depth & 15u)) - 1u;
    int keyed = (h->flags & FLAG_TRANSPARENT) != 0;
    enum codec_result result = CODEC_OK;

    if (h->data > length)
        return CODEC_TRUNCATED;
    row = calloc(h->row_bytes, 1);
    if (row == NULL)
        return CODEC_NO_MEMORY;
    make_palette(data, h, palette);
    for (y = 0; y < h->height && result == CODEC_OK; y++) {
        result = unpack_row(data, length, &pos, h, y, row);
        if (result != CODEC_OK)
            break;
        for (x = 0; x < h->width; x++, out += 4) {
            if (h->direct) {
                value = be16(row + (size_t)x * 2u);
                out[0] = (uint8_t)((value >> 11) * 255u / 31u);
                out[1] = (uint8_t)(((value >> 5) & 63u) * 255u / 63u);
                out[2] = (uint8_t)((value & 31u) * 255u / 31u);
            } else {
                shift = 8u - h->depth - (x * h->depth) % 8u;
                value = (row[(size_t)x * h->depth / 8u] >> shift) & mask;
                memcpy(out, palette[value], 3);
            }
            out[3] = keyed && value == h->key ? 0 : 255;
        }
    }
    free(row);
    return result;
}

void palm_free(struct palm_image *image)
{
    free(image->rgba);
    image->rgba = NULL;
    image->width = image->height = 0;
}

enum codec_result palm_decode(const uint8_t *data, size_t length, unsigned index,
                              struct palm_image *image)
{
    struct family f;
    struct header h;
    enum codec_result result;
    unsigned i;

    if (image == NULL)
        return CODEC_INVALID;
    image->width = image->height = 0;
    image->rgba = NULL;
    if (data == NULL)
        return CODEC_TRUNCATED;
    start(&f, data, length);
    for (i = 0; i <= index; i++)
        if (!next_bitmap(&f, &h, &result))
            return i == 0 ? result : CODEC_INVALID;
    image->rgba = malloc((size_t)h.width * h.height * 4u);
    if (image->rgba == NULL)
        return CODEC_NO_MEMORY;
    result = decode_bitmap(data, length, &h, image->rgba);
    if (result != CODEC_OK) {
        palm_free(image);
        return result;
    }
    image->width = h.width;
    image->height = h.height;
    return CODEC_OK;
}
