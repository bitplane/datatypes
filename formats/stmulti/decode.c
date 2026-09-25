#include "decode.h"
#include "common/atarist.h"
#include <stdlib.h>
#include <string.h>

#define MAX_EVENTS 48u

static unsigned be16(const uint8_t *p)
{
    return ((unsigned)p[0] << 8) | p[1];
}

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)be16(p) << 16) | be16(p + 2);
}

static uint32_t rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

static void put(uint8_t *p, uint32_t c)
{
    p[0] = (uint8_t)(c >> 16);
    p[1] = (uint8_t)(c >> 8);
    p[2] = (uint8_t)c;
    p[3] = 255;
}

/* Averages each channel of two alternating screens into the first. */
static void blend(uint8_t *first, const uint8_t *second, size_t pixels)
{
    size_t i;

    for (i = 0; i < pixels * 4u; i++)
        first[i] = (uint8_t)((first[i] + second[i]) / 2u);
}

static enum codec_result alloc_screens(struct stmulti_image *image,
                                       unsigned width, unsigned height,
                                       unsigned screens)
{
    image->width = width;
    image->height = height;
    image->rgba = malloc((size_t)width * height * 4u * screens);
    return image->rgba != NULL ? CODEC_OK : CODEC_NO_MEMORY;
}

/* ---- Multi Palette Picture ---- */

/* Each mode reloads colours at fixed columns of every line. */
struct mpp_mode {
    unsigned width, height;
    unsigned colours;    /* palette entries stored per line */
    unsigned first_slot; /* slots from here to 15 load before each line */
    unsigned first_x;    /* column of the first reload */
    int black;           /* reload number that sets black, or -1 */
};

static const struct mpp_mode mpp_modes[4] = {
    { 320, 199, 52, 1, 33, 32 },
    { 320, 199, 46, 1, 9, 16 },
    { 320, 199, 54, 1, 4, 32 },
    { 416, 273, 48, 6, 69, -1 },
};

/* Columns from reload n to the next one. */
static unsigned mpp_gap(unsigned mode, unsigned n)
{
    switch (mode) {
    case 1:
        return n % 2u == 0 ? 4u : 16u;
    case 2:
        return 8u;
    default:
        if (n == 15)
            return mode == 0 ? 88u : 112u;
        if (n == 31)
            return 12u;
        if (n == 37)
            return 100u;
        return 4u;
    }
}

struct bits { const uint8_t *data; size_t length, bit; };

static long read_bits(struct bits *b, unsigned count)
{
    long v = 0;

    if (count > (b->length * 8u - b->bit))
        return -1;
    while (count-- > 0) {
        v = (v << 1) | ((b->data[b->bit / 8u] >> (7u - b->bit % 8u)) & 1u);
        b->bit++;
    }
    return v;
}

static unsigned five_bit(unsigned v)
{
    return (v << 3) | (v >> 2);
}

/* One palette entry: 9-bit ST, 12-bit STE or 15-bit, by the depth flags. */
static int mpp_colour(struct bits *b, unsigned depth, uint32_t *colour)
{
    long v;

    switch (depth) {
    case 0: /* RRRGGGBBB */
        if ((v = read_bits(b, 9)) < 0)
            return 0;
        *colour = rgb(st_level((unsigned)v >> 6, 0), st_level((unsigned)v >> 3, 0),
                      st_level((unsigned)v, 0));
        return 1;
    case 1: /* STE guns: least significant bit first */
        if ((v = read_bits(b, 12)) < 0)
            return 0;
        *colour = rgb(st_level((unsigned)v >> 8 & 15u, 1),
                      st_level((unsigned)v >> 4 & 15u, 1),
                      st_level((unsigned)v & 15u, 1));
        return 1;
    default: /* xyz rRRR gGGG bBBB: STE guns plus a fifth, lowest bit each */
        if ((v = read_bits(b, 15)) < 0)
            return 0;
        {
            unsigned u = (unsigned)v;
            unsigned r = ((u >> 7 & 0xeu) | (u >> 11 & 1u)) << 1 | (u >> 14 & 1u);
            unsigned g = ((u >> 3 & 0xeu) | (u >> 7 & 1u)) << 1 | (u >> 13 & 1u);
            unsigned bl = ((u << 1 & 0xeu) | (u >> 3 & 1u)) << 1 | (u >> 12 & 1u);
            *colour = rgb((uint8_t)five_bit(r), (uint8_t)five_bit(g),
                          (uint8_t)five_bit(bl));
        }
        return 1;
    }
}

struct event { unsigned x, slot; int black; };

static unsigned mpp_events(unsigned mode, struct event *events)
{
    const struct mpp_mode *m = &mpp_modes[mode];
    unsigned n = 0, x = m->first_x;

    while (x < m->width && n < MAX_EVENTS) {
        events[n].x = x;
        events[n].slot = n % 16u;
        events[n].black = (int)n == m->black;
        x += mpp_gap(mode, n);
        n++;
    }
    return n;
}

static enum codec_result mpp_screen(const uint8_t *palette, size_t palette_length,
                                    const uint8_t *bitmap, unsigned mode,
                                    unsigned depth, uint8_t *out)
{
    const struct mpp_mode *m = &mpp_modes[mode];
    struct event events[MAX_EVENTS];
    unsigned count = mpp_events(mode, events), x, y, s;
    struct bits b = { palette, palette_length, 0 };
    uint32_t slot[16] = { 0 };

    for (y = 0; y < m->height; y++) {
        const uint8_t *line = bitmap + (size_t)y * (m->width / 2u);
        unsigned e = 0;

        for (s = m->first_slot; s < 16; s++)
            if (!mpp_colour(&b, depth, &slot[s]))
                return CODEC_INVALID;
        for (x = 0; x < m->width; x++) {
            for (; e < count && events[e].x == x; e++) {
                if (events[e].black)
                    slot[events[e].slot] = 0;
                else if (!mpp_colour(&b, depth, &slot[events[e].slot]))
                    return CODEC_INVALID;
            }
            put(out, slot[st_pixel(line, 4, x)]);
            out += 4;
        }
    }
    return CODEC_OK;
}

static enum codec_result mpp_decode(const uint8_t *data, size_t length,
                                    struct stmulti_image *image)
{
    static const unsigned depth_bits[4] = { 9, 12, 0, 15 };
    unsigned mode, depth, screens, i;
    const struct mpp_mode *m;
    size_t offset, palette, bitmap;
    uint32_t extra;
    enum codec_result r;

    if (length < 12)
        return CODEC_TRUNCATED;
    mode = data[3];
    depth = data[4] & 3u;
    if (mode > 3 || depth == 2)
        return CODEC_INVALID;
    m = &mpp_modes[mode];
    screens = data[4] & 4u ? 2u : 1u;
    palette = ((size_t)m->colours * m->height * depth_bits[depth] + 15u) / 16u * 2u;
    bitmap = (size_t)m->width * m->height / 2u;
    extra = be32(data + 8);
    if (extra > length - 12u)
        return CODEC_TRUNCATED;
    offset = 12u + extra;
    if ((length - offset) / screens < palette + bitmap)
        return CODEC_TRUNCATED;
    if ((r = alloc_screens(image, m->width, m->height, screens)) != CODEC_OK)
        return r;
    for (i = 0; i < screens; i++) {
        const uint8_t *p = data + offset + i * (palette + bitmap);
        r = mpp_screen(p, palette, p + palette, mode, depth,
                       image->rgba + (size_t)i * m->width * m->height * 4u);
        if (r != CODEC_OK) {
            stmulti_free(image);
            return r;
        }
    }
    if (screens == 2)
        blend(image->rgba, image->rgba + (size_t)m->width * m->height * 4u,
              (size_t)m->width * m->height);
    return CODEC_OK;
}

/* ---- PhotoChrome ---- */

struct stream { const uint8_t *data; size_t length, pos; };

/* One PhotoChrome block: a count of commands, then runs of bytes (unit 1)
   or words (unit 2). Fills count values and skips whatever is left. */
static enum codec_result pcs_block(struct stream *s, uint8_t *out, size_t count,
                                   unsigned unit)
{
    size_t done = 0, commands, k;

    if (s->length - s->pos < 2)
        return CODEC_TRUNCATED;
    commands = be16(s->data + s->pos);
    s->pos += 2;
    while (commands-- > 0) {
        unsigned x;
        size_t n, fill;
        int literal;

        if (s->pos >= s->length)
            return CODEC_TRUNCATED;
        x = s->data[s->pos++];
        if (x == 0 || x == 1) {
            if (s->length - s->pos < 2)
                return CODEC_TRUNCATED;
            n = be16(s->data + s->pos);
            s->pos += 2;
            literal = x == 1;
        } else if (x < 128) {
            n = x;
            literal = 0;
        } else {
            n = 256u - x;
            literal = 1;
        }
        if (literal) {
            if ((s->length - s->pos) / unit < n)
                return CODEC_TRUNCATED;
            fill = n < count - done ? n : count - done;
            memcpy(out + done * unit, s->data + s->pos, fill * unit);
            s->pos += n * unit;
        } else {
            /* The value comes even when the run is empty. */
            if (s->length - s->pos < unit)
                return CODEC_TRUNCATED;
            fill = n < count - done ? n : count - done;
            for (k = 0; k < fill; k++)
                memcpy(out + (done + k) * unit, s->data + s->pos, unit);
            s->pos += unit;
        }
        done += fill;
    }
    return done == count ? CODEC_OK : CODEC_INVALID;
}

/* The palette slot that colour c uses at column x of a line, after
   Hans Wessels's public-domain description of PhotoChrome's timing. Slots
   48 to 63 are the next line's first 16. */
static unsigned pcs_slot(unsigned c, unsigned x)
{
    unsigned slot = c;

    if (x >= 4u * c)
        slot += 16u;
    if (c < 14) {
        if (x >= 4u * c + 76u)
            slot += 16u;
        if (x >= 176u + 10u * c - (c & 1u) * 6u)
            slot += 16u;
    } else if (x >= 4u * c + 92u) {
        slot += 16u;
    }
    return slot;
}

struct pcs_screen { uint8_t bitmap[PCS_BITMAP], palette[PCS_PALETTE_WORDS * 2u]; };

static void pcs_render(const struct pcs_screen *s, int ste, uint8_t *out)
{
    unsigned x, y, p;

    for (y = 0; y < PCS_HEIGHT; y++) {
        const uint8_t *words = s->palette + y * 96u;

        for (x = 0; x < PCS_WIDTH; x++) {
            size_t at = (size_t)(y + 1u) * 40u + x / 8u;
            unsigned c = 0;
            const uint8_t *w;

            for (p = 0; p < 4; p++)
                c |= ((s->bitmap[p * 8000u + at] >> (7u - x % 8u)) & 1u) << p;
            w = words + pcs_slot(c, x) * 2u;
            put(out, rgb(st_level(w[0], ste), st_level(w[1] >> 4, ste),
                         st_level(w[1], ste)));
            out += 4;
        }
    }
}

static enum codec_result pcs_decode(const uint8_t *data, size_t length,
                                    struct stmulti_image *image)
{
    struct stream s = { data, length, 6 };
    struct pcs_screen *screen;
    unsigned flags, screens, i;
    enum codec_result r = CODEC_OK;
    int ste = 0;

    if (length < 6)
        return CODEC_TRUNCATED;
    flags = data[4];
    screens = flags != 0 ? 2u : 1u;
    screen = malloc(sizeof *screen * screens);
    if (screen == NULL)
        return CODEC_NO_MEMORY;
    for (i = 0; i < screens && r == CODEC_OK; i++) {
        r = pcs_block(&s, screen[i].bitmap, PCS_BITMAP, 1);
        if (r == CODEC_OK)
            r = pcs_block(&s, screen[i].palette, PCS_PALETTE_WORDS, 2);
    }
    if (r == CODEC_OK && screens == 2) {
        /* Unless a flag says otherwise, the second screen is stored as its
           difference from the first. */
        if (!(flags & 1u))
            for (i = 0; i < PCS_BITMAP; i++)
                screen[1].bitmap[i] ^= screen[0].bitmap[i];
        if (!(flags & 2u))
            for (i = 0; i < sizeof screen->palette; i++)
                screen[1].palette[i] ^= screen[0].palette[i];
    }
    for (i = 0; i < screens && r == CODEC_OK; i++)
        ste |= st_is_ste(screen[i].palette, PCS_PALETTE_WORDS);
    if (r == CODEC_OK)
        r = alloc_screens(image, PCS_WIDTH, PCS_HEIGHT, screens);
    if (r == CODEC_OK) {
        for (i = 0; i < screens; i++)
            pcs_render(&screen[i], ste,
                       image->rgba + (size_t)i * PCS_WIDTH * PCS_HEIGHT * 4u);
        if (screens == 2)
            blend(image->rgba, image->rgba + PCS_WIDTH * PCS_HEIGHT * 4u,
                  PCS_WIDTH * PCS_HEIGHT);
    }
    free(screen);
    return r;
}

enum codec_result stmulti_decode(const uint8_t *data, size_t length,
                                 struct stmulti_image *image)
{
    image->rgba = NULL;
    if (length >= 3 && memcmp(data, "MPP", 3) == 0)
        return mpp_decode(data, length, image);
    if (length >= 4 && be16(data) == PCS_WIDTH && be16(data + 2) == 200u)
        return pcs_decode(data, length, image);
    return CODEC_INVALID;
}

void stmulti_free(struct stmulti_image *image)
{
    free(image->rgba);
    image->rgba = NULL;
}
