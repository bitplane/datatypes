#include "decode.h"
#include <stdlib.h>
#include <string.h>

/* Formats worth trying for a file, chosen by its extension. */
enum family {
    ANY, ART, DOO, BIL, SSB, SRT, DA4, KID, RGB, SD0, SD1, SD2,
    PAINTWORKS, PG, GP, EZA, CE
};

static const struct {
    char ext[4];
    enum family family;
} extensions[] = {
    { "art", ART }, { "doo", DOO }, { "bil", BIL }, { "ssb", SSB },
    { "srt", SRT }, { "da4", DA4 }, { "kid", KID }, { "rgb", RGB },
    { "sd0", SD0 }, { "sd1", SD1 }, { "sd2", SD2 },
    { "sc0", PAINTWORKS }, { "sc1", PAINTWORKS }, { "sc2", PAINTWORKS },
    { "cl0", PAINTWORKS }, { "cl1", PAINTWORKS }, { "cl2", PAINTWORKS },
    { "pg0", PAINTWORKS }, { "pg1", PG }, { "pg2", PG }, { "pg3", GP },
    { "eza", EZA }, { "ce1", CE }, { "ce2", CE }, { "ce3", CE }
};

static const uint8_t mono[2][3] = { { 255, 255, 255 }, { 0, 0, 0 } };

static unsigned be16(const uint8_t *p)
{
    return ((unsigned)p[0] << 8) | p[1];
}

uint8_t stscreen_level(unsigned nibble, int ste)
{
    /* The STE keeps its extra, least significant bit in bit 3. */
    if (ste)
        return (uint8_t)((((nibble & 7u) << 1) | ((nibble >> 3) & 1u)) * 17u);
    /* round(v * 255 / 7), which is also 3-bit replication. */
    return (uint8_t)(((nibble & 7u) * 255u + 3u) / 7u);
}

void stscreen_free(struct stscreen_image *image)
{
    free(image->rgba);
    image->rgba = NULL;
    image->width = image->height = 0;
}

static enum family family_of(const char *name)
{
    const char *dot = NULL, *p;
    char ext[4];
    size_t i;

    if (name == NULL)
        return ANY;
    for (p = name; *p != '\0'; p++) {
        if (*p == '.')
            dot = p;
        else if (*p == '/' || *p == ':')
            dot = NULL;
    }
    if (dot == NULL || strlen(dot + 1) != 3)
        return ANY;
    for (i = 0; i < 3; i++) {
        char c = dot[1 + i];
        ext[i] = (char)(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    }
    ext[3] = '\0';
    for (i = 0; i < sizeof extensions / sizeof extensions[0]; i++)
        if (strcmp(extensions[i].ext, ext) == 0)
            return extensions[i].family;
    return ANY;
}

static enum codec_result allocate(struct stscreen_image *image,
                                  unsigned width, unsigned height)
{
    image->rgba = malloc((size_t)width * height * 4u);
    if (image->rgba == NULL)
        return CODEC_NO_MEMORY;
    image->width = width;
    image->height = height;
    return CODEC_OK;
}

static void put(struct stscreen_image *image, unsigned x, unsigned y,
                const uint8_t *rgb)
{
    uint8_t *p = image->rgba + ((size_t)y * image->width + x) * 4u;
    p[0] = rgb[0];
    p[1] = rgb[1];
    p[2] = rgb[2];
    p[3] = 255;
}

/* Colour index of pixel x in a line where each 16 pixels are one word per plane. */
static unsigned index_at(const uint8_t *line, unsigned x, unsigned planes)
{
    const uint8_t *group = line + (x / 16u) * planes * 2u;
    unsigned bit = 15u - x % 16u, index = 0, p;

    for (p = 0; p < planes; p++)
        index |= ((be16(group + p * 2u) >> bit) & 1u) << p;
    return index;
}

static void planar(struct stscreen_image *image, const uint8_t *bits,
                   size_t stride, unsigned planes, const uint8_t (*palette)[3])
{
    unsigned x, y;

    for (y = 0; y < image->height; y++)
        for (x = 0; x < image->width; x++)
            put(image, x, y, palette[index_at(bits + y * stride, x, planes)]);
}

/* A palette of count words. It is STE (4 bits per gun) if any colour has a
   fourth bit set, and ST otherwise. */
static void st_palette(const uint8_t *words, unsigned count, uint8_t palette[16][3])
{
    unsigned i;
    int ste = 0;

    for (i = 0; i < count; i++)
        if (be16(words + i * 2u) & 0x888u)
            ste = 1;
    for (i = 0; i < count; i++) {
        unsigned c = be16(words + i * 2u);
        palette[i][0] = stscreen_level(c >> 8, ste);
        palette[i][1] = stscreen_level(c >> 4, ste);
        palette[i][2] = stscreen_level(c, ste);
    }
}

/* A whole screen in resolution mode 0 (320x200x16), 1 (640x200x4) or
   2 (640x400x2), twice as tall if doubled. High resolution ignores words. */
static enum codec_result screen(struct stscreen_image *image, const uint8_t *bits,
                                const uint8_t *words, unsigned mode,
                                unsigned doubled)
{
    uint8_t palette[16][3];
    unsigned planes = 4u >> mode, width = mode == 0 ? 320u : 640u;
    enum codec_result result;

    if (mode != 2)
        st_palette(words, 1u << planes, palette);
    result = allocate(image, width, (mode == 2 ? 400u : 200u) << doubled);
    if (result != CODEC_OK)
        return result;
    planar(image, bits, width * planes / 8u, planes, mode == 2 ? mono : palette);
    return CODEC_OK;
}

/* Byte-oriented run-length streams. */
struct rle {
    const uint8_t *data;
    size_t at, length;
    unsigned count;
    int value; /* the repeated byte, or -1 for literals */
    int packbits;
};

static int rle_byte(struct rle *r)
{
    while (r->count == 0) {
        unsigned b;
        if (r->at >= r->length)
            return -1;
        b = r->data[r->at++];
        if (r->packbits) {
            /* PackBits, where 128 repeats 129 times as EZ-Art reads it. */
            r->count = b < 128 ? b + 1u : 257u - b;
            r->value = -1;
            if (b >= 128) {
                if (r->at >= r->length)
                    return -1;
                r->value = r->data[r->at++];
            }
        } else if (b < 128) {
            /* Paintworks: a run of b copies of the next byte... */
            r->count = b;
            if (r->at >= r->length)
                return -1;
            r->value = r->data[r->at++];
        } else {
            /* ...or b - 128 literals. */
            r->count = b - 128u;
            r->value = -1;
        }
    }
    r->count--;
    if (r->value >= 0)
        return r->value;
    if (r->at >= r->length)
        return -1;
    return r->data[r->at++];
}

/* Paintworks screens (SC), pages (PG, twice as tall) and clips (CL): a
   NEOchrome header marked "ANvisionA", then raw or run-length bitplanes. */
static enum codec_result paintworks(const uint8_t *data, size_t length,
                                    struct stscreen_image *image)
{
    struct rle r = { 0 };
    unsigned flags, mode, doubled, unit, plane;
    size_t size, at;
    uint8_t *bits;
    enum codec_result result;

    if (length < 128)
        return CODEC_TRUNCATED;
    if (memcmp(data + 0x36, "ANvisionA", 9) != 0)
        return CODEC_INVALID;
    flags = data[0x3f];
    switch (flags & 15u) {
    case 0: doubled = 1; break;
    case 1: case 2: doubled = 0; break;
    default: return CODEC_INVALID;
    }
    mode = flags >> 4 & 3u;
    if (mode == 3)
        return CODEC_INVALID;
    size = (size_t)32000u << doubled;
    if (!(flags & 0x80u)) {
        if (length - 128u < size)
            return CODEC_TRUNCATED;
        return screen(image, data + 128, data + 4, mode, doubled);
    }

    /* Each plane's words in turn, through the whole picture. */
    bits = malloc(size);
    if (bits == NULL)
        return CODEC_NO_MEMORY;
    r.data = data + 128;
    r.length = length - 128u;
    unit = 8u >> mode;
    for (plane = 0; plane < unit; plane += 2)
        for (at = plane; at < size; at += unit) {
            int hi = rle_byte(&r), lo = rle_byte(&r);
            if (hi < 0 || lo < 0) {
                free(bits);
                return CODEC_TRUNCATED;
            }
            bits[at] = (uint8_t)hi;
            bits[at + 1] = (uint8_t)lo;
        }
    result = screen(image, bits, data + 4, mode, doubled);
    free(bits);
    return result;
}

/* Graphics Processor: a mode byte, the palette, then raw bitplanes at 331 or
   runs from 333 of one unit (16 pixels' worth of a plane) repeated. */
static enum codec_result graphics_processor(const uint8_t *data, size_t length,
                                            struct stscreen_image *image)
{
    unsigned mode, unit;
    size_t at = 0, pos = 333;
    uint8_t *bits;
    enum codec_result result;

    if (length < 2)
        return CODEC_TRUNCATED;
    mode = data[1];
    if (data[0] != 0 || (mode > 2 && (mode < 10 || mode > 12)))
        return CODEC_INVALID;
    if (mode <= 2) {
        if (length < 32331u)
            return CODEC_TRUNCATED;
        return screen(image, data + 331, data + 2, mode, 0);
    }
    mode -= 10;
    unit = 4u >> mode;
    bits = malloc(32000);
    if (bits == NULL)
        return CODEC_NO_MEMORY;
    while (at < 32000u) {
        unsigned count;
        if (pos + 1u + unit > length) {
            free(bits);
            return CODEC_TRUNCATED;
        }
        count = data[pos];
        if (count == 0) {
            free(bits);
            return CODEC_INVALID;
        }
        /* A run past the last pixel is clamped. */
        for (; count > 0 && at < 32000u; count--, at += unit)
            memcpy(bits + at, data + pos + 1, unit);
        pos += 1u + unit;
    }
    result = screen(image, bits, data + 2, mode, 0);
    free(bits);
    return result;
}

/* EZ-Art Professional: "EZ", the palette, then PackBits lines, each plane's
   40 bytes in turn. */
static enum codec_result ez_art(const uint8_t *data, size_t length,
                                struct stscreen_image *image)
{
    struct rle r = { 0 };
    unsigned y, plane, w, i;
    uint8_t *bits;
    enum codec_result result;

    if (length < 4)
        return CODEC_TRUNCATED;
    if (memcmp(data, "EZ\0\310", 4) != 0)
        return CODEC_INVALID;
    if (length < 44)
        return CODEC_TRUNCATED;
    bits = malloc(32000);
    if (bits == NULL)
        return CODEC_NO_MEMORY;
    r.data = data + 44;
    r.length = length - 44u;
    r.packbits = 1;
    for (y = 0; y < 200; y++)
        for (plane = 0; plane < 4; plane++)
            for (w = plane * 2u; w < 160; w += 8)
                for (i = 0; i < 2; i++) {
                    int b = rle_byte(&r);
                    if (b < 0) {
                        free(bits);
                        return CODEC_TRUNCATED;
                    }
                    bits[y * 160u + w + i] = (uint8_t)b;
                }
    result = screen(image, bits, data + 4, 0, 0);
    free(bits);
    return result;
}

static uint8_t level6(unsigned v)
{
    v &= 63u;
    return (uint8_t)(v << 2 | v >> 4);
}

static uint8_t level5(unsigned v)
{
    v &= 31u;
    return (uint8_t)(v << 3 | v >> 2);
}

/* ComputerEyes: "EYES", a mode, then samples in columns from offset 22. */
static enum codec_result computer_eyes(const uint8_t *data, size_t length,
                                       struct stscreen_image *image)
{
    unsigned x, y;
    enum codec_result result;

    if (length < 6)
        return CODEC_TRUNCATED;
    if (memcmp(data, "EYES\0", 5) != 0 || data[5] > 2)
        return CODEC_INVALID;
    if (length < (data[5] == 0 ? 192022u : 256022u))
        return CODEC_TRUNCATED;
    switch (data[5]) {
    case 0:
        /* Three planes of 6-bit red, green and blue. */
        result = allocate(image, 320, 200);
        if (result != CODEC_OK)
            return result;
        for (x = 0; x < 320; x++)
            for (y = 0; y < 200; y++) {
                const uint8_t *p = data + 22 + x * 200u + y;
                uint8_t rgb[3];
                rgb[0] = level6(p[0]);
                rgb[1] = level6(p[64000]);
                rgb[2] = level6(p[128000]);
                put(image, x, y, rgb);
            }
        return CODEC_OK;
    case 1:
        /* Words of 5:5:5 RGB; the top bit is unused. */
        result = allocate(image, 640, 200);
        if (result != CODEC_OK)
            return result;
        for (x = 0; x < 640; x++)
            for (y = 0; y < 200; y++) {
                unsigned c = be16(data + 22 + (x * 200u + y) * 2u);
                uint8_t rgb[3];
                rgb[0] = level5(c >> 10);
                rgb[1] = level5(c >> 5);
                rgb[2] = level5(c);
                put(image, x, y, rgb);
            }
        return CODEC_OK;
    default:
        /* Grey, the sum of three 6-bit guns, with each column's even
           lines before its odd ones. */
        result = allocate(image, 640, 400);
        if (result != CODEC_OK)
            return result;
        for (x = 0; x < 640; x++)
            for (y = 0; y < 400; y++) {
                unsigned v = data[22 + x * 400u + (y & 1u) * 200u + y / 2u] * 4u / 3u;
                uint8_t rgb[3];
                rgb[0] = rgb[1] = rgb[2] = (uint8_t)(v > 255u ? 255u : v);
                put(image, x, y, rgb);
            }
        return CODEC_OK;
    }
}

/* Fullscreen Construction Kit: "KD", the palette, then 274 overscan lines
   of 230 bytes, of which 448 pixels show. */
static enum codec_result construction_kit(const uint8_t *data, size_t length,
                                          struct stscreen_image *image)
{
    uint8_t palette[16][3];
    enum codec_result result;

    if (length < 2)
        return CODEC_TRUNCATED;
    if (data[0] != 'K' || data[1] != 'D')
        return CODEC_INVALID;
    if (length < 63054u)
        return CODEC_TRUNCATED;
    st_palette(data + 2, 16, palette);
    result = allocate(image, 448, 274);
    if (result != CODEC_OK)
        return result;
    planar(image, data + 34, 230, 4, palette);
    return CODEC_OK;
}

/* RGB Intermediate: three low resolution DEGAS pictures whose colour
   indexes are the red, green and blue levels. */
static enum codec_result rgb_intermediate(const uint8_t *data, size_t length,
                                          struct stscreen_image *image)
{
    unsigned x, y, c;
    enum codec_result result;

    if (length < 96102u)
        return CODEC_TRUNCATED;
    result = allocate(image, 320, 200);
    if (result != CODEC_OK)
        return result;
    for (y = 0; y < 200; y++)
        for (x = 0; x < 320; x++) {
            uint8_t rgb[3];
            for (c = 0; c < 3; c++)
                rgb[c] = (uint8_t)(index_at(data + 34 + c * 32034u + y * 160u, x, 4) * 17u);
            put(image, x, y, rgb);
        }
    return CODEC_OK;
}

static int is_paintworks(const uint8_t *data, size_t length)
{
    return length >= 0x3f && memcmp(data + 0x36, "ANvisionA", 9) == 0;
}

/* Formats with a signature, for files whose extension says nothing. */
static enum codec_result sniff(const uint8_t *data, size_t length,
                               struct stscreen_image *image)
{
    if (length >= 4 && memcmp(data, "EYES", 4) == 0)
        return computer_eyes(data, length, image);
    if (length >= 4 && memcmp(data, "EZ\0\310", 4) == 0)
        return ez_art(data, length, image);
    if (is_paintworks(data, length))
        return paintworks(data, length, image);
    if (length == 63054u && data[0] == 'K' && data[1] == 'D')
        return construction_kit(data, length, image);
    return CODEC_INVALID;
}

enum codec_result stscreen_decode(const uint8_t *data, size_t length,
                                  const char *name,
                                  struct stscreen_image *image)
{
    enum family family = family_of(name);

    if (image == NULL)
        return CODEC_INVALID;
    image->width = image->height = 0;
    image->rgba = NULL;
    if (data == NULL)
        return CODEC_TRUNCATED;

    switch (family) {
    case ART:
        /* Art Director keeps eight palettes and the chosen one's index. */
        if (length == 32512u)
            return screen(image, data, data + 32000 + (data[0x7e1f] < 8 ? data[0x7e1f] : 0) * 32u, 0, 0);
        /* GFA Artist: the palette, then the bitmap. */
        if (length == 32032u)
            return screen(image, data + 32, data, 0, 0);
        /* MonoSTar and The ArtiST: a high resolution bitmap. */
        if (length == 32000u)
            return screen(image, data, NULL, 2, 0);
        return length < 32000u ? CODEC_TRUNCATED : CODEC_INVALID;
    case DOO:
        return length < 32000u ? CODEC_TRUNCATED : screen(image, data, NULL, 2, 0);
    case BIL:
        /* ColorSTar saves GFA Artist or low resolution DEGAS files. */
        if (length == 32032u)
            return screen(image, data + 32, data, 0, 0);
        if (length == 32034u && be16(data) == 0)
            return screen(image, data + 34, data + 2, 0, 0);
        return length < 32032u ? CODEC_TRUNCATED : CODEC_INVALID;
    case SSB:
        return length < 32768u ? CODEC_TRUNCATED : screen(image, data, data + 32000, 0, 0);
    case SRT:
        /* Synthetic Arts: medium resolution, then "JHSy", 1 and the palette. */
        if (length < 32038u)
            return CODEC_TRUNCATED;
        if (memcmp(data + 32000, "JHSy\0\1", 6) != 0)
            return CODEC_INVALID;
        return screen(image, data, data + 32006, 1, 0);
    case DA4:
        return length < 64000u ? CODEC_TRUNCATED : screen(image, data, NULL, 2, 1);
    case KID:
        return construction_kit(data, length, image);
    case RGB:
        return rgb_intermediate(data, length, image);
    case SD0: case SD1: case SD2:
        /* Dali: NEOchrome's layout, with the resolution only in the name. */
        if (length < 32128u)
            return CODEC_TRUNCATED;
        return screen(image, data + 128, data + 4, (unsigned)(family - SD0), 0);
    case PAINTWORKS:
        return paintworks(data, length, image);
    case PG:
        if (is_paintworks(data, length))
            return paintworks(data, length, image);
        return graphics_processor(data, length, image);
    case GP:
        return graphics_processor(data, length, image);
    case EZA:
        return ez_art(data, length, image);
    case CE:
        return computer_eyes(data, length, image);
    default:
        return sniff(data, length, image);
    }
}
