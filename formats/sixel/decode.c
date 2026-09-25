#include "decode.h"
#include <stdlib.h>
#include <string.h>

#define SIXEL_MAX_PIXELS (16ul * 1024ul * 1024ul)
#define SIXEL_MAX_SIDE 65535ul
/* Colour registers. ImageMagick has 1024; higher numbers use the last. */
#define SIXEL_REGISTERS 1024u
/* Positions and repeats stop growing here, past the largest valid side. */
#define SIXEL_POS_CAP (SIXEL_MAX_SIDE + 1ul)
/* Parameters stop growing here, past every value that is clamped. */
#define SIXEL_PARAM_CAP 0xfffffful
#define SIXEL_PARAMS 5u
#define UNDRAWN 0xffffu

#define ESC 0x1b
#define DCS 0x90
#define ST 0x9c

/* The VT340's 16 colours, in percent. */
static const uint8_t vt340[16][3] = {
    { 0, 0, 0 }, { 20, 20, 80 }, { 80, 13, 13 }, { 20, 80, 20 },
    { 80, 20, 80 }, { 20, 80, 80 }, { 80, 80, 20 }, { 53, 53, 53 },
    { 26, 26, 26 }, { 33, 33, 60 }, { 60, 26, 26 }, { 33, 60, 33 },
    { 60, 33, 60 }, { 33, 60, 60 }, { 60, 60, 33 }, { 80, 80, 80 }
};

struct params { unsigned long value[SIXEL_PARAMS]; unsigned count; };

struct state {
    const uint8_t *data;
    size_t pos, end;
    unsigned long x, y, repeat, max_x, max_y, raster_w, raster_h;
    unsigned color;
    uint8_t (*palette)[3];
    uint8_t *rgba;
    unsigned long width, height;
};

static int is_digit(uint8_t c) { return c >= '0' && c <= '9'; }

static uint8_t percent(unsigned long p)
{
    if (p > 100)
        p = 100;
    return (uint8_t)((p * 255u + 50u) / 100u);
}

/* Line breaks and other C0 controls are ignored everywhere in the data,
   even inside a number, since some writers wrap lines at a fixed width. */
static int is_control(uint8_t c) { return c < 0x20; }

static int is_blank(uint8_t c) { return c == ' ' || is_control(c); }

/* Numbers separated by ';'. An empty field is 0; a trailing ';' adds none.
   Spaces between them are skipped. */
static void read_params(struct state *s, struct params *p)
{
    unsigned long n;

    p->count = 0;
    while (s->pos < s->end) {
        uint8_t c = s->data[s->pos];
        if (is_blank(c)) {
            s->pos++;
        } else if (is_digit(c)) {
            for (n = 0; s->pos < s->end; s->pos++) {
                c = s->data[s->pos];
                if (is_control(c))
                    continue;
                if (!is_digit(c))
                    break;
                n = n * 10u + (unsigned long)(c - '0');
                if (n > SIXEL_PARAM_CAP)
                    n = SIXEL_PARAM_CAP;
            }
            if (p->count < SIXEL_PARAMS)
                p->value[p->count] = n;
            p->count++;
            while (s->pos < s->end && is_blank(s->data[s->pos]))
                s->pos++;
            if (s->pos < s->end && s->data[s->pos] == ';')
                s->pos++;
        } else if (c == ';') {
            if (p->count < SIXEL_PARAMS)
                p->value[p->count] = 0;
            p->count++;
            s->pos++;
        } else {
            break;
        }
    }
    if (p->count > SIXEL_PARAMS)
        p->count = SIXEL_PARAMS;
}

/* DEC HLS: hue 0 is blue, 120 red and 240 green. Integer HSL in units
   of 1/1200000, so every step is exact. */
static void hls(unsigned long h, unsigned long l, unsigned long s, uint8_t *rgb)
{
    const unsigned long scale = 1200000ul;
    unsigned long chroma, x, m, sector, offset, v[3];
    unsigned i;

    if (h > 360)
        h = 360;
    if (l > 100)
        l = 100;
    if (s > 100)
        s = 100;
    h = (h + 240u) % 360u;
    /* chroma = (1 - |2L - 1|) * S, m = L - chroma / 2 */
    chroma = (l <= 50 ? 2u * l : 200u - 2u * l) * s * 120u;
    m = l * 12000u - chroma / 2u;
    sector = h / 60u;
    offset = h % 60u;
    x = chroma / 60u * (sector % 2u == 0 ? offset : 60u - offset);
    switch (sector) {
    case 0: v[0] = chroma; v[1] = x; v[2] = 0; break;
    case 1: v[0] = x; v[1] = chroma; v[2] = 0; break;
    case 2: v[0] = 0; v[1] = chroma; v[2] = x; break;
    case 3: v[0] = 0; v[1] = x; v[2] = chroma; break;
    case 4: v[0] = x; v[1] = 0; v[2] = chroma; break;
    default: v[0] = chroma; v[1] = 0; v[2] = x; break;
    }
    for (i = 0; i < 3; i++)
        rgb[i] = (uint8_t)(((v[i] + m) * 255u + scale / 2u) / scale);
}

static void define_color(struct state *s, const struct params *p)
{
    uint8_t *rgb = s->palette[s->color];

    if (p->value[1] == 1) {
        hls(p->value[2], p->value[3], p->value[4], rgb);
    } else if (p->value[1] == 2) {
        rgb[0] = percent(p->value[2]);
        rgb[1] = percent(p->value[3]);
        rgb[2] = percent(p->value[4]);
    }
    /* Other colour systems are undefined; the register keeps its colour. */
}

static void draw(struct state *s, unsigned bits, unsigned long count)
{
    unsigned long row, x;
    unsigned bit;

    for (bit = 0; bit < 6; bit++) {
        if ((bits & (1u << bit)) == 0)
            continue;
        row = s->y + bit;
        if (row + 1u > s->max_y)
            s->max_y = row + 1u;
        if (s->x + count > s->max_x)
            s->max_x = s->x + count;
        if (s->rgba == NULL)
            continue;
        /* The first pass sized the image to hold every pixel drawn. */
        if (s->x + count > s->width || row >= s->height)
            continue;
        for (x = s->x; x < s->x + count; x++) {
            uint8_t *p = s->rgba + (row * s->width + x) * 4u;
            p[0] = (uint8_t)(s->color & 0xffu);
            p[1] = (uint8_t)(s->color >> 8);
        }
    }
}

/* Interpret the sixel data from s->pos to s->end. Without rgba, only
   measure the drawn area and the raster attributes. */
static void run(struct state *s)
{
    struct params p;

    while (s->pos < s->end) {
        uint8_t c = s->data[s->pos++];
        if (c >= 0x3f && c <= 0x7e) {
            unsigned long count = s->repeat;
            s->repeat = 1;
            if (c != 0x3f)
                draw(s, (unsigned)(c - 0x3f), count);
            s->x += count;
            if (s->x > SIXEL_POS_CAP)
                s->x = SIXEL_POS_CAP;
        } else if (c == '!') {
            read_params(s, &p);
            s->repeat = p.count > 0 && p.value[0] > 0 ? p.value[0] : 1;
            if (s->repeat > SIXEL_POS_CAP)
                s->repeat = SIXEL_POS_CAP;
        } else if (c == '#') {
            read_params(s, &p);
            if (p.count > 0)
                s->color = p.value[0] < SIXEL_REGISTERS ? (unsigned)p.value[0]
                                                        : SIXEL_REGISTERS - 1u;
            if (p.count == SIXEL_PARAMS && s->palette != NULL)
                define_color(s, &p);
        } else if (c == '"') {
            /* Pan and Pad give the pixel aspect ratio, which is ignored. */
            read_params(s, &p);
            if (p.count > 2 && p.value[2] > 0)
                s->raster_w = p.value[2];
            if (p.count > 3 && p.value[3] > 0)
                s->raster_h = p.value[3];
        } else if (c == '$') {
            s->x = 0;
            s->repeat = 1;
        } else if (c == '-') {
            s->x = 0;
            s->repeat = 1;
            s->y += 6;
            if (s->y > SIXEL_POS_CAP)
                s->y = SIXEL_POS_CAP;
        }
        /* Anything else, such as line breaks, is ignored. */
    }
}

/* Find the next sixel introducer at or after pos: ESC P or DCS, then
   parameters, then q. Sets *params to the first parameter byte and returns
   the offset of the data after q, or 0 if there is none. *cut is set when
   the data ends inside what may be an introducer. */
static size_t find_image(const uint8_t *data, size_t length, size_t pos,
                         size_t *params, int *cut)
{
    size_t i, k;

    *cut = 0;
    for (i = pos; i < length; i++) {
        if (data[i] == ESC && i + 1 < length && data[i + 1] == 'P')
            k = i + 2;
        else if (data[i] == DCS)
            k = i + 1;
        else if (data[i] == ESC && i + 1 == length)
            k = length;
        else
            continue;
        *params = k;
        while (k < length && (is_digit(data[k]) || data[k] == ';'))
            k++;
        if (k == length)
            *cut = 1;
        else if (data[k] == 'q')
            return k + 1;
    }
    return 0;
}

/* The string terminator: ESC \, or 0x9C. Any other escape also ends the
   string, as it does on a terminal. Returns length if there is none, or
   if the data stops after the ESC. */
static size_t find_end(const uint8_t *data, size_t length, size_t pos)
{
    for (; pos < length; pos++)
        if (data[pos] == ST || (data[pos] == ESC && pos + 1 < length))
            return pos;
    return length;
}

unsigned sixel_count(const uint8_t *data, size_t length)
{
    size_t pos = 0, start, params, end;
    unsigned count = 0;
    int cut;

    while ((start = find_image(data, length, pos, &params, &cut)) != 0) {
        end = find_end(data, length, start);
        if (end == length)
            break;
        count++;
        /* An ESC that ends one image may start the next. */
        pos = end;
    }
    return count;
}

static void make_palette(uint8_t (*palette)[3])
{
    unsigned n = 0, r, g, b;

    for (; n < 16; n++) {
        palette[n][0] = percent(vt340[n][0]);
        palette[n][1] = percent(vt340[n][1]);
        palette[n][2] = percent(vt340[n][2]);
    }
    /* Then xterm's 256-colour layout, as ImageMagick and libsixel use:
       a 6x6x6 cube and a grey ramp. The rest start white. */
    for (r = 0; r < 6; r++)
        for (g = 0; g < 6; g++)
            for (b = 0; b < 6; b++, n++) {
                palette[n][0] = (uint8_t)(r * 51u);
                palette[n][1] = (uint8_t)(g * 51u);
                palette[n][2] = (uint8_t)(b * 51u);
            }
    for (r = 0; r < 24; r++, n++)
        palette[n][0] = palette[n][1] = palette[n][2] = (uint8_t)(r * 11u);
    for (; n < SIXEL_REGISTERS; n++)
        palette[n][0] = palette[n][1] = palette[n][2] = 255;
}

static void reset(struct state *s, const uint8_t *data, size_t start, size_t end)
{
    s->data = data;
    s->pos = start;
    s->end = end;
    s->x = s->y = 0;
    s->repeat = 1;
    s->max_x = s->max_y = 0;
    s->raster_w = s->raster_h = 0;
    s->color = 0;
}

enum codec_result sixel_decode(const uint8_t *data, size_t length, unsigned index,
                               struct sixel_image *image)
{
    uint8_t (*palette)[3];
    struct state s;
    struct params p;
    size_t pos = 0, start, params = 0, end = 0, i, pixels;
    unsigned long width, height;
    int transparent, cut;
    unsigned n;

    image->width = image->height = 0;
    image->rgba = NULL;
    for (n = 0;; n++) {
        start = find_image(data, length, pos, &params, &cut);
        if (start == 0)
            return cut ? CODEC_TRUNCATED : CODEC_INVALID;
        end = find_end(data, length, start);
        if (end == length)
            return CODEC_TRUNCATED;
        if (n == index)
            break;
        pos = end;
    }

    /* P1 selects the pixel aspect ratio, which is ignored like the raster
       attributes' Pan and Pad. P2 = 1 leaves undrawn pixels transparent;
       otherwise they take colour 0. P3 is the grid size, also ignored. */
    reset(&s, data, params, start - 1);
    read_params(&s, &p);
    transparent = p.count > 1 && p.value[1] == 1;

    reset(&s, data, start, end);
    s.palette = NULL;
    s.rgba = NULL;
    run(&s);
    width = s.max_x > s.raster_w ? s.max_x : s.raster_w;
    height = s.max_y > s.raster_h ? s.max_y : s.raster_h;
    if (width == 0)
        width = 1;
    if (height == 0)
        height = 1;
    if (width > SIXEL_MAX_SIDE || height > SIXEL_MAX_SIDE ||
        width * height > SIXEL_MAX_PIXELS)
        return CODEC_TOO_LARGE;

    pixels = (size_t)width * height;
    image->rgba = malloc(pixels * 4u);
    palette = malloc(SIXEL_REGISTERS * sizeof *palette);
    if (image->rgba == NULL || palette == NULL) {
        free(palette);
        sixel_free(image);
        return CODEC_NO_MEMORY;
    }
    /* Until the palette is final each pixel holds its register number. */
    memset(image->rgba, 0xff, pixels * 4u);
    make_palette(palette);
    reset(&s, data, start, end);
    s.palette = palette;
    s.rgba = image->rgba;
    s.width = width;
    s.height = height;
    run(&s);

    /* Registers apply to pixels drawn before their last definition too,
       as on the VT340. */
    for (i = 0; i < pixels; i++) {
        uint8_t *q = image->rgba + i * 4u;
        unsigned reg = (unsigned)q[0] | (unsigned)q[1] << 8;
        if (reg == UNDRAWN) {
            if (transparent) {
                q[0] = q[1] = q[2] = q[3] = 0;
                continue;
            }
            reg = 0;
        }
        q[0] = palette[reg][0];
        q[1] = palette[reg][1];
        q[2] = palette[reg][2];
        q[3] = 255;
    }
    free(palette);
    image->width = (unsigned)width;
    image->height = (unsigned)height;
    return CODEC_OK;
}

void sixel_free(struct sixel_image *image)
{
    free(image->rgba);
    image->rgba = NULL;
}
