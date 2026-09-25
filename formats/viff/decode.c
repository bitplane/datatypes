#include "decode.h"
#include <stdlib.h>

#define VIFF_MAGIC 0xabu
#define VIFF_FILE_TYPE 1u
#define VIFF_HEADER 1024u
#define VIFF_FIELDS 520u
#define VIFF_MAX_SIDE 65535u
#define VIFF_MAX_PIXELS (16u * 1024u * 1024u)

/* Machine dependencies with little-endian data; the rest are big-endian. */
enum { DEP_DEC = 4, DEP_NS = 8 };
enum { TYP_BIT = 0, TYP_1_BYTE = 1 };
enum { MS_NONE = 0, MS_ONEPERBAND = 1, MS_SHARED = 3 };
enum { CM_NONE = 0, CM_NTSC_RGB = 1, CM_GENERIC_RGB = 15 };
enum { LOC_IMPLICIT = 1 };
enum { DES_RAW = 0 };

/* 32-bit header fields from offset 520. */
enum {
    F_WIDTH, F_HEIGHT, F_SUBROW, F_STARTX, F_STARTY, F_PIXSIZX, F_PIXSIZY,
    F_LOCATION, F_LOCATION_DIM, F_IMAGES, F_BANDS, F_STORAGE, F_ENCODE,
    F_MAP_SCHEME, F_MAP_TYPE, F_MAP_BANDS, F_MAP_ENTRIES, F_MAP_SUBROW,
    F_MAP_ENABLE, F_MAPS_PER_CYCLE, F_COLOR_MODEL, F_FIELDS
};

struct layout {
    unsigned long f[F_FIELDS];
    const uint8_t *map, *raster;
    uint64_t size; /* header, map and raster */
};

static unsigned long get32(const uint8_t *p, int big)
{
    if (big)
        return ((unsigned long)p[0] << 24) | ((unsigned long)p[1] << 16) |
               ((unsigned long)p[2] << 8) | (unsigned long)p[3];
    return ((unsigned long)p[3] << 24) | ((unsigned long)p[2] << 16) |
           ((unsigned long)p[1] << 8) | (unsigned long)p[0];
}

/* Bytes per data sample, or 0 for an unknown storage type; bits count as 0 too. */
static unsigned sample_size(unsigned long storage)
{
    switch (storage) {
    case 1: return 1;
    case 2: return 2;
    case 4: case 5: return 4;      /* 4-byte integer, float */
    case 6: case 9: return 8;      /* complex, double */
    case 10: return 16;            /* double complex */
    default: return 0;
    }
}

static unsigned map_cell_size(unsigned long type)
{
    switch (type) {
    case 0: case 1: return 1;      /* a map with no type reads as bytes */
    case 2: return 2;
    case 4: case 5: return 4;
    case 6: case 7: return 8;
    default: return 0;
    }
}

/* Read the header at data and find the image's map and raster. Fails for
   anything whose size can't be known: other encodings, explicit locations,
   several images under one header, unknown types or oversized dimensions. */
static enum codec_result read_layout(const uint8_t *data, size_t length,
                                     struct layout *l)
{
    uint64_t pixels, raster, map = 0;
    unsigned i, sample;
    int big;

    if (length < 1u || data[0] != VIFF_MAGIC)
        return CODEC_INVALID;
    if (length < VIFF_HEADER)
        return CODEC_TRUNCATED;
    if (data[1] != VIFF_FILE_TYPE)
        return CODEC_INVALID;
    big = data[4] != DEP_DEC && data[4] != DEP_NS;
    for (i = 0; i < F_FIELDS; i++)
        l->f[i] = get32(data + VIFF_FIELDS + 4u * i, big);

    if (l->f[F_ENCODE] != DES_RAW || l->f[F_LOCATION] != LOC_IMPLICIT ||
        l->f[F_IMAGES] != 1u || l->f[F_BANDS] == 0u ||
        l->f[F_WIDTH] == 0u || l->f[F_HEIGHT] == 0u)
        return CODEC_INVALID;
    if (l->f[F_WIDTH] > VIFF_MAX_SIDE || l->f[F_HEIGHT] > VIFF_MAX_SIDE ||
        l->f[F_BANDS] > VIFF_MAX_SIDE)
        return CODEC_TOO_LARGE;
    pixels = (uint64_t)l->f[F_WIDTH] * l->f[F_HEIGHT];
    if (pixels > VIFF_MAX_PIXELS)
        return CODEC_TOO_LARGE;

    if (l->f[F_STORAGE] == TYP_BIT) {
        raster = (uint64_t)((l->f[F_WIDTH] + 7u) / 8u) * l->f[F_HEIGHT] *
                 l->f[F_BANDS];
    } else {
        sample = sample_size(l->f[F_STORAGE]);
        if (sample == 0)
            return CODEC_INVALID;
        raster = pixels * l->f[F_BANDS] * sample;
    }

    /* A map is stored only when there is a scheme and it has bands. */
    if (l->f[F_MAP_BANDS] == 0u)
        l->f[F_MAP_SCHEME] = MS_NONE;
    if (l->f[F_MAP_SCHEME] != MS_NONE) {
        unsigned cell = map_cell_size(l->f[F_MAP_TYPE]);
        if (cell == 0)
            return CODEC_INVALID;
        if (l->f[F_MAP_BANDS] > VIFF_MAX_SIDE || l->f[F_MAP_ENTRIES] > VIFF_MAX_SIDE)
            return CODEC_TOO_LARGE;
        map = (uint64_t)l->f[F_MAP_BANDS] * l->f[F_MAP_ENTRIES] * cell;
    }

    l->size = VIFF_HEADER + map + raster;
    if (l->size > length)
        return CODEC_TRUNCATED;
    l->map = data + VIFF_HEADER;
    l->raster = l->map + (size_t)map;
    return CODEC_OK;
}

/* Walk to image index; its layout is read with every error reported. */
static enum codec_result find_image(const uint8_t *data, size_t length,
                                    unsigned index, struct layout *l)
{
    size_t offset = 0;
    unsigned i;

    for (i = 0; i < index; i++) {
        enum codec_result r = read_layout(data + offset, length - offset, l);
        if (r != CODEC_OK)
            return i == 0 ? r : CODEC_INVALID;
        offset += (size_t)l->size;
        if (offset >= length || data[offset] != VIFF_MAGIC)
            return CODEC_INVALID;
    }
    return read_layout(data + offset, length - offset, l);
}

enum codec_result viff_count(const uint8_t *data, size_t length, unsigned *count)
{
    struct layout l;
    size_t offset = 0;
    unsigned n = 0;
    enum codec_result r;

    if (count != NULL)
        *count = 0;
    if (data == NULL || count == NULL)
        return CODEC_INVALID;
    r = read_layout(data, length, &l);
    if (r != CODEC_OK)
        return r;
    while (r == CODEC_OK) {
        n++;
        offset += (size_t)l.size;
        if (offset >= length || data[offset] != VIFF_MAGIC)
            break;
        r = read_layout(data + offset, length - offset, &l);
    }
    *count = n;
    return CODEC_OK;
}

static int supported(const struct layout *l)
{
    const unsigned long *f = l->f;

    if (f[F_COLOR_MODEL] != CM_NONE && f[F_COLOR_MODEL] != CM_NTSC_RGB &&
        f[F_COLOR_MODEL] != CM_GENERIC_RGB)
        return 0;
    if (f[F_MAP_SCHEME] != MS_NONE && f[F_MAP_SCHEME] != MS_ONEPERBAND &&
        f[F_MAP_SCHEME] != MS_SHARED)
        return 0;
    if (f[F_STORAGE] == TYP_BIT)
        return f[F_BANDS] == 1u;       /* any map is ignored */
    if (f[F_STORAGE] != TYP_1_BYTE)
        return 0;
    if (f[F_MAP_SCHEME] == MS_NONE)
        return f[F_BANDS] == 1u || f[F_BANDS] == 3u || f[F_BANDS] == 4u;
    return f[F_BANDS] == 1u && map_cell_size(f[F_MAP_TYPE]) == 1u &&
           f[F_MAP_ENTRIES] != 0u;
}

static void decode_bits(const struct layout *l, uint8_t *rgba)
{
    size_t width = l->f[F_WIDTH], height = l->f[F_HEIGHT];
    size_t stride = (width + 7u) / 8u, x, y;

    /* Rows start on a byte; the leftmost pixel is the lowest bit, and 1 is black. */
    for (y = 0; y < height; y++) {
        const uint8_t *row = l->raster + y * stride;
        for (x = 0; x < width; x++) {
            uint8_t v = (row[x >> 3] >> (x & 7u)) & 1u ? 0 : 255;
            rgba[0] = rgba[1] = rgba[2] = v;
            rgba[3] = 255;
            rgba += 4;
        }
    }
}

static void decode_bands(const struct layout *l, uint8_t *rgba)
{
    size_t plane = (size_t)l->f[F_WIDTH] * l->f[F_HEIGHT], i;
    unsigned long bands = l->f[F_BANDS];
    const uint8_t *p = l->raster;

    /* Bands are stored one after another. */
    for (i = 0; i < plane; i++, rgba += 4) {
        if (bands == 1u) {
            rgba[0] = rgba[1] = rgba[2] = p[i];
        } else {
            rgba[0] = p[i];
            rgba[1] = p[plane + i];
            rgba[2] = p[2u * plane + i];
        }
        rgba[3] = bands == 4u ? p[3u * plane + i] : 255;
    }
}

static void decode_mapped(const struct layout *l, uint8_t *rgba)
{
    size_t plane = (size_t)l->f[F_WIDTH] * l->f[F_HEIGHT], i;
    size_t entries = l->f[F_MAP_ENTRIES];
    unsigned long map_bands = l->f[F_MAP_BANDS];
    /* Each map band holds one component for every entry. A single band is
       gray; two give red and blue from the first and green from the second. */
    const uint8_t *red = l->map;
    const uint8_t *green = map_bands >= 2u ? l->map + entries : red;
    const uint8_t *blue = map_bands >= 3u ? l->map + 2u * entries : red;

    for (i = 0; i < plane; i++, rgba += 4) {
        size_t index = l->raster[i];
        if (index >= entries)
            index = 0;
        rgba[0] = red[index];
        rgba[1] = green[index];
        rgba[2] = blue[index];
        rgba[3] = 255;
    }
}

enum codec_result viff_decode(const uint8_t *data, size_t length, unsigned index,
                              struct viff_image *image)
{
    struct layout l;
    enum codec_result r;

    if (image == NULL)
        return CODEC_INVALID;
    image->width = image->height = 0;
    image->rgba = NULL;
    if (data == NULL)
        return CODEC_INVALID;
    r = find_image(data, length, index, &l);
    if (r != CODEC_OK)
        return r;
    if (!supported(&l))
        return CODEC_INVALID;

    image->rgba = malloc((size_t)l.f[F_WIDTH] * l.f[F_HEIGHT] * 4u);
    if (image->rgba == NULL)
        return CODEC_NO_MEMORY;
    if (l.f[F_STORAGE] == TYP_BIT)
        decode_bits(&l, image->rgba);
    else if (l.f[F_MAP_SCHEME] == MS_NONE)
        decode_bands(&l, image->rgba);
    else
        decode_mapped(&l, image->rgba);
    image->width = (unsigned)l.f[F_WIDTH];
    image->height = (unsigned)l.f[F_HEIGHT];
    return CODEC_OK;
}

void viff_free(struct viff_image *image)
{
    if (image == NULL)
        return;
    free(image->rgba);
    image->rgba = NULL;
    image->width = image->height = 0;
}
