#include "encode.h"
#include <stdlib.h>
#include <string.h>

/* Search steps allowed per line before giving up on fitting its colours. */
#ifndef SEARCH_BUDGET
#define SEARCH_BUDGET 2000L
#endif

/* Each colour index is reloaded twice per line, so the line splits into
   segments at 32 reload columns; within one, the same 16 slots show. */
#define SEGMENTS 33u

/* One line's colours are numbered 0-47, so sets of colours and of slots
   fit in 64-bit masks. */
struct line {
    unsigned start[SEGMENTS + 1u]; /* first column of each segment */
    uint64_t live[SEGMENTS];       /* slots showing in each segment */
    uint64_t wanted[SEGMENTS];     /* colours each segment shows */
    unsigned words[48];            /* palette word of each colour */
    unsigned colours;
    int slot[48];                  /* colour held, or -1 if free */
    long budget;
    uint64_t missing[SEGMENTS], free_slots[SEGMENTS]; /* scratch */
    /* The search's choices so far: a colour and the slots to try it in. */
    struct choice {
        uint8_t candidates[16], count, next, colour;
    } stack[49];
};

enum { SOLVED, DEAD, BRANCH };

static void put_be16(uint8_t *p, unsigned v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static unsigned popcount(uint64_t v)
{
    static const uint8_t bits[16] = { 0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4 };
    unsigned n = 0;

    for (; v != 0; v >>= 4)
        n += bits[v & 15u];
    return n;
}

static uint8_t over_white(uint8_t c, uint8_t a)
{
    return (uint8_t)((c * a + 255u * (255u - a) + 127u) / 255u);
}

/* The palette nibble that decodes to level, or -1 if there is none. */
static int nibble(uint8_t level, int ste)
{
    unsigned n;

    for (n = 0; n < 16; n++)
        if ((ste || n < 8) && spectrum_level(n, ste) == level)
            return (int)n;
    return -1;
}

/* The palette word for a pixel, or -1 if it can't be stored. */
static int entry(const uint8_t *p, int ste)
{
    int r = nibble(over_white(p[0], p[3]), ste);
    int g = nibble(over_white(p[1], p[3]), ste);
    int b = nibble(over_white(p[2], p[3]), ste);

    if (r < 0 || g < 0 || b < 0)
        return -1;
    return (r << 8) | (g << 4) | b;
}

/* The segment layout, which is the same for every line. */
static void segment(struct line *l)
{
    unsigned n = 0, x, c;

    /* Segments start where some colour index changes slot. */
    for (x = 0; x < SPECTRUM_WIDTH; x++) {
        for (c = 0; x > 0 && c < 16; c++)
            if (spectrum_slot(c, x) != spectrum_slot(c, x - 1u))
                break;
        if (x == 0 || c < 16)
            l->start[n++] = x;
    }
    l->start[n] = SPECTRUM_WIDTH;
    for (n = 0; n < SEGMENTS; n++) {
        l->live[n] = 0;
        for (c = 0; c < 16; c++)
            l->live[n] |= (uint64_t)1 << spectrum_slot(c, l->start[n]);
    }
}

/* Check a partial search: SOLVED, DEAD, or BRANCH with the next choice
   to try in ch. */
static int examine(struct line *l, struct choice *ch)
{
    uint64_t *missing = l->missing, *free_slots = l->free_slots;
    uint64_t used = 0, need, room;
    unsigned scores[16], slack = 49, best = 0, n, m, s, i;

    for (s = 0; s < 48; s++)
        if (l->slot[s] >= 0)
            used |= (uint64_t)1 << s;
    for (n = 0; n < SEGMENTS; n++) {
        uint64_t held = 0;
        for (s = 0; s < 48; s++)
            if ((l->live[n] >> s & 1u) && l->slot[s] >= 0)
                held |= (uint64_t)1 << l->slot[s];
        missing[n] = l->wanted[n] & ~held;
        free_slots[n] = l->live[n] & ~used;
    }
    /* Every colour missing from a run of segments needs a free slot of its
       own that shows somewhere in the run. */
    for (n = 0; n < SEGMENTS; n++) {
        need = room = 0;
        for (m = n; m < SEGMENTS; m++) {
            need |= missing[m];
            room |= free_slots[m];
            if (popcount(need) > popcount(room))
                return DEAD;
        }
    }
    /* Work on the segment with the fewest free slots to spare. */
    for (n = 0; n < SEGMENTS; n++) {
        unsigned spare;
        if (missing[n] == 0)
            continue;
        spare = popcount(free_slots[n]) - popcount(missing[n]);
        if (spare < slack) {
            slack = spare;
            best = n;
        }
    }
    if (slack == 49)
        return SOLVED;
    for (ch->colour = 0; !(missing[best] >> ch->colour & 1u); ch->colour++)
        ;
    /* Try first the free slots that would show the colour in the most
       segments that miss it, then the shortest. */
    ch->count = ch->next = 0;
    for (s = 0; s < 48; s++) {
        unsigned score = 0, span = 0;
        if (!(free_slots[best] >> s & 1u))
            continue;
        for (n = 0; n < SEGMENTS; n++)
            if (l->live[n] >> s & 1u) {
                span++;
                score += (unsigned)(missing[n] >> ch->colour & 1u);
            }
        score = score * 64u + 63u - span;
        for (i = ch->count++; i > 0 && scores[i - 1] < score; i--) {
            scores[i] = scores[i - 1];
            ch->candidates[i] = ch->candidates[i - 1];
        }
        scores[i] = score;
        ch->candidates[i] = (uint8_t)s;
    }
    return BRANCH;
}

/* Give colours to free slots until every segment shows every colour it
   wants, backtracking when a choice leaves the line unsolvable. */
static int fill(struct line *l)
{
    unsigned depth = 0;
    int state = examine(l, &l->stack[0]);

    for (;;) {
        struct choice *ch;
        if (state == SOLVED)
            return 1;
        if (state == BRANCH) {
            if (--l->budget < 0)
                return 0;
            depth++;
        }
        /* Undo the last choice and take the next, going back up the stack
           past choices with none left. */
        for (;;) {
            if (depth == 0)
                return 0;
            ch = &l->stack[depth - 1u];
            if (ch->next > 0)
                l->slot[ch->candidates[ch->next - 1u]] = -1;
            if (ch->next < ch->count)
                break;
            depth--;
        }
        l->slot[ch->candidates[ch->next++]] = ch->colour;
        state = examine(l, &l->stack[depth]);
    }
}

static void put_pixel(uint8_t *line, unsigned x, unsigned c)
{
    uint8_t *group = line + (x / 16u) * 8u;
    unsigned bit = 15u - x % 16u, p;

    for (p = 0; p < 4; p++)
        if (c >> p & 1u)
            group[p * 2u + (bit < 8u)] |= (uint8_t)(1u << bit % 8u);
}

/* The number of a line's colour, adding it if it's new; -1 past 48. */
static int number(struct line *l, unsigned word)
{
    unsigned i;

    for (i = 0; i < l->colours && l->words[i] != word; i++)
        ;
    if (i == l->colours) {
        if (i == 48)
            return -1;
        l->words[l->colours++] = word;
    }
    return (int)i;
}

/* Fill in one line's pixels and palette. */
static int put_line(struct line *l, const uint8_t *rgba, int ste,
                    uint8_t *pixels, uint8_t *palette)
{
    unsigned n, x, s, c;

    l->colours = 0;
    for (n = 0; n < SEGMENTS; n++) {
        l->wanted[n] = 0;
        for (x = l->start[n]; x < l->start[n + 1u]; x++) {
            int id = number(l, (unsigned)entry(rgba + x * 4u, ste));
            if (id < 0)
                return 0;
            l->wanted[n] |= (uint64_t)1 << id;
        }
        if (popcount(l->wanted[n]) > 16)
            return 0;
    }
    for (s = 0; s < 48; s++)
        l->slot[s] = -1;
    l->budget = SEARCH_BUDGET;
    if (!fill(l))
        return 0;
    for (x = 0; x < SPECTRUM_WIDTH; x++) {
        int id = number(l, (unsigned)entry(rgba + x * 4u, ste));
        for (c = 0; l->slot[spectrum_slot(c, x)] != id; c++)
            ;
        put_pixel(pixels, x, c);
    }
    for (s = 0; s < 48; s++)
        if (l->slot[s] >= 0)
            put_be16(palette + s * 2u, l->words[l->slot[s]]);
    return 1;
}

enum codec_result spectrum_encode(const uint8_t *rgba, unsigned width,
                                  unsigned height,
                                  uint8_t output[SPU_FILE_SIZE])
{
    struct line *l;
    unsigned y, s;
    size_t i;
    int ste, marked = 0, spare = -1;

    if (rgba == NULL || output == NULL)
        return CODEC_INVALID;
    if (width != SPECTRUM_WIDTH || height != SPECTRUM_HEIGHT)
        return CODEC_INVALID;
    /* Line 0 has no palette, so it must be black. */
    for (i = 0; i < width; i++)
        if (entry(rgba + i * 4u, 0) != 0)
            return CODEC_INVALID;
    /* ST levels where they cover the picture, STE levels otherwise. */
    for (ste = 0; ste < 2; ste++) {
        for (i = width; i < (size_t)width * height; i++)
            if (entry(rgba + i * 4u, ste) < 0)
                break;
        if (i == (size_t)width * height)
            break;
    }
    if (ste == 2)
        return CODEC_INVALID;

    l = malloc(sizeof *l);
    if (l == NULL)
        return CODEC_NO_MEMORY;
    memset(output, 0, SPU_FILE_SIZE);
    segment(l);
    for (y = 1; y < height; y++) {
        uint8_t *palette = output + SPU_SCREEN_SIZE + (y - 1u) * 96u;
        if (!put_line(l, rgba + (size_t)y * width * 4u, ste,
                      output + y * 160u, palette)) {
            free(l);
            return CODEC_INVALID;
        }
        for (s = 0; s < 48; s++) {
            if (l->slot[s] < 0)
                spare = (int)((y - 1u) * 48u + s);
            else
                marked |= (int)(l->words[l->slot[s]] & 0x888u);
        }
    }
    free(l);
    /* An STE palette is recognised by a fourth bit in any colour. */
    if (ste && !marked) {
        if (spare < 0)
            return CODEC_INVALID;
        put_be16(output + SPU_SCREEN_SIZE + (unsigned)spare * 2u, 0x888);
    }
    return CODEC_OK;
}
