#include "decode.h"
#include <stdlib.h>
#include <string.h>

#define KISSCEL_MAX_PIXELS (16u * 1024u * 1024u)
#define KISSCEL_MAX_SIDE 65535u
#define KISSCEL_HEADER 32u
#define KISSCEL_OLD_HEADER 4u
#define KCF_OLD_GROUP 32u
/* KiSS/GS allows 10 palette files; later viewers allow more. */
#define CNF_MAX_KCF 256u
#define CNF_MAX_SETS 256u

static unsigned le16(const uint8_t *p)
{
    return (unsigned)p[0] | ((unsigned)p[1] << 8);
}

static int is_kiss(const uint8_t *data, size_t length)
{
    return length >= 4 && memcmp(data, "KiSS", 4) == 0;
}

static size_t row_bytes(const struct kisscel_info *info)
{
    switch (info->bpp) {
    case 4: return ((size_t)info->width + 1u) / 2u;
    case 8: return info->width;
    default: return (size_t)info->width * 4u;
    }
}

static size_t header_size(const uint8_t *data, size_t length)
{
    return is_kiss(data, length) ? KISSCEL_HEADER : KISSCEL_OLD_HEADER;
}

enum codec_result kisscel_info(const uint8_t *data, size_t length,
                               struct kisscel_info *info)
{
    size_t header, row;

    if (length < KISSCEL_OLD_HEADER)
        return CODEC_TRUNCATED;
    if (!is_kiss(data, length)) {
        info->width = le16(data);
        info->height = le16(data + 2);
        info->x = info->y = 0;
        info->bpp = 4;
    } else {
        if (length < KISSCEL_HEADER)
            return CODEC_TRUNCATED;
        /* 0x20 marks palette cels and 0x21 colour cels, but GIMP writes 0x20
           for 32-bit ones, so the mark doesn't decide the depth. */
        if (data[4] != 0x20 && data[4] != 0x21)
            return CODEC_INVALID;
        info->bpp = data[5];
        if (info->bpp != 4 && info->bpp != 8 && info->bpp != 32)
            return CODEC_INVALID;
        /* The reserved bytes should be zero; they are ignored. */
        info->width = le16(data + 8);
        info->height = le16(data + 10);
        info->x = le16(data + 12);
        info->y = le16(data + 14);
    }
    if (info->width == 0 || info->height == 0)
        return CODEC_INVALID;
    if (info->width + info->x > KISSCEL_MAX_SIDE ||
        info->height + info->y > KISSCEL_MAX_SIDE ||
        (size_t)(info->width + info->x) * (info->height + info->y) >
            KISSCEL_MAX_PIXELS)
        return CODEC_TOO_LARGE;
    header = header_size(data, length);
    row = row_bytes(info);
    if (row > (length - header) / info->height)
        return CODEC_TRUNCATED;
    return CODEC_OK;
}

static void grey(unsigned count, struct kisscel_palette *palette)
{
    unsigned i;
    palette->count = count;
    for (i = 0; i < count; i++)
        memset(palette->rgb + i * 3u, (int)(i * 256u / count), 3);
}

/* Palette pixels: index 0 is transparent, and one the palette lacks is black. */
static void put_index(uint8_t *out, unsigned index,
                      const struct kisscel_palette *palette)
{
    if (index == 0) {
        memset(out, 0, 4);
        return;
    }
    if (index < palette->count)
        memcpy(out, palette->rgb + index * 3u, 3);
    else
        memset(out, 0, 3);
    out[3] = 255;
}

enum codec_result kisscel_decode(const uint8_t *data, size_t length,
                                 const struct kisscel_palette *palette,
                                 struct kisscel_image *image)
{
    struct kisscel_info info;
    struct kisscel_palette ramp;
    enum codec_result result;
    const uint8_t *src;
    uint8_t *out;
    size_t header, row, stride;
    unsigned x, y, index;

    image->rgba = NULL;
    result = kisscel_info(data, length, &info);
    if (result != CODEC_OK)
        return result;
    if (palette == NULL && info.bpp != 32) {
        grey(1u << info.bpp, &ramp);
        palette = &ramp;
    }
    image->width = info.width + info.x;
    image->height = info.height + info.y;
    /* The area left of and above the cel stays transparent. */
    image->rgba = calloc((size_t)image->width * image->height, 4);
    if (image->rgba == NULL)
        return CODEC_NO_MEMORY;
    header = header_size(data, length);
    row = row_bytes(&info);
    stride = (size_t)image->width * 4u;
    for (y = 0; y < info.height; y++) {
        src = data + header + row * y;
        out = image->rgba + stride * (y + info.y) + (size_t)info.x * 4u;
        for (x = 0; x < info.width; x++, out += 4) {
            switch (info.bpp) {
            case 4:
                index = src[x / 2u];
                put_index(out, x & 1u ? index & 15u : index >> 4, palette);
                break;
            case 8:
                put_index(out, src[x], palette);
                break;
            default:
                /* Blue, green, red, then straight alpha. */
                out[0] = src[x * 4u + 2u];
                out[1] = src[x * 4u + 1u];
                out[2] = src[x * 4u];
                out[3] = src[x * 4u + 3u];
                break;
            }
        }
    }
    return CODEC_OK;
}

void kisscel_free(struct kisscel_image *image)
{
    free(image->rgba);
    image->rgba = NULL;
}

/* 12-bit colours are rrrrbbbb 0000gggg. Nibbles scale by 16, as GIMP does. */
static void colours12(const uint8_t *src, unsigned count,
                      struct kisscel_palette *palette)
{
    unsigned i;
    for (i = 0; i < count; i++, src += 2) {
        palette->rgb[i * 3u] = (uint8_t)(src[0] & 0xf0u);
        palette->rgb[i * 3u + 1u] = (uint8_t)((src[1] & 15u) << 4);
        palette->rgb[i * 3u + 2u] = (uint8_t)((src[0] & 15u) << 4);
    }
}

enum codec_result kisscel_palette(const uint8_t *data, size_t length,
                                  unsigned group,
                                  struct kisscel_palette *palette)
{
    unsigned bits, count, groups;
    size_t size;

    if (!is_kiss(data, length)) {
        /* Up to 10 groups of 16 12-bit colours, with no header. */
        groups = (unsigned)(length / KCF_OLD_GROUP);
        if (groups == 0)
            return CODEC_TRUNCATED;
        if (group >= groups)
            group = 0;
        palette->count = 16;
        colours12(data + (size_t)group * KCF_OLD_GROUP, 16, palette);
        return CODEC_OK;
    }
    if (length < KISSCEL_HEADER)
        return CODEC_TRUNCATED;
    if (data[4] != 0x10)
        return CODEC_INVALID;
    bits = data[5];
    count = le16(data + 8);
    groups = le16(data + 10);
    if ((bits != 12 && bits != 24) || (count != 16 && count != 256) ||
        groups == 0)
        return CODEC_INVALID;
    size = (size_t)count * (bits / 8u + (bits == 12));
    /* A file cut short keeps the groups it holds in full. */
    if ((length - KISSCEL_HEADER) / size < groups)
        groups = (unsigned)((length - KISSCEL_HEADER) / size);
    if (groups == 0)
        return CODEC_TRUNCATED;
    if (group >= groups)
        group = 0;
    data += KISSCEL_HEADER + size * group;
    palette->count = count;
    if (bits == 12)
        colours12(data, count, palette);
    else
        memcpy(palette->rgb, data, (size_t)count * 3u);
    return CODEC_OK;
}

static int lower(int c)
{
    return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
}

static int is_space(int c)
{
    return c == ' ' || c == '\t';
}

/* A token ends at white space, a comment, or a field marker. */
static size_t token(const char *p, const char *end)
{
    const char *start = p;
    while (p < end && !is_space(*p) && *p != ';' && *p != '*' && *p != ':')
        p++;
    return (size_t)(p - start);
}

static const char *skip_space(const char *p, const char *end)
{
    while (p < end && is_space(*p))
        p++;
    return p;
}

static int read_number(const char **p, const char *end, unsigned *value)
{
    unsigned v = 0;
    int digits = 0;
    while (*p < end && **p >= '0' && **p <= '9') {
        if (v < 100000u)
            v = v * 10u + (unsigned)(**p - '0');
        (*p)++;
        digits++;
    }
    *value = v;
    return digits;
}

/* Cel names are compared without their directory and ignoring case, as the
   DOS viewers KiSS started on did. */
static int same_name(const char *name, size_t length, const char *cel)
{
    size_t i, start = 0, cel_length = strlen(cel);
    for (i = 0; i < length; i++)
        if (name[i] == '/' || name[i] == '\\')
            start = i + 1u;
    if (length - start != cel_length)
        return 0;
    for (i = 0; i < cel_length; i++)
        if (lower((unsigned char)name[start + i]) != lower((unsigned char)cel[i]))
            return 0;
    return 1;
}

int kisscel_cnf_palette(const char *cnf, size_t length, const char *cel,
                        const char **kcf, size_t *kcf_length,
                        unsigned *group)
{
    const char *end = cnf + length, *line = cnf, *next, *p, *name;
    const char *kcf_name[CNF_MAX_KCF];
    size_t kcf_size[CNF_MAX_KCF], n;
    unsigned kcf_count = 0, sets = 0, set_group[CNF_MAX_SETS];
    unsigned palette = 0, set = 0, value;
    int found = 0, have_set;

    for (; line < end; line = next) {
        next = line;
        while (next < end && *next != '\n' && *next != '\r')
            next++;
        p = line + 1;
        switch (line < next ? *line : 0) {
        case '%':
            n = token(p, next);
            if (n > 0 && kcf_count < CNF_MAX_KCF) {
                kcf_name[kcf_count] = p;
                kcf_size[kcf_count++] = n;
            }
            break;
        case '$':
            /* One line per set, starting with the set's palette group. */
            p = skip_space(p, next);
            if (sets < CNF_MAX_SETS)
                set_group[sets++] = read_number(&p, next, &value) ? value : 0;
            break;
        case '#':
            if (found)
                break;
            /* #object[.fix] cel [*palette] [: sets] [; comment] */
            while (p < next && !is_space(*p) && *p != ';')
                p++;
            p = skip_space(p, next);
            name = p;
            n = token(p, next);
            if (n == 0 || !same_name(name, n, cel))
                break;
            found = 1;
            have_set = 0;
            for (p += n; p < next && *p != ';'; p++) {
                if (*p == '*') {
                    p = skip_space(p + 1, next);
                    if (!read_number(&p, next, &palette))
                        palette = 0;
                    p--;
                } else if (*p == ':') {
                    for (p++; p < next && *p != ';'; ) {
                        if (read_number(&p, next, &value)) {
                            if (!have_set || value < set)
                                set = value;
                            have_set = 1;
                        } else if (is_space(*p)) {
                            p++;
                        } else {
                            break;
                        }
                    }
                    p--;
                }
            }
            break;
        }
        if (next < end)
            next++;
    }
    if (!found || palette >= kcf_count)
        return 0;
    *kcf = kcf_name[palette];
    *kcf_length = kcf_size[palette];
    *group = set < sets ? set_group[set] : 0;
    return 1;
}
