#include <stdlib.h>
#include <string.h>

#include "decode.h"

/* A BSAVE header: 0xFE, then the start, end and run addresses. */
#define HEADER 7u
/* The VRAM a Graph Saurus screen 7, 8 or 12 picture covers. */
#define SR_VRAM 0xd400u

enum family { NONE, SC2, SC3, SC4, SC5, SC6, SC7, SC8, SCA, SCC, SR5, SR6, SR7, SRI };

static const struct {
    char ext[4];
    enum family family;
} extensions[] = {
    { "sc2", SC2 }, { "grp", SC2 }, { "sc3", SC3 }, { "sc4", SC4 },
    { "sc5", SC5 }, { "ge5", SC5 }, { "sc6", SC6 },
    { "sc7", SC7 }, { "ge7", SC7 },
    { "sc8", SC8 }, { "ge8", SC8 }, { "sr8", SC8 },
    { "sca", SCA }, { "scb", SCA },
    { "scc", SCC }, { "srs", SCC }, { "yjk", SCC },
    { "sr5", SR5 }, { "sr6", SR6 }, { "sr7", SR7 }, { "sri", SRI }
};

/* The TMS9918 colours, for MSX1 screens that carry no palette. Colour 0 is
   transparent, which shows the black backdrop. */
static const uint8_t msx1[16][3] = {
    { 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x00 }, { 0x3a, 0xbb, 0x43 },
    { 0x70, 0xd3, 0x77 }, { 0x54, 0x59, 0xd7 }, { 0x7b, 0x7b, 0xe8 },
    { 0xb3, 0x63, 0x4b }, { 0x61, 0xdf, 0xe7 }, { 0xd4, 0x6a, 0x53 },
    { 0xf8, 0x8e, 0x77 }, { 0xc7, 0xc7, 0x59 }, { 0xd9, 0xd4, 0x81 },
    { 0x36, 0xa5, 0x3b }, { 0xb0, 0x6b, 0xae }, { 0xc7, 0xd0, 0xc5 },
    { 0xfa, 0xff, 0xf8 }
};

/* The V9938 palette after reset, as the VDP stores it: 0RRR0BBB, 00000GGG. */
static const uint8_t msx2[32] = {
    0x00, 0, 0x00, 0, 0x11, 6, 0x33, 7, 0x17, 1, 0x27, 3, 0x51, 1, 0x27, 6,
    0x71, 1, 0x73, 3, 0x61, 6, 0x64, 6, 0x11, 4, 0x65, 2, 0x55, 5, 0x77, 7
};

/* Screen 6's four colours after reset. */
static const uint8_t msx6[4][3] = {
    { 0x00, 0x00, 0x00 }, { 0x24, 0x92, 0x24 }, { 0x24, 0xdb, 0x24 },
    { 0x6d, 0xff, 0x6d }
};

/* Screen 8's sprites use their own fixed palette. */
static const uint8_t sc8_sprites[32] = {
    0x00, 0, 0x02, 0, 0x30, 0, 0x32, 0, 0x00, 3, 0x02, 3, 0x30, 3, 0x32, 3,
    0x72, 4, 0x07, 0, 0x70, 0, 0x77, 0, 0x00, 7, 0x07, 7, 0x70, 7, 0x77, 7
};

void msx_free(struct msx_image *image)
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
        return NONE;
    for (p = name; *p != '\0'; p++) {
        if (*p == '.')
            dot = p;
        else if (*p == '/' || *p == ':')
            dot = NULL;
    }
    if (dot == NULL || strlen(dot + 1) != 3)
        return NONE;
    for (i = 0; i < 3; i++) {
        char c = dot[1 + i];
        ext[i] = (char)(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    }
    ext[3] = '\0';
    for (i = 0; i < sizeof extensions / sizeof extensions[0]; i++)
        if (strcmp(extensions[i].ext, ext) == 0)
            return extensions[i].family;
    return NONE;
}

const char *msx_palette_ext(const char *name)
{
    switch (family_of(name)) {
    case SR5: return "pl5";
    case SR6: return "pl6";
    case SR7: case SRI: return "pl7";
    default: return NULL;
    }
}

/* 3 bits to 8, as the V9938's DAC levels. */
static uint8_t level3(unsigned v)
{
    v &= 7;
    return (uint8_t)(v << 5 | v << 2 | v >> 1);
}

static uint8_t level5(int v)
{
    v = v < 0 ? 0 : v > 31 ? 31 : v;
    return (uint8_t)(v << 3 | v >> 2);
}

/* count VDP palette entries to RGB. */
static void palette9(const uint8_t *p, unsigned count, uint8_t (*rgb)[3])
{
    unsigned i;

    for (i = 0; i < count; i++) {
        rgb[i][0] = level3(p[2 * i] >> 4);
        rgb[i][1] = level3(p[2 * i + 1]);
        rgb[i][2] = level3(p[2 * i]);
    }
}

/* MSX1 screens saved on an MSX2 may hold a palette. Unused bits must be
   clear and it can't be all black. */
static int is_palette(const uint8_t *p)
{
    unsigned i, any = 0;

    for (i = 0; i < 16; i++) {
        if ((p[2 * i] & 0x88) != 0 || (p[2 * i + 1] & 0xf8) != 0)
            return 0;
        any |= p[2 * i] | p[2 * i + 1];
    }
    return any != 0;
}

static unsigned le16(const uint8_t *p)
{
    return p[0] | (unsigned)p[1] << 8;
}

/* The BSAVE end address, after checking the header starts at VRAM 0. */
static enum codec_result header(const uint8_t *data, size_t length,
                                unsigned *end)
{
    if (length < HEADER)
        return CODEC_TRUNCATED;
    if (data[0] != 0xfe || le16(data + 1) != 0 || le16(data + 5) != 0)
        return CODEC_INVALID;
    *end = le16(data + 3);
    return CODEC_OK;
}

/* A dump of at least need bytes of VRAM. */
static enum codec_result dump(const uint8_t *data, size_t length, unsigned need)
{
    unsigned end;
    enum codec_result result = header(data, length, &end);

    if (result != CODEC_OK)
        return result;
    if (end + 1u < need || length < HEADER + need)
        return CODEC_TRUNCATED;
    return CODEC_OK;
}

/* Screens 5 and 6 may be dumped a few lines short: the lines of 128 bytes
   the dump covers, at most 212. */
static enum codec_result lines128(const uint8_t *data, size_t length,
                                  unsigned *height)
{
    unsigned end, lines;
    enum codec_result result = header(data, length, &end);

    if (result != CODEC_OK)
        return result;
    lines = (end + 1u) >> 7;
    if (lines == 0 || length < HEADER + (size_t)lines * 128u)
        return CODEC_TRUNCATED;
    *height = lines < 212 ? lines : 212;
    return CODEC_OK;
}

/* Graph Saurus packs screen 7, 8 and 12 pictures with header byte 0xFD:
   0 n v is n copies of v (n = 0 means 256), 1 to 15 then v is that many
   copies, and anything else is itself. Unpacked or not, vram gets the
   picture's first SR_VRAM bytes. */
static enum codec_result unpack_sr(const uint8_t *data, size_t length,
                                   uint8_t *buffer, const uint8_t **vram)
{
    size_t at = HEADER, out = 0;

    if (length < HEADER)
        return CODEC_TRUNCATED;
    if (data[0] == 0xfe) {
        enum codec_result result = dump(data, length, SR_VRAM);
        *vram = data + HEADER;
        return result;
    }
    if (data[0] != 0xfd || le16(data + 1) != 0 || le16(data + 5) != 0)
        return CODEC_INVALID;
    if (HEADER + le16(data + 3) != length)
        return length < HEADER + le16(data + 3) ? CODEC_TRUNCATED : CODEC_INVALID;
    while (out < SR_VRAM) {
        unsigned count = 1, value;

        if (at >= length)
            return CODEC_TRUNCATED;
        value = data[at++];
        if (value < 16) {
            count = value;
            if (value == 0) {
                if (at >= length)
                    return CODEC_TRUNCATED;
                count = data[at++];
                if (count == 0)
                    count = 256;
            }
            if (at >= length)
                return CODEC_TRUNCATED;
            value = data[at++];
        }
        /* A run past the end of the screen is clamped. */
        if (count > SR_VRAM - out)
            count = (unsigned)(SR_VRAM - out);
        memset(buffer + out, (int)value, count);
        out += count;
    }
    *vram = buffer;
    return CODEC_OK;
}

static enum codec_result allocate(struct msx_image *image, unsigned width,
                                  unsigned height)
{
    image->rgba = malloc((size_t)width * height * 4u);
    if (image->rgba == NULL)
        return CODEC_NO_MEMORY;
    image->width = width;
    image->height = height;
    return CODEC_OK;
}

static void put(struct msx_image *image, unsigned x, unsigned y,
                const uint8_t *rgb)
{
    uint8_t *p = image->rgba + ((size_t)y * image->width + x) * 4u;
    p[0] = rgb[0];
    p[1] = rgb[1];
    p[2] = rgb[2];
    p[3] = 255;
}

/* Screens 2 and 4: 256x192 in 8x8 cells, each row of a cell with its own
   foreground and background. Three banks of 256 patterns, one for each
   third of the screen. */
static void tiles(struct msx_image *image, const uint8_t *vram,
                  const uint8_t (*palette)[3])
{
    unsigned x, y;

    for (y = 0; y < 192; y++)
        for (x = 0; x < 256; x++) {
            size_t cell = ((size_t)(y & 0xc0) << 5) + (y & 7) +
                          ((size_t)vram[0x1800 + ((y & ~7u) << 2) + (x >> 3)] << 3);
            unsigned c = vram[0x2000 + cell];
            put(image, x, y, palette[(vram[cell] >> (~x & 7) & 1) ? c >> 4 : c & 15]);
        }
}

/* Screen 3: 64x48 blocks of 4x4 pixels, two to a pattern byte. Without a
   name table in the dump, the names are the ones BASIC sets up. */
static void blocks(struct msx_image *image, const uint8_t *vram, int names,
                   const uint8_t (*palette)[3])
{
    unsigned x, y;

    for (y = 0; y < 192; y++)
        for (x = 0; x < 256; x++) {
            unsigned name = names ? vram[0x800 + ((y & ~7u) << 2) + (x >> 3)]
                                  : (y & 0xe0) + (x >> 3);
            unsigned c = vram[(name << 3) + (y >> 2 & 7)] >> (x & 4 ? 0 : 4) & 15;
            put(image, x, y, palette[c]);
        }
}

/* Draws the hardware sprites over a full VRAM dump, as the VDP would with
   16x16 sprites: mode 1 below screen 4, mode 2 from it on. The 512-pixel
   modes draw each sprite pixel twice as wide. */
static void sprites(struct msx_image *image, const uint8_t *vram,
                    unsigned mode, unsigned attributes, unsigned patterns,
                    const uint8_t (*palette)[3])
{
    unsigned rows = mode <= 4 ? 192 : 212, x, y;
    unsigned stop = mode >= 4 ? 216 : 208;

    if (rows > image->height)
        rows = image->height;
    for (y = 0; y < rows; y++)
        for (x = 0; x < 256; x++) {
            unsigned color = 0, left = mode >= 4 ? 8 : 4, sprite;
            int ored = 0;

            for (sprite = 0; sprite < 32; sprite++) {
                const uint8_t *a = vram + attributes + sprite * 4u;
                unsigned row, c;
                int column;

                if (a[0] == stop)
                    break;
                row = (y - a[0] - 1u) & 255;
                if (row >= 16)
                    continue;
                if (left-- == 0)
                    break;
                c = mode >= 4 ? vram[attributes - 512 + sprite * 16u + row] : a[3];
                /* In mode 2, CC set ORs a sprite into the one above it. */
                if (mode < 4 || (c & 0x40) == 0) {
                    if (color != 0)
                        break;
                    ored = 1;
                } else if (!ored)
                    continue;
                column = (int)x - a[1] + (c & 0x80 ? 32 : 0);
                if (column < 0 || column >= 16)
                    continue;
                if ((vram[patterns + ((a[2] & 0xfcu) << 3) + row + ((column & 8) << 1)] >>
                     (~column & 7) & 1) == 0)
                    continue;
                color |= c | (mode >= 4 ? 16 : 0);
            }
            if (color == 0)
                continue;
            if (mode == 6) {
                put(image, 2 * x, y, palette[color >> 2 & 3]);
                put(image, 2 * x + 1, y, palette[color & 3]);
            } else if (mode == 7) {
                put(image, 2 * x, y, palette[color & 15]);
                put(image, 2 * x + 1, y, palette[color & 15]);
            } else
                put(image, x, y, palette[color & 15]);
        }
}

/* 4 bits a pixel, high nibble first. */
static void nibbles(struct msx_image *image, const uint8_t *bits, size_t stride,
                    const uint8_t (*palette)[3])
{
    unsigned x, y;

    for (y = 0; y < image->height; y++)
        for (x = 0; x < image->width; x++) {
            unsigned b = bits[y * stride + (x >> 1)];
            put(image, x, y, palette[x & 1 ? b & 15 : b >> 4]);
        }
}

/* Screen 6: 2 bits a pixel, 512 wide. */
static void crumbs(struct msx_image *image, const uint8_t *bits,
                   const uint8_t (*palette)[3])
{
    unsigned x, y;

    for (y = 0; y < image->height; y++)
        for (x = 0; x < 512; x++)
            put(image, x, y, palette[bits[y * 128u + (x >> 2)] >> ((~x & 3) << 1) & 3]);
}

/* Screen 8: GGGRRRBB, blue in four levels. */
static void direct(struct msx_image *image, const uint8_t *bits)
{
    static const uint8_t blues[4] = { 0, 2, 4, 7 };
    unsigned x, y;

    for (y = 0; y < image->height; y++)
        for (x = 0; x < 256; x++) {
            unsigned b = bits[y * 256u + x];
            uint8_t rgb[3];
            rgb[0] = level3(b >> 2);
            rgb[1] = level3(b >> 5);
            rgb[2] = level3(blues[b & 3]);
            put(image, x, y, rgb);
        }
}

/* The low three bits of a group's first two bytes, or last two. */
static int chroma(const uint8_t *p)
{
    int v = (p[0] & 7) | (p[1] & 7) << 3;
    return v >= 32 ? v - 64 : v;
}

/* Screens 10 and 12: every four pixels share J and K, and each has its own
   5-bit Y. In screen 10 an odd Y is instead a palette colour, Y / 2. */
static void yjk(struct msx_image *image, const uint8_t *bits, int attributes,
                const uint8_t (*palette)[3])
{
    unsigned x, y;

    for (y = 0; y < image->height; y++) {
        const uint8_t *line = bits + y * 256u;
        for (x = 0; x < 256; x++) {
            const uint8_t *group = line + (x & ~3u);
            int luma = line[x] >> 3, k = chroma(group), j = chroma(group + 2);
            int blue = 5 * luma - 2 * j - k + 2;
            uint8_t rgb[3];

            if (attributes && (luma & 1)) {
                put(image, x, y, palette[luma >> 1]);
                continue;
            }
            rgb[0] = level5(luma + j);
            rgb[1] = level5(luma + k);
            rgb[2] = level5(blue / 4); /* negative clamps to 0 either way */
            put(image, x, y, rgb);
        }
    }
}

static enum codec_result screen2(const uint8_t *data, size_t length,
                                 unsigned mode, struct msx_image *image)
{
    const uint8_t *vram = data + HEADER;
    uint8_t palette[16][3];
    enum codec_result result = dump(data, length, 0x3800);

    if (result != CODEC_OK)
        return result;
    if (is_palette(vram + 0x1b80))
        palette9(vram + 0x1b80, 16, palette);
    else if (mode == 4)
        palette9(msx2, 16, palette);
    else
        memcpy(palette, msx1, sizeof palette);
    result = allocate(image, 256, 192);
    if (result != CODEC_OK)
        return result;
    tiles(image, vram, (const uint8_t (*)[3])palette);
    /* Only a dump of all 16K has the sprite patterns. */
    if (mode == 2 ? length == HEADER + 0x4000u : length >= HEADER + 0x4000u)
        sprites(image, vram, mode, mode == 2 ? 0x1b00 : 0x1e00, 0x3800,
                (const uint8_t (*)[3])palette);
    return CODEC_OK;
}

static enum codec_result screen3(const uint8_t *data, size_t length,
                                 struct msx_image *image)
{
    const uint8_t *vram = data + HEADER;
    uint8_t palette[16][3];
    enum codec_result result = dump(data, length, 0x600);

    if (result != CODEC_OK)
        return result;
    if (length >= HEADER + 0x2040u && is_palette(vram + 0x2020))
        palette9(vram + 0x2020, 16, palette);
    else
        memcpy(palette, msx1, sizeof palette);
    result = allocate(image, 256, 192);
    if (result != CODEC_OK)
        return result;
    blocks(image, vram, length >= HEADER + 0xb00u, (const uint8_t (*)[3])palette);
    if (length == HEADER + 0x4000u)
        sprites(image, vram, 3, 0x1b00, 0x3800, (const uint8_t (*)[3])palette);
    return CODEC_OK;
}

/* Screens 5 and 6, with the palette at 0x7680 when the dump reaches it and
   the sprites when it is exactly 32K. */
static enum codec_result screen5(const uint8_t *data, size_t length,
                                 unsigned mode, struct msx_image *image)
{
    const uint8_t *vram = data + HEADER;
    uint8_t palette[16][3];
    unsigned height;
    enum codec_result result = lines128(data, length, &height);

    if (result != CODEC_OK)
        return result;
    if (mode == 5)
        palette9(length >= HEADER + 0x76a0u ? vram + 0x7680 : msx2, 16, palette);
    else if (length >= HEADER + 0x7688u)
        palette9(vram + 0x7680, 4, palette);
    else
        memcpy(palette, msx6, sizeof msx6);
    result = allocate(image, mode == 5 ? 256 : 512, height);
    if (result != CODEC_OK)
        return result;
    if (mode == 5)
        nibbles(image, vram, 128, (const uint8_t (*)[3])palette);
    else
        crumbs(image, vram, (const uint8_t (*)[3])palette);
    if (length == HEADER + 0x8000u)
        sprites(image, vram, mode, 0x7600, 0x7800, (const uint8_t (*)[3])palette);
    return CODEC_OK;
}

static enum codec_result screen7(const uint8_t *data, size_t length,
                                 struct msx_image *image)
{
    const uint8_t *vram = data + HEADER;
    uint8_t palette[16][3];
    enum codec_result result = dump(data, length, SR_VRAM);

    if (result != CODEC_OK)
        return result;
    palette9(length >= HEADER + 0xfaa0u ? vram + 0xfa80 : msx2, 16, palette);
    result = allocate(image, 512, 212);
    if (result != CODEC_OK)
        return result;
    nibbles(image, vram, 256, (const uint8_t (*)[3])palette);
    if (length == HEADER + 0xfaa0u)
        sprites(image, vram, 7, 0xfa00, 0xf000, (const uint8_t (*)[3])palette);
    return CODEC_OK;
}

static enum codec_result screen8(const uint8_t *data, size_t length,
                                 struct msx_image *image)
{
    uint8_t *buffer = malloc(SR_VRAM);
    const uint8_t *vram = NULL;
    enum codec_result result;

    if (buffer == NULL)
        return CODEC_NO_MEMORY;
    result = unpack_sr(data, length, buffer, &vram);
    if (result == CODEC_OK)
        result = allocate(image, 256, 212);
    if (result == CODEC_OK) {
        direct(image, vram);
        if (data[0] == 0xfe && length == HEADER + 0xfaa0u) {
            uint8_t palette[16][3];
            palette9(sc8_sprites, 16, palette);
            sprites(image, data + HEADER, 8, 0xfa00, 0xf000,
                    (const uint8_t (*)[3])palette);
        }
    }
    free(buffer);
    return result;
}

/* Screens 10 (attributes set, with a palette) and 12. */
static enum codec_result screen12(const uint8_t *data, size_t length,
                                  int attributes, struct msx_image *image)
{
    uint8_t *buffer = NULL, palette[16][3] = { { 0 } };
    const uint8_t *vram = data + HEADER;
    unsigned height = 212;
    enum codec_result result;

    if (attributes) {
        result = dump(data, length, SR_VRAM);
        if (result == CODEC_OK && length < HEADER + 0xfaa0u)
            result = CODEC_TRUNCATED;
    } else if (length >= HEADER + 0xc000u && data[0] == 0xfe &&
               le16(data + 1) == 0 && le16(data + 5) == 0 &&
               le16(data + 3) == 0xbfff) {
        /* 192 lines, from a program that left the bottom 20 off. */
        height = 192;
        result = CODEC_OK;
    } else {
        buffer = malloc(SR_VRAM);
        if (buffer == NULL)
            return CODEC_NO_MEMORY;
        result = unpack_sr(data, length, buffer, &vram);
    }
    if (result == CODEC_OK)
        result = allocate(image, 256, height);
    if (result == CODEC_OK) {
        int full = data[0] == 0xfe && length == HEADER + 0xfaa0u;
        if (attributes || full)
            palette9(data + HEADER + 0xfa80, 16, palette);
        yjk(image, vram, attributes, (const uint8_t (*)[3])palette);
        if (full)
            sprites(image, data + HEADER, 12, 0xfa00, 0xf000,
                    (const uint8_t (*)[3])palette);
    }
    free(buffer);
    return result;
}

/* Graph Saurus keeps the palette of screens 5, 6 and 7 in its own file. */
static void companion(const uint8_t *file, size_t length, unsigned count,
                      uint8_t (*palette)[3])
{
    if (file != NULL && length >= count * 2u)
        palette9(file, count, palette);
    else if (count == 4)
        memcpy(palette, msx6, sizeof msx6);
    else
        palette9(msx2, 16, palette);
}

static enum codec_result graph_saurus(const uint8_t *data, size_t length,
                                      enum family family, const uint8_t *file,
                                      size_t file_length, struct msx_image *image)
{
    uint8_t palette[16][3], *buffer;
    const uint8_t *vram = NULL;
    unsigned height;
    enum codec_result result;

    companion(file, file_length, family == SR6 ? 4 : 16, palette);
    switch (family) {
    case SR5:
    case SR6:
        result = lines128(data, length, &height);
        if (result == CODEC_OK)
            result = allocate(image, family == SR5 ? 256 : 512, height);
        if (result != CODEC_OK)
            return result;
        if (family == SR5)
            nibbles(image, data + HEADER, 128, (const uint8_t (*)[3])palette);
        else
            crumbs(image, data + HEADER, (const uint8_t (*)[3])palette);
        return CODEC_OK;
    case SR7:
        buffer = malloc(SR_VRAM);
        if (buffer == NULL)
            return CODEC_NO_MEMORY;
        result = unpack_sr(data, length, buffer, &vram);
        if (result == CODEC_OK)
            result = allocate(image, 512, 212);
        if (result == CODEC_OK)
            nibbles(image, vram, 256, (const uint8_t (*)[3])palette);
        free(buffer);
        return result;
    default:
        /* Interlaced screen 7: both pages' 424 lines, with no header. */
        if (length != 424u * 256u)
            return length < 424u * 256u ? CODEC_TRUNCATED : CODEC_INVALID;
        result = allocate(image, 512, 424);
        if (result != CODEC_OK)
            return result;
        nibbles(image, data, 256, (const uint8_t (*)[3])palette);
        return CODEC_OK;
    }
}

enum codec_result msx_decode(const uint8_t *data, size_t length,
                             const char *name, const uint8_t *palette,
                             size_t palette_length, struct msx_image *image)
{
    enum family family = family_of(name);
    enum codec_result result;

    if (image == NULL)
        return CODEC_INVALID;
    image->width = image->height = 0;
    image->rgba = NULL;
    if (data == NULL)
        return CODEC_TRUNCATED;

    switch (family) {
    case SC2: result = screen2(data, length, 2, image); break;
    case SC3: result = screen3(data, length, image); break;
    case SC4: result = screen2(data, length, 4, image); break;
    case SC5: result = screen5(data, length, 5, image); break;
    case SC6: result = screen5(data, length, 6, image); break;
    case SC7: result = screen7(data, length, image); break;
    case SC8: result = screen8(data, length, image); break;
    case SCA: result = screen12(data, length, 1, image); break;
    case SCC: result = screen12(data, length, 0, image); break;
    case SR5: case SR6: case SR7: case SRI:
        result = graph_saurus(data, length, family, palette, palette_length, image);
        break;
    default:
        return CODEC_INVALID;
    }
    if (result != CODEC_OK)
        msx_free(image);
    return result;
}
