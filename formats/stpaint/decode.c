#include "decode.h"
#include "common/atarist.h"
#include <stdlib.h>
#include <string.h>

enum kind { NONE, TINY, CRACKART, IMAGIC, STAD, DALI, PABLO, PICWORKS, PAINTSHOP };

/* Tiny's packer pads to 128-byte blocks. Much more than that after the
   streams means a file from another program that shares the extension. */
#define TINY_SLACK 512u
#define PABLO_ID "PABLO PACKED PICTURE: Groupe CDND \r\n"

static unsigned be16(const uint8_t *p)
{
    return ((unsigned)p[0] << 8) | p[1];
}

static int lower(int c)
{
    return c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c;
}

/* The format and, for Dali, the resolution its extension names. */
static enum kind by_name(const char *name, unsigned *mode)
{
    static const struct { char ext[4]; enum kind kind; unsigned mode; } table[] = {
        { "tny", TINY, 0 }, { "tn1", TINY, 0 }, { "tn2", TINY, 0 },
        { "tn3", TINY, 0 }, { "tn4", TINY, 0 }, { "tn5", TINY, 0 },
        { "tn6", TINY, 0 }, { "ca1", CRACKART, 0 }, { "ca2", CRACKART, 0 },
        { "ca3", CRACKART, 0 }, { "ic1", IMAGIC, 0 }, { "ic2", IMAGIC, 0 },
        { "ic3", IMAGIC, 0 }, { "pac", STAD, 0 }, { "lpk", DALI, 0 },
        { "mpk", DALI, 1 }, { "hpk", DALI, 2 }, { "ppp", PABLO, 0 },
        { "pa3", PABLO, 0 }, { "cp3", PICWORKS, 0 }, { "psc", PAINTSHOP, 0 },
    };
    const char *ext = NULL, *p;
    unsigned i;

    if (name == NULL)
        return NONE;
    for (p = name; *p != '\0'; p++) {
        if (*p == '/' || *p == ':')
            ext = NULL;
        else if (*p == '.')
            ext = p + 1;
    }
    if (ext == NULL || strlen(ext) != 3)
        return NONE;
    for (i = 0; i < sizeof table / sizeof table[0]; i++)
        if (lower(ext[0]) == table[i].ext[0] && lower(ext[1]) == table[i].ext[1] &&
            lower(ext[2]) == table[i].ext[2]) {
            *mode = table[i].mode;
            return table[i].kind;
        }
    return NONE;
}

static int crackart_signature(const uint8_t *data, size_t length)
{
    return length >= 4 && data[0] == 'C' && data[1] == 'A' && data[2] <= 1 &&
           data[3] <= 2;
}

/* Signatures come first; the name decides only for formats without one. */
static enum kind identify(const uint8_t *data, size_t length, const char *name,
                          unsigned *mode)
{
    enum kind kind;

    if (length >= 4 && memcmp(data, "IMDC", 4) == 0)
        return IMAGIC;
    if (length >= 4 && memcmp(data, "pM8", 3) == 0 && (data[3] == '5' || data[3] == '6'))
        return STAD;
    if (length >= 4 && memcmp(data, "tm89", 4) == 0)
        return PAINTSHOP;
    if (length >= sizeof PABLO_ID - 1 && memcmp(data, PABLO_ID, sizeof PABLO_ID - 1) == 0)
        return PABLO;
    kind = by_name(name, mode);
    if (kind == NONE && crackart_signature(data, length))
        return CRACKART;
    return kind;
}

/* Writes count copies of value from *n on, stopping at the end of the screen. */
static void fill(uint8_t *out, unsigned *n, size_t count, unsigned value)
{
    while (count-- > 0 && *n < STPAINT_SCREEN)
        out[(*n)++] = (uint8_t)value;
}

/* Lays a stream out in columns: byte n goes step bytes after byte n-1, and a
   column that passes the end starts again one byte right of the last. */
static void columns(const uint8_t *stream, uint8_t *screen, unsigned step)
{
    unsigned n, pos = 0, column = 0;

    for (n = 0; n < STPAINT_SCREEN; n++) {
        screen[pos] = stream[n];
        pos += step;
        if (pos >= STPAINT_SCREEN)
            pos = ++column;
    }
}

/* A positive decimal number of at most limit, then CR LF. */
static long ascii_number(const uint8_t *data, size_t length, size_t *offset,
                         long limit)
{
    size_t i = *offset;
    long value = 0;

    if (i >= length || data[i] < '0' || data[i] > '9')
        return i >= length ? -2 : -1;
    while (i < length && data[i] >= '0' && data[i] <= '9') {
        value = value * 10 + (data[i++] - '0');
        if (value > limit)
            return -1;
    }
    if (i + 2 > length)
        return -2;
    if (data[i] != '\r' || data[i + 1] != '\n' || value == 0)
        return -1;
    *offset = i + 2;
    return value;
}

struct picture {
    uint8_t *screen;
    unsigned mode;
    const uint8_t *palette;
};

static enum codec_result tiny(const uint8_t *data, size_t length, struct picture *pic)
{
    size_t header, controls, words, control, value, end;
    unsigned n = 0, mode;

    if (length < 1)
        return CODEC_TRUNCATED;
    mode = data[0];
    if (mode > 5)
        return CODEC_INVALID;
    /* Modes 3-5 add four bytes of colour cycling, which a still ignores. */
    header = mode > 2 ? 5u : 1u;
    if (length < header + 36u)
        return CODEC_TRUNCATED;
    controls = be16(data + header + 32);
    words = be16(data + header + 34);
    control = header + 36u;
    value = control + controls;
    end = value + words * 2u;
    if (length < end)
        return CODEC_TRUNCATED;
    if (length > end + TINY_SLACK)
        return CODEC_INVALID;
    pic->mode = mode % 3u;
    pic->palette = data + header;

    /* 16000 words, as four sets of 20 word columns 200 lines high. Set s holds
       columns s, s+4, ..., s+76, which in low resolution is plane s. */
    while (n < STPAINT_SCREEN / 2u) {
        size_t count;
        int literal;
        unsigned x;

        if (control >= value)
            return CODEC_INVALID;
        x = data[control++];
        if (x < 2) {
            if (control + 2 > value)
                return CODEC_INVALID;
            count = be16(data + control);
            control += 2;
            literal = x == 1;
        } else if (x < 128) {
            count = x;
            literal = 0;
        } else {
            count = 256u - x;
            literal = 1;
        }
        if (!literal && value + 2 > end)
            return CODEC_INVALID;
        for (; count > 0 && n < STPAINT_SCREEN / 2u; count--, n++) {
            unsigned set = n / 4000u, column = set + (n % 4000u) / 200u * 4u;
            uint8_t *dst = pic->screen + (n % 200u) * 160u + column * 2u;
            if (value + 2 > end)
                return CODEC_INVALID;
            dst[0] = data[value];
            dst[1] = data[value + 1];
            if (literal)
                value += 2;
        }
        if (!literal)
            value += 2;
    }
    return CODEC_OK;
}

static enum codec_result crackart(const uint8_t *data, size_t length,
                                  struct picture *pic, uint8_t *stream)
{
    static const unsigned palette_size[3] = { 32, 8, 0 };
    size_t i;
    unsigned escape, delta, step, n = 0;

    if (length < 4)
        return CODEC_TRUNCATED;
    if (!crackart_signature(data, length))
        return CODEC_INVALID;
    pic->mode = data[3];
    pic->palette = data + 4;
    i = 4u + palette_size[pic->mode];
    if (data[2] == 0) {
        if (length < i + STPAINT_SCREEN)
            return CODEC_TRUNCATED;
        memcpy(pic->screen, data + i, STPAINT_SCREEN);
        return CODEC_OK;
    }
    if (length < i + 4u)
        return CODEC_TRUNCATED;
    escape = data[i];
    delta = data[i + 1];
    step = be16(data + i + 2);
    i += 4;
    if (step >= STPAINT_SCREEN)
        return CODEC_INVALID;
    /* Every byte starts as delta; a step of 0 means that is the whole picture. */
    memset(stream, (int)delta, STPAINT_SCREEN);
    while (step != 0 && n < STPAINT_SCREEN) {
        unsigned b, c, lo;
        size_t count;

        if (i >= length)
            return CODEC_TRUNCATED;
        b = data[i++];
        if (b != escape) {
            stream[n++] = (uint8_t)b;
            continue;
        }
        if (i >= length)
            return CODEC_TRUNCATED;
        c = data[i++];
        if (c == escape) {
            stream[n++] = (uint8_t)c;
            continue;
        }
        if (i >= length)
            return CODEC_TRUNCATED;
        b = data[i++];
        if (c == 2 && b == 0)
            break;
        if (c == 1 || c == 2) {
            if (i >= length)
                return CODEC_TRUNCATED;
            lo = data[i++];
            count = ((size_t)b << 8 | lo) + 1u;
            b = delta;
        } else {
            count = c == 0 ? b + 1u : c + 1u;
        }
        if (c < 2) {
            if (i >= length)
                return CODEC_TRUNCATED;
            b = data[i++];
        }
        fill(stream, &n, count, b);
    }
    columns(stream, pic->screen, step == 0 ? 1u : step);
    return CODEC_OK;
}

/* Imagic's long count: 257, plus 256 for each further 1, then a byte that is
   skipped, then the low part. */
static int imagic_count(const uint8_t *data, size_t length, size_t *i, size_t *count)
{
    *count = 257;
    for (;;) {
        if (*i >= length)
            return 0;
        if (data[(*i)++] != 1)
            break;
        if (*count < 2u * STPAINT_SCREEN)
            *count += 256;
    }
    if (*i >= length)
        return 0;
    *count += data[(*i)++];
    return 1;
}

static enum codec_result imagic(const uint8_t *data, size_t length,
                                struct picture *pic, uint8_t *stream)
{
    size_t i = 0x43;
    unsigned escape, n = 0;

    if (length < 0x43)
        return CODEC_TRUNCATED;
    /* C8 02 marks a compressed picture; no uncompressed ones are known. */
    if (memcmp(data, "IMDC", 4) != 0 || data[4] != 0 || data[5] > 2 ||
        data[0x40] != 0xc8 || data[0x41] != 2)
        return CODEC_INVALID;
    pic->mode = data[5];
    pic->palette = data + 6;
    escape = data[0x42];
    /* Film deltas take ESC 02 runs from a base picture; alone they are 0. */
    memset(stream, 0, STPAINT_SCREEN);
    while (n < STPAINT_SCREEN) {
        unsigned b, c;
        size_t count;

        if (i >= length)
            return CODEC_TRUNCATED;
        b = data[i++];
        if (b != escape) {
            stream[n++] = (uint8_t)b;
            continue;
        }
        if (i >= length)
            return CODEC_TRUNCATED;
        c = data[i++];
        if (c == escape) {
            stream[n++] = (uint8_t)c;
            continue;
        }
        if (c == 2) {
            if (i >= length)
                return CODEC_TRUNCATED;
            c = data[i++];
            if (c == 0)
                break;
            if (c == 2) {
                /* A block ending in 0 that every real file has once. */
                while (i < length && data[i++] != 0)
                    ;
                continue;
            }
            if (c == 1) {
                if (!imagic_count(data, length, &i, &count))
                    return CODEC_TRUNCATED;
            } else {
                count = c + 1u;
            }
            fill(stream, &n, count, 0);
            continue;
        }
        if (c == 1) {
            if (!imagic_count(data, length, &i, &count))
                return CODEC_TRUNCATED;
        } else if (c == 0) {
            if (i >= length)
                return CODEC_TRUNCATED;
            count = data[i++] + 1u;
        } else {
            count = c + 1u;
        }
        if (i >= length)
            return CODEC_TRUNCATED;
        fill(stream, &n, count, data[i++]);
    }
    columns(stream, pic->screen, 160);
    return CODEC_OK;
}

static enum codec_result stad(const uint8_t *data, size_t length,
                              struct picture *pic, uint8_t *stream)
{
    size_t i = 7;
    unsigned id, pack, special, n = 0;

    if (length < 7)
        return CODEC_TRUNCATED;
    if (memcmp(data, "pM8", 3) != 0 || (data[3] != '5' && data[3] != '6'))
        return CODEC_INVALID;
    id = data[4];
    pack = data[5];
    special = data[6];
    pic->mode = 2;
    pic->palette = NULL;
    while (n < STPAINT_SCREEN) {
        unsigned b;

        if (i >= length) {
            /* Some writers stop one byte short of the screen. */
            if (n == STPAINT_SCREEN - 1u) {
                stream[n++] = 0;
                break;
            }
            return CODEC_TRUNCATED;
        }
        b = data[i++];
        if (b == id) {
            if (i >= length)
                return CODEC_TRUNCATED;
            fill(stream, &n, data[i++] + 1u, pack);
        } else if (b == special) {
            if (i + 2 > length)
                return CODEC_TRUNCATED;
            fill(stream, &n, data[i + 1] + 1u, data[i]);
            i += 2;
        } else {
            stream[n++] = (uint8_t)b;
        }
    }
    /* pM86 is packed down 80 byte columns of 400 lines. */
    columns(stream, pic->screen, data[3] == '6' ? 80u : 1u);
    return CODEC_OK;
}

static enum codec_result dali(const uint8_t *data, size_t length, unsigned mode,
                              struct picture *pic)
{
    size_t i = 32, counts, values, run = 0, entry = 0;
    long size;
    unsigned x, y;

    size = ascii_number(data, length, &i, 32000);
    if (size < 0)
        return size == -2 ? CODEC_TRUNCATED : CODEC_INVALID;
    counts = (size_t)size;
    /* The value table's size; the values follow the counts regardless. */
    size = ascii_number(data, length, &i, 32000);
    if (size < 0)
        return size == -2 ? CODEC_TRUNCATED : CODEC_INVALID;
    values = i + counts;
    pic->mode = mode;
    pic->palette = data;
    /* Runs of longwords down 40 columns of 200 lines, for every resolution. */
    for (x = 0; x < 160; x += 4) {
        for (y = 0; y < 200; y++) {
            const uint8_t *value;
            if (run == 0) {
                if (entry >= counts)
                    return CODEC_INVALID;
                if (values + entry * 4u + 4u > length)
                    return CODEC_TRUNCATED;
                run = data[i + entry++];
                if (run == 0)
                    return CODEC_INVALID;
            }
            value = data + values + (entry - 1u) * 4u;
            memcpy(pic->screen + y * 160u + x, value, 4);
            run--;
        }
    }
    return CODEC_OK;
}

static enum codec_result pablo(const uint8_t *data, size_t length, struct picture *pic)
{
    size_t i = sizeof PABLO_ID - 1;
    long size;

    if (length < i || memcmp(data, PABLO_ID, i) != 0)
        return length < i ? CODEC_TRUNCATED : CODEC_INVALID;
    size = ascii_number(data, length, &i, 65535);
    if (size < 0)
        return size == -2 ? CODEC_TRUNCATED : CODEC_INVALID;
    if (length < i + 2u)
        return CODEC_TRUNCATED;
    /* Compression type 29 is undocumented, and no such files are known. */
    if (data[i] > 2 || data[i + 1] != 0)
        return CODEC_INVALID;
    pic->mode = data[i];
    pic->palette = data + i + 4u;
    if (length < i + 36u + STPAINT_SCREEN)
        return CODEC_TRUNCATED;
    memcpy(pic->screen, data + i + 36u, STPAINT_SCREEN);
    return CODEC_OK;
}

static enum codec_result picworks(const uint8_t *data, size_t length, struct picture *pic)
{
    size_t table, value, i;
    unsigned n = 0;

    /* A count of entries, then per entry: literal 8-byte units and repeats of
       the next 8 bytes. The literals left over end the file exactly, which is
       all that identifies it. */
    if (length < 4)
        return CODEC_TRUNCATED;

    table = ((size_t)be16(data) + 1u) * 4u;
    value = table;
    if (length < value)
        return CODEC_TRUNCATED;
    pic->mode = 2;
    pic->palette = NULL;
    for (i = 4; i < table; i += 4) {
        size_t literals = (size_t)be16(data + i) * 8u, repeats = (size_t)be16(data + i + 2) * 8u, k;
        if (n + literals + repeats > STPAINT_SCREEN)
            return CODEC_INVALID;
        if (value + literals + 8u > length)
            return CODEC_TRUNCATED;
        memcpy(pic->screen + n, data + value, literals);
        n += (unsigned)literals;
        value += literals;
        for (k = 0; k < repeats; k += 8)
            memcpy(pic->screen + n + k, data + value, 8);
        n += (unsigned)repeats;
        value += 8;
    }
    if (length < value + (STPAINT_SCREEN - n))
        return CODEC_TRUNCATED;
    if (length != value + (STPAINT_SCREEN - n))
        return CODEC_INVALID;
    memcpy(pic->screen + n, data + value, STPAINT_SCREEN - n);
    return CODEC_OK;
}

static enum codec_result allocate(struct stpaint_image *image, unsigned width,
                                  unsigned height)
{
    image->rgba = malloc((size_t)width * height * 4u);
    if (image->rgba == NULL)
        return CODEC_NO_MEMORY;
    image->width = width;
    image->height = height;
    return CODEC_OK;
}

/* One bit per pixel, 1 black, from bits with the given bytes per line. */
static void mono(const uint8_t *bits, unsigned stride, struct stpaint_image *image)
{
    unsigned x, y;
    uint8_t *dst = image->rgba;

    for (y = 0; y < image->height; y++)
        for (x = 0; x < image->width; x++) {
            uint8_t level = (bits[y * stride + x / 8u] >> (7u - x % 8u) & 1u) ? 0 : 255;
            dst[0] = dst[1] = dst[2] = level;
            dst[3] = 255;
            dst += 4;
        }
}

static enum codec_result paintshop(const uint8_t *data, size_t length,
                                   struct stpaint_image *image)
{
    unsigned width, height, stride, total, n = 0;
    size_t i = 14;
    uint8_t *bits;
    enum codec_result result;

    if (length < 15)
        return CODEC_TRUNCATED;
    if (memcmp(data, "tm89", 4) != 0 || data[8] != 2 || data[9] != 1)
        return CODEC_INVALID;
    width = be16(data + 10) + 1u;
    height = be16(data + 12) + 1u;
    if (width > 640 || height > 400)
        return CODEC_INVALID;
    stride = (width + 7u) / 8u;
    total = stride * height;
    bits = malloc(total);
    if (bits == NULL)
        return CODEC_NO_MEMORY;

    if (data[14] == 99) {
        /* Stored: the bitmap, then FF. */
        if (length < 15u + total + 1u) {
            free(bits);
            return CODEC_TRUNCATED;
        }
        memcpy(bits, data + 15, total);
    } else {
        /* One command per line, or per run of repeated lines, then FF. */
        while (n < total) {
            unsigned command, lines;
            if (i >= length) {
                free(bits);
                return CODEC_TRUNCATED;
            }
            command = data[i++];
            switch (command) {
            case 0:
            case 200:
                memset(bits + n, command ? 0xff : 0, stride);
                n += stride;
                break;
            case 10:
            case 12:
                if (i >= length) {
                    free(bits);
                    return CODEC_TRUNCATED;
                }
                lines = data[i++] + (command == 12 ? 257u : 1u);
                if (n == 0 || n + lines * stride > total) {
                    free(bits);
                    return CODEC_INVALID;
                }
                for (; lines > 0; lines--, n += stride)
                    memcpy(bits + n, bits + n - stride, stride);
                break;
            case 100:
                if (i >= length) {
                    free(bits);
                    return CODEC_TRUNCATED;
                }
                memset(bits + n, data[i++], stride);
                n += stride;
                break;
            case 102: {
                unsigned k;
                if (i + 2 > length) {
                    free(bits);
                    return CODEC_TRUNCATED;
                }
                for (k = 0; k < stride; k++)
                    bits[n + k] = data[i + (k & 1u)];
                i += 2;
                n += stride;
                break;
            }
            case 110:
                if (i + stride > length) {
                    free(bits);
                    return CODEC_TRUNCATED;
                }
                memcpy(bits + n, data + i, stride);
                i += stride;
                n += stride;
                break;
            default:
                free(bits);
                return CODEC_INVALID;
            }
        }
        /* The closing FF is required only to show the file is complete. */
        if (i >= length) {
            free(bits);
            return CODEC_TRUNCATED;
        }
    }
    result = allocate(image, width, height);
    if (result == CODEC_OK)
        mono(bits, stride, image);
    free(bits);
    return result;
}

static enum codec_result render(const struct picture *pic, struct stpaint_image *image)
{
    unsigned planes = 4u >> pic->mode, width = pic->mode ? 640u : 320u;
    unsigned height = pic->mode == 2 ? 400u : 200u, stride = width * planes / 8u;
    uint8_t palette[16][3];
    unsigned x, y;
    uint8_t *dst;
    enum codec_result result;

    if (pic->mode == 2) {
        result = allocate(image, width, height);
        if (result == CODEC_OK)
            mono(pic->screen, stride, image);
        return result;
    }
    /* Only the colours the resolution uses decide whether it is STE. */
    st_palette(pic->palette, 1u << planes, palette);
    result = allocate(image, width, height);
    if (result != CODEC_OK)
        return result;
    dst = image->rgba;
    for (y = 0; y < height; y++)
        for (x = 0; x < width; x++) {
            const uint8_t *rgb = palette[st_pixel(pic->screen + y * stride, planes, x)];
            dst[0] = rgb[0];
            dst[1] = rgb[1];
            dst[2] = rgb[2];
            dst[3] = 255;
            dst += 4;
        }
    return CODEC_OK;
}

void stpaint_free(struct stpaint_image *image)
{
    free(image->rgba);
    image->rgba = NULL;
    image->width = image->height = 0;
}

enum codec_result stpaint_decode(const uint8_t *data, size_t length,
                                 const char *name, struct stpaint_image *image)
{
    struct picture pic;
    unsigned mode = 0;
    enum kind kind;
    uint8_t *buffer;
    enum codec_result result;

    if (image == NULL)
        return CODEC_INVALID;
    image->width = image->height = 0;
    image->rgba = NULL;
    if (data == NULL)
        return CODEC_TRUNCATED;
    kind = identify(data, length, name, &mode);
    if (kind == NONE)
        return CODEC_INVALID;
    if (kind == PAINTSHOP)
        return paintshop(data, length, image);

    /* The screen, then room for a stream laid out in columns. */
    buffer = malloc(2u * STPAINT_SCREEN);
    if (buffer == NULL)
        return CODEC_NO_MEMORY;
    pic.screen = buffer;
    switch (kind) {
    case TINY: result = tiny(data, length, &pic); break;
    case CRACKART: result = crackart(data, length, &pic, buffer + STPAINT_SCREEN); break;
    case IMAGIC: result = imagic(data, length, &pic, buffer + STPAINT_SCREEN); break;
    case STAD: result = stad(data, length, &pic, buffer + STPAINT_SCREEN); break;
    case DALI: result = dali(data, length, mode, &pic); break;
    case PABLO: result = pablo(data, length, &pic); break;
    default: result = picworks(data, length, &pic); break;
    }
    if (result == CODEC_OK)
        result = render(&pic, image);
    free(buffer);
    return result;
}
