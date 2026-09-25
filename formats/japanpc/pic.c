/* PIC: Yanagisawa's picture format, first on the Sharp X68000, then on the
   PC-88VA, FM TOWNS, Macintosh and MSX. */
#include <stdlib.h>
#include <string.h>

#include "japanpc.h"

#define UNSET 0xffffffffu
#define CACHE 128u

/* Recently used colours of deep pictures. A new colour replaces the least
   recently used slot; a slot is addressed by its number, not its rank. */
struct cache {
    uint32_t colour[CACHE];
    uint8_t newer[CACHE], older[CACHE];
    unsigned newest;
};

static void cache_init(struct cache *c)
{
    unsigned i;
    /* From newest to oldest: slot 0, then 127 down to 1. */
    for (i = 0; i < CACHE; i++) {
        c->colour[i] = 0;
        c->older[i] = (uint8_t)((i + CACHE - 1u) % CACHE);
        c->newer[i] = (uint8_t)((i + 1u) % CACHE);
    }
    c->newest = 0;
}

static void cache_touch(struct cache *c, unsigned slot)
{
    unsigned oldest;
    if (slot == c->newest)
        return;
    c->older[c->newer[slot]] = c->older[slot];
    c->newer[c->older[slot]] = c->newer[slot];
    oldest = c->newer[c->newest];
    c->older[slot] = (uint8_t)c->newest;
    c->newer[slot] = (uint8_t)oldest;
    c->newer[c->newest] = (uint8_t)slot;
    c->older[oldest] = (uint8_t)slot;
    c->newest = slot;
}

static void cache_add(struct cache *c, uint32_t colour)
{
    /* The ring's oldest slot follows the newest; it becomes the newest. */
    c->newest = c->newer[c->newest];
    c->colour[c->newest] = colour;
}

/* A run length: n ones, a zero, then n + 1 bits, plus 2^(n+1) - 1. */
static enum codec_result pic_length(struct jp_bits *b, size_t *length)
{
    unsigned bits;
    for (bits = 1; bits < 21; bits++) {
        int bit = jp_bit(b);
        long value;
        if (bit < 0)
            return CODEC_TRUNCATED;
        if (bit == 0) {
            value = jp_read(b, bits);
            if (value < 0)
                return CODEC_TRUNCATED;
            *length = (size_t)value + (1u << bits) - 1u;
            return CODEC_OK;
        }
    }
    return CODEC_INVALID;
}

/* Paint colour down the lines from at, one step of -2 to 2 pixels each. */
static enum codec_result pic_chain(struct jp_bits *b, uint32_t *rgb,
                                   size_t count, unsigned width, size_t at,
                                   uint32_t colour)
{
    long pos = (long)at; /* at most 16M pixels */
    for (;;) {
        long code = jp_read(b, 2);
        int bit;
        if (code < 0)
            return CODEC_TRUNCATED;
        if (code == 0) {
            if ((bit = jp_bit(b)) < 0)
                return CODEC_TRUNCATED;
            if (bit == 0)
                return CODEC_OK;
            if ((bit = jp_bit(b)) < 0)
                return CODEC_TRUNCATED;
            pos += bit ? 2 : -2;
        } else {
            pos += code - 2; /* 1, 2, 3: one left, straight down, one right */
        }
        pos += (long)width;
        if (pos < 0 || (size_t)pos >= count)
            return CODEC_INVALID;
        rgb[pos] = colour;
    }
}

/* How a colour code is laid out, most significant field first. */
enum pic_colour {
    PIC_INDEX,      /* palette index, no cache */
    PIC_X68K,       /* GGGGGRRRRRBBBBBI */
    PIC_X68K_15,    /* GGGGGRRRRRBBBBB */
    PIC_G6R5B5,     /* PC-88VA */
    PIC_G4R4B4,
    PIC_R5G5B5,     /* Macintosh */
    PIC_G8R8B8,
    PIC_RAW         /* kept as read: PC-88VA dithered pairs */
};

struct pic_format {
    unsigned depth;
    enum pic_colour colour;
    uint32_t palette[256];
};

static uint32_t rgb(unsigned r, unsigned g, unsigned b)
{
    return (uint32_t)r << 16 | (uint32_t)g << 8 | b;
}

/* Scale a bits-wide value to 8 bits by repeating it. */
static unsigned scale(unsigned value, unsigned bits)
{
    unsigned out = 0;
    int shift = 8 - (int)bits;
    for (; shift > -(int)bits; shift -= (int)bits)
        out |= shift >= 0 ? value << shift : value >> -shift;
    return out & 255u;
}

static uint32_t g3r3b2(unsigned c)
{
    return rgb(scale(c >> 2 & 7u, 3), scale(c >> 5, 3), scale(c & 3u, 2));
}

static uint32_t pic_colour(enum pic_colour kind, unsigned long c)
{
    switch (kind) {
    case PIC_X68K: return jp_x68k_colour((unsigned)c);
    case PIC_X68K_15: return jp_x68k_colour((unsigned)c << 1);
    case PIC_G6R5B5: return jp_g6r5b5_colour((unsigned)c);
    case PIC_G4R4B4:
        return rgb(scale(c >> 4 & 15u, 4), scale(c >> 8 & 15u, 4),
                   scale(c & 15u, 4));
    case PIC_R5G5B5:
        return rgb(scale(c >> 10 & 31u, 5), scale(c >> 5 & 31u, 5),
                   scale(c & 31u, 5));
    case PIC_G8R8B8:
        return rgb(c >> 8 & 255u, c >> 16 & 255u, c & 255u);
    case PIC_INDEX:
    case PIC_RAW:
        break;
    }
    return (uint32_t)c;
}

static enum codec_result pic_pixels(struct jp_bits *b,
                                    const struct pic_format *f,
                                    uint32_t *rgb, unsigned width,
                                    size_t count)
{
    uint32_t colour = 0;
    struct cache *cache = NULL;
    enum codec_result result = CODEC_OK;
    size_t i, at = 0;

    for (i = 0; i < count; i++)
        rgb[i] = UNSET;
    if (f->colour != PIC_INDEX) {
        cache = malloc(sizeof *cache);
        if (cache == NULL)
            return CODEC_NO_MEMORY;
        cache_init(cache);
    }
    for (;;) {
        size_t length;
        long value;
        int bit;

        if ((result = pic_length(b, &length)) != CODEC_OK)
            break;
        /* The run repeats the colour, but a pixel a chain has painted
           takes over as the colour from there on. */
        for (; length > 1; length--, at++) {
            if (rgb[at] == UNSET)
                rgb[at] = colour;
            else
                colour = rgb[at];
            if (at + 1 >= count)
                goto done;
        }

        if (f->colour == PIC_INDEX) {
            if ((value = jp_read(b, f->depth)) < 0)
                goto truncated;
            colour = f->palette[value];
        } else if ((bit = jp_bit(b)) < 0) {
            goto truncated;
        } else if (bit == 0) {
            if ((value = jp_read(b, f->depth)) < 0)
                goto truncated;
            colour = pic_colour(f->colour, (unsigned long)value);
            cache_add(cache, colour);
        } else {
            if ((value = jp_read(b, 7)) < 0)
                goto truncated;
            cache_touch(cache, (unsigned)value);
            colour = cache->colour[value];
        }
        rgb[at] = colour;
        if (at + 1 >= count)
            break;

        if ((bit = jp_bit(b)) < 0)
            goto truncated;
        if (bit && (result = pic_chain(b, rgb, count, width, at,
                                       colour)) != CODEC_OK)
            break;
        at++;
    }
done:
    free(cache);
    return result;
truncated:
    free(cache);
    return CODEC_TRUNCATED;
}

static unsigned be16(const uint8_t *p) { return (unsigned)p[0] << 8 | p[1]; }

/* Repeat pixels across or down when the ratio is a whole 2:1. */
static void pic_ratio(unsigned across, unsigned down, unsigned *repeat_x,
                      unsigned *repeat_y)
{
    if (across != 0 && across == down * 2u)
        *repeat_x = 2;
    else if (down != 0 && down == across * 2u)
        *repeat_y = 2;
}

enum codec_result jp_decode_pic(const uint8_t *data, size_t size,
                                struct jp_canvas *canvas)
{
    struct pic_format f;
    struct jp_bits bits;
    unsigned model, mode, width, height, i, repeat_x = 1, repeat_y = 1;
    unsigned palette_bits = 16, colours = 0;
    size_t at = 3, count;
    enum codec_result result;
    enum jp_dac dac = JP_DAC_X68K;
    int dithered = 0;

    if (size >= 7 && memcmp(data + 3, "/MM/", 4) == 0)
        dac = JP_DAC_3; /* MSX, by the comment */
    /* A comment ending in 0x1A, then padding ending in 0. */
    while (at < size && data[at] != 0x1a)
        at++;
    while (at < size && data[at] != 0)
        at++;
    at++;
    if (at > size || size - at < 8)
        return CODEC_TRUNCATED;
    /* A reserved 0, the machine and its mode, then the colour depth. */
    if (data[at] != 0)
        return CODEC_INVALID;
    model = data[at + 1] & 15u;
    mode = data[at + 1] >> 4;
    f.depth = be16(data + at + 2);
    width = be16(data + at + 4);
    height = be16(data + at + 6);
    at += 8;

    switch (model) {
    case 0x0: /* X68000 */
        if (mode != 0)
            return CODEC_INVALID;
        if (f.depth == 4 || f.depth == 8)
            f.colour = PIC_INDEX;
        else if (f.depth == 15)
            f.colour = PIC_X68K_15;
        else if (f.depth == 16)
            f.colour = PIC_X68K;
        else
            return CODEC_INVALID;
        break;
    case 0x1: /* PC-88VA: HR mode, or 256 colours dithered in pairs */
        if (f.depth == 8) {
            f.colour = PIC_INDEX;
            palette_bits = 0;
            for (i = 0; i < 256; i++)
                f.palette[i] = g3r3b2(i);
        } else if (f.depth == 12) {
            f.colour = PIC_G4R4B4;
        } else if (f.depth == 16) {
            dithered = (mode & 2u) != 0;
            f.colour = dithered ? PIC_RAW : PIC_G6R5B5;
        } else {
            return CODEC_INVALID;
        }
        /* Full-screen pictures, sized against 640x400 unless in HR mode. */
        if (!(mode & 1u)) {
            repeat_x = width <= 320 ? 2 : 1;
            repeat_y = height * (dithered ? 2u : 1u) <= 204 ? 2 : 1;
        }
        break;
    case 0x2: /* FM TOWNS */
        if (mode == 0)
            at += 6; /* a screen position */
        dac = JP_DAC_TOWNS;
        if (f.depth == 4 || f.depth == 8)
            f.colour = PIC_INDEX;
        else if (f.depth == 15)
            f.colour = PIC_X68K_15;
        else if (f.depth == 16)
            f.colour = PIC_X68K;
        else
            return CODEC_INVALID;
        break;
    case 0x3: /* Macintosh */
        if (f.depth != 15)
            return CODEC_INVALID;
        f.colour = PIC_R5G5B5;
        break;
    case 0xf: /* generic: position, pixel ratio, and a packed palette */
        if (mode != 0 && mode != 1 && mode != 15)
            return CODEC_INVALID;
        if (size - at < 7)
            return CODEC_TRUNCATED;
        if (mode == 15)
            pic_ratio(data[at + 4], data[at + 5], &repeat_x, &repeat_y);
        at += 6;
        if (f.depth == 4 || f.depth == 8) {
            f.colour = PIC_INDEX;
            palette_bits = data[at++];
            if (palette_bits == 0 || palette_bits > 8)
                return CODEC_INVALID;
        } else if (f.depth == 12) {
            f.colour = PIC_G4R4B4;
        } else if (f.depth == 15) {
            f.colour = PIC_X68K_15;
        } else if (f.depth == 16) {
            f.colour = PIC_X68K;
        } else if (f.depth == 24) {
            f.colour = PIC_G8R8B8;
        } else {
            return CODEC_INVALID;
        }
        break;
    default:
        return CODEC_INVALID;
    }
    if (at > size)
        return CODEC_TRUNCATED;

    if (f.colour == PIC_INDEX && palette_bits == 16) {
        /* X68000 colour words */
        colours = 1u << f.depth;
        if ((size - at) / 2u < colours)
            return CODEC_TRUNCATED;
        for (i = 0; i < colours; i++) {
            uint32_t c = jp_x68k_colour(be16(data + at + i * 2u));
            f.palette[i] = jp_dac_colour(dac, c >> 16, c >> 8 & 255u,
                                         c & 255u);
        }
        at += colours * 2u;
    } else if (f.colour == PIC_INDEX && palette_bits != 0) {
        /* green, red and blue fields of palette_bits each, packed */
        colours = 1u << f.depth;
        jp_bits_init(&bits, data, at, size);
        for (i = 0; i < colours; i++) {
            long g = jp_read(&bits, palette_bits),
                 r = jp_read(&bits, palette_bits),
                 b = jp_read(&bits, palette_bits);
            if (b < 0)
                return CODEC_TRUNCATED;
            f.palette[i] = rgb(scale((unsigned)r, palette_bits),
                               scale((unsigned)g, palette_bits),
                               scale((unsigned)b, palette_bits));
        }
        at = bits.pos;
    }

    if (dithered) {
        /* Each 16-bit colour is two 8-bit pixels, first in the low byte;
           a line holds an even line, then the odd one below it. */
        if (height > JP_MAX_SIDE / 2u)
            return CODEC_TOO_LARGE;
        result = jp_canvas_init(canvas, width, height * 2u, repeat_x,
                                repeat_y);
    } else {
        result = jp_canvas_init(canvas, width, height, repeat_x, repeat_y);
    }
    if (result != CODEC_OK)
        return result;
    count = (size_t)width * height;
    jp_bits_init(&bits, data, at, size);
    result = pic_pixels(&bits, &f, canvas->rgb, width, count);
    if (result == CODEC_OK && dithered)
        for (i = (unsigned)count; i-- > 0;) {
            uint32_t pair = canvas->rgb[i];
            canvas->rgb[i * 2u + 1u] = g3r3b2(pair >> 8 & 255u);
            canvas->rgb[i * 2u] = g3r3b2(pair & 255u);
        }
    return result;
}
