#include "decode.h"
#include "colors.h"
#include <stdlib.h>
#include <string.h>

#define XPM_MAX_PIXELS (16u * 1024u * 1024u)
/* Larger numbers are reported as this, which no field accepts. */
#define XPM_HUGE 0x10000000ul

/* Which C array a string belongs to. Only XPM1 cares. */
enum array { ARRAY_ANY, ARRAY_COLORS, ARRAY_PIXELS, ARRAY_OTHER };

struct reader {
    const uint8_t *p, *end;
    int natural;             /* XPM2 without C syntax: one string per line */
    int xpm1;                /* strings come from the named arrays */
    enum array array;        /* the brace block being read, for XPM1 */
    int truncated;           /* a comment or string ran off the end */
};

struct span {
    const uint8_t *text;
    size_t length;
};

struct color_table {
    unsigned count, cpp;
    const uint8_t **codes;   /* each points at cpp bytes of the input */
    uint8_t *rgba;           /* four bytes per colour */
    int32_t *slots;          /* open addressing, -1 for empty */
    size_t mask;
};

static int is_space(uint8_t c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
}

static int is_ident(uint8_t c, int first)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' ||
           (!first && c >= '0' && c <= '9');
}

static int lower(uint8_t c)
{
    return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
}

/* A case-insensitive comparison of a span's end with a lower case suffix. */
static int ends_with(struct span s, const char *suffix)
{
    size_t n = strlen(suffix), i;
    if (s.length < n)
        return 0;
    for (i = 0; i < n; i++)
        if (lower(s.text[s.length - n + i]) != (uint8_t)suffix[i])
            return 0;
    return 1;
}

static int span_is(struct span s, const char *word)
{
    return s.length == strlen(word) && memcmp(s.text, word, s.length) == 0;
}

/* Skip a comment whose "/" is at p. Returns the byte after it, or NULL. */
static const uint8_t *skip_comment(const uint8_t *p, const uint8_t *end)
{
    for (p += 2; end - p >= 2; p++)
        if (p[0] == '*' && p[1] == '/')
            return p + 2;
    return NULL;
}

static int at_comment(const uint8_t *p, const uint8_t *end)
{
    return end - p >= 2 && p[0] == '/' && p[1] == '*';
}

/* The next line with text that is not a comment, without its line end. */
static int next_line(struct reader *r, struct span *s)
{
    while (r->p < r->end) {
        const uint8_t *start = r->p, *eol = memchr(start, '\n', (size_t)(r->end - start));
        size_t length;
        r->p = eol != NULL ? eol + 1 : r->end;
        length = (size_t)((eol != NULL ? eol : r->end) - start);
        if (length > 0 && start[length - 1] == '\r')
            length--;
        if (length == 0 || start[0] == '!')
            continue;
        s->text = start;
        s->length = length;
        return 1;
    }
    return 0;
}

/* The next C string literal, from the wanted array for XPM1. Strings are
   taken literally up to the next quote, as libXpm does. */
static int next_string(struct reader *r, enum array want, struct span *s)
{
    struct span ident = { NULL, 0 };

    if (r->natural)
        return next_line(r, s);
    while (r->p < r->end) {
        const uint8_t *p = r->p;
        if (at_comment(p, r->end)) {
            r->p = skip_comment(p, r->end);
            if (r->p == NULL) {
                r->p = r->end;
                r->truncated = 1;
                return 0;
            }
        } else if (*p == '"') {
            const uint8_t *close = memchr(p + 1, '"', (size_t)(r->end - p - 1));
            if (close == NULL) {
                r->p = r->end;
                r->truncated = 1;
                return 0;
            }
            r->p = close + 1;
            if (!r->xpm1 || r->array == want) {
                s->text = p + 1;
                s->length = (size_t)(close - p - 1);
                return 1;
            }
        } else if (is_ident(*p, 1)) {
            ident.text = p;
            while (p < r->end && is_ident(*p, 0))
                p++;
            ident.length = (size_t)(p - ident.text);
            r->p = p;
        } else {
            if (*p == '{')
                r->array = ends_with(ident, "_colors") ? ARRAY_COLORS :
                           ends_with(ident, "_pixels") ? ARRAY_PIXELS : ARRAY_OTHER;
            else if (*p == '}')
                r->array = ARRAY_ANY;
            r->p++;
        }
    }
    return 0;
}

/* Split off the next whitespace-separated word of s. */
static int next_word(struct span *s, struct span *word)
{
    while (s->length > 0 && is_space(*s->text)) {
        s->text++;
        s->length--;
    }
    if (s->length == 0)
        return 0;
    word->text = s->text;
    while (s->length > 0 && !is_space(*s->text)) {
        s->text++;
        s->length--;
    }
    word->length = (size_t)(s->text - word->text);
    return 1;
}

static int parse_number(struct span word, unsigned long *value)
{
    size_t i;
    *value = 0;
    if (word.length == 0)
        return 0;
    for (i = 0; i < word.length; i++) {
        if (word.text[i] < '0' || word.text[i] > '9')
            return 0;
        *value = *value * 10u + (unsigned long)(word.text[i] - '0');
        if (*value > XPM_HUGE)
            *value = XPM_HUGE;
    }
    return 1;
}

struct values {
    unsigned long width, height, ncolors, cpp, hot_x, hot_y;
};

/* "width height ncolors cpp [x_hot y_hot] [XPMEXT]". Anything unexpected
   after the four required values is ignored. */
static enum codec_result parse_values(struct span s, struct values *v)
{
    unsigned long *fields[4];
    struct span word;
    unsigned long x, y;
    int i;

    fields[0] = &v->width;
    fields[1] = &v->height;
    fields[2] = &v->ncolors;
    fields[3] = &v->cpp;
    for (i = 0; i < 4; i++)
        if (!next_word(&s, &word) || !parse_number(word, fields[i]))
            return CODEC_INVALID;
    if (next_word(&s, &word) && parse_number(word, &x) &&
        next_word(&s, &word) && parse_number(word, &y)) {
        v->hot_x = x;
        v->hot_y = y;
    }
    return CODEC_OK;
}

/* XPM1 keeps its values in #define lines before the arrays. */
static enum codec_result parse_defines(struct reader *r, struct values *v)
{
    int seen = 0;

    for (;;) {
        struct span name, word, line;
        unsigned long value;
        const uint8_t *eol;

        while (r->p < r->end && is_space(*r->p))
            r->p++;
        if (at_comment(r->p, r->end)) {
            r->p = skip_comment(r->p, r->end);
            if (r->p == NULL)
                return CODEC_TRUNCATED;
            continue;
        }
        if (r->p == r->end || *r->p != '#')
            break;
        eol = memchr(r->p, '\n', (size_t)(r->end - r->p));
        line.text = r->p + 1;
        line.length = (size_t)((eol != NULL ? eol : r->end) - line.text);
        r->p = eol != NULL ? eol + 1 : r->end;
        if (!next_word(&line, &word) || !span_is(word, "define") ||
            !next_word(&line, &name) || !next_word(&line, &word) ||
            !parse_number(word, &value))
            continue;
        if (ends_with(name, "_format"))
            seen |= 1;
        else if (ends_with(name, "_width"))
            v->width = value, seen |= 2;
        else if (ends_with(name, "_height"))
            v->height = value, seen |= 4;
        else if (ends_with(name, "_ncolors"))
            v->ncolors = value, seen |= 8;
        else if (ends_with(name, "_pixel"))
            v->cpp = value, seen |= 16;
    }
    return seen == 31 ? CODEC_OK : CODEC_INVALID;
}

/* Work out the syntax from the start of the file and position r after it. */
static enum codec_result start(struct reader *r, struct values *v)
{
    const uint8_t *p = r->p, *end = r->end;
    struct span first, second;

    if (end - p >= 3 && p[0] == 0xef && p[1] == 0xbb && p[2] == 0xbf)
        p += 3;
    while (p < end && is_space(*p))
        p++;
    if (at_comment(p, end)) {
        const uint8_t *after = skip_comment(p, end);
        struct span body;
        if (after == NULL)
            return CODEC_TRUNCATED;
        body.text = p + 2;
        body.length = (size_t)(after - 2 - body.text);
        if (!next_word(&body, &first))
            return CODEC_INVALID;
        /* "XPM" is XPM3; "XPM2 C" is XPM2 in C syntax. */
        if (!span_is(first, "XPM") &&
            !(span_is(first, "XPM2") && next_word(&body, &second) && span_is(second, "C")))
            return CODEC_INVALID;
        r->p = after;
        return CODEC_OK;
    }
    if (p < end && *p == '!') {
        const uint8_t *eol = memchr(p, '\n', (size_t)(end - p));
        struct span line;
        line.text = p + 1;
        line.length = (size_t)((eol != NULL ? eol : end) - line.text);
        if (!next_word(&line, &first) || !span_is(first, "XPM2") ||
            next_word(&line, &second))
            return CODEC_INVALID;
        r->p = eol != NULL ? eol + 1 : end;
        r->natural = 1;
        return CODEC_OK;
    }
    if (end - p >= 7 && memcmp(p, "#define", 7) == 0) {
        r->p = p;
        r->xpm1 = 1;
        return parse_defines(r, v);
    }
    return CODEC_INVALID;
}

static size_t hash_code(const uint8_t *code, unsigned cpp)
{
    uint32_t h = 2166136261u;
    unsigned i;
    for (i = 0; i < cpp; i++)
        h = (h ^ code[i]) * 16777619u;
    return h;
}

/* The slot holding code, or the empty slot where it belongs. */
static int32_t *find_slot(const struct color_table *t, const uint8_t *code)
{
    size_t i = hash_code(code, t->cpp) & t->mask;
    for (;;) {
        int32_t *slot = &t->slots[i];
        if (*slot < 0 || memcmp(t->codes[*slot], code, t->cpp) == 0)
            return slot;
        i = (i + 1) & t->mask;
    }
}

static void free_table(struct color_table *t)
{
    free(t->codes);
    free(t->rgba);
    free(t->slots);
}

static enum codec_result alloc_table(struct color_table *t, unsigned count, unsigned cpp)
{
    size_t slots = 16;
    while (slots < (size_t)count * 2u)
        slots *= 2;
    t->count = count;
    t->cpp = cpp;
    t->mask = slots - 1;
    t->codes = malloc(count * sizeof *t->codes);
    t->rgba = malloc((size_t)count * 4u);
    t->slots = malloc(slots * sizeof *t->slots);
    if (t->codes == NULL || t->rgba == NULL || t->slots == NULL)
        return CODEC_NO_MEMORY;
    memset(t->slots, 0xff, slots * sizeof *t->slots);
    return CODEC_OK;
}

enum color_key { KEY_C, KEY_G, KEY_G4, KEY_M, KEY_S, KEY_COUNT };

static int key_of(struct span word)
{
    static const char *const keys[KEY_COUNT] = { "c", "g", "g4", "m", "s" };
    int k;
    for (k = 0; k < KEY_COUNT; k++)
        if (span_is(word, keys[k]))
            return k;
    return -1;
}

/* The words after a colour code: key and value pairs, where a value may be
   several words and the word after a key is always a value. Colour displays
   use the first of c, g, g4 and m that parses; s names are not looked up. */
static enum codec_result parse_keys(struct span s, uint8_t rgba[4])
{
    struct span values[KEY_COUNT], word;
    int key = -1, expect_value = 0, k;

    memset(values, 0, sizeof values);
    while (next_word(&s, &word)) {
        if (!expect_value && (k = key_of(word)) >= 0) {
            key = k;
            expect_value = 1;
            values[key].text = NULL;
            values[key].length = 0;
            continue;
        }
        if (key < 0)
            return CODEC_INVALID;
        if (expect_value)
            values[key].text = word.text;
        values[key].length = (size_t)(word.text + word.length - values[key].text);
        expect_value = 0;
    }
    for (k = KEY_C; k <= KEY_M; k++)
        if (values[k].text != NULL && xpm_parse_color(values[k].text, values[k].length, rgba))
            return CODEC_OK;
    return CODEC_INVALID;
}

static enum codec_result read_colors(struct reader *r, struct color_table *t)
{
    unsigned i;

    for (i = 0; i < t->count; i++) {
        struct span s, value;
        enum codec_result result;
        int32_t *slot;

        if (!next_string(r, ARRAY_COLORS, &s))
            return CODEC_TRUNCATED;
        if (s.length < t->cpp)
            return CODEC_INVALID;
        t->codes[i] = s.text;
        if (r->xpm1) {
            /* A code string, then a string with only the colour. */
            if (!next_string(r, ARRAY_COLORS, &value))
                return CODEC_TRUNCATED;
            while (value.length > 0 && is_space(value.text[value.length - 1]))
                value.length--;
            while (value.length > 0 && is_space(*value.text)) {
                value.text++;
                value.length--;
            }
            result = xpm_parse_color(value.text, value.length, t->rgba + i * 4u)
                     ? CODEC_OK : CODEC_INVALID;
        } else {
            s.text += t->cpp;
            s.length -= t->cpp;
            result = parse_keys(s, t->rgba + i * 4u);
        }
        if (result != CODEC_OK)
            return result;
        /* A code defined twice takes its last colour. */
        slot = find_slot(t, t->codes[i]);
        *slot = (int32_t)i;
    }
    return CODEC_OK;
}

static enum codec_result read_pixels(struct reader *r, const struct color_table *t,
                                     struct xpm_image *image, int *transparent, int *opaque)
{
    unsigned x, y;
    uint8_t *out = image->rgba;

    for (y = 0; y < image->height; y++) {
        struct span s;
        const uint8_t *code;
        if (!next_string(r, ARRAY_PIXELS, &s))
            return CODEC_TRUNCATED;
        /* Longer rows are allowed; the extra is ignored, as libXpm does. */
        if (s.length / t->cpp < image->width)
            return CODEC_INVALID;
        code = s.text;
        for (x = 0; x < image->width; x++, code += t->cpp, out += 4) {
            int32_t index = *find_slot(t, code);
            if (index < 0)
                return CODEC_INVALID;
            memcpy(out, t->rgba + (size_t)index * 4u, 4);
            if (out[3] == 0)
                *transparent = 1;
            else
                *opaque = 1;
        }
    }
    return CODEC_OK;
}

void xpm_free(struct xpm_image *image)
{
    free(image->rgba);
    image->rgba = NULL;
    image->width = image->height = 0;
    image->hot_x = image->hot_y = -1;
    image->has_alpha = 0;
}

enum codec_result xpm_decode(const uint8_t *data, size_t length, struct xpm_image *image)
{
    struct values v = { 0, 0, 0, 0, XPM_HUGE, XPM_HUGE };
    struct color_table table;
    struct reader r;
    struct span s;
    enum codec_result result;
    int transparent = 0, opaque = 0;
    size_t i, pixels;

    if (image == NULL)
        return CODEC_INVALID;
    image->rgba = NULL;
    xpm_free(image);
    if (data == NULL)
        return CODEC_TRUNCATED;
    memset(&r, 0, sizeof r);
    memset(&table, 0, sizeof table);
    r.p = data;
    r.end = data + length;
    result = start(&r, &v);
    if (result != CODEC_OK)
        return result;
    if (!r.xpm1) {
        if (!next_string(&r, ARRAY_ANY, &s))
            return CODEC_TRUNCATED;
        result = parse_values(s, &v);
        if (result != CODEC_OK)
            return result;
    }
    if (v.width == 0 || v.height == 0 || v.ncolors == 0 || v.cpp == 0 || v.cpp > XPM_MAX_CPP)
        return CODEC_INVALID;
    if (v.width > 65535u || v.height > 65535u ||
        v.width * v.height > XPM_MAX_PIXELS || v.ncolors > XPM_MAX_PIXELS)
        return CODEC_TOO_LARGE;
    /* Every colour and pixel needs cpp bytes of the file, so a file too short
       to hold them is truncated; checking first keeps allocations in bounds. */
    if (v.ncolors > length / v.cpp || v.width * v.height > length / v.cpp)
        return CODEC_TRUNCATED;
    pixels = (size_t)v.width * v.height;

    result = alloc_table(&table, (unsigned)v.ncolors, (unsigned)v.cpp);
    if (result == CODEC_OK)
        result = read_colors(&r, &table);
    if (result == CODEC_OK) {
        image->rgba = malloc(pixels * 4u);
        if (image->rgba == NULL)
            result = CODEC_NO_MEMORY;
    }
    if (result == CODEC_OK) {
        image->width = (unsigned)v.width;
        image->height = (unsigned)v.height;
        result = read_pixels(&r, &table, image, &transparent, &opaque);
    }
    free_table(&table);
    if (result == CODEC_TRUNCATED || (result == CODEC_OK && r.truncated))
        result = CODEC_TRUNCATED;
    if (result != CODEC_OK) {
        xpm_free(image);
        return result;
    }
    /* An image with every pixel transparent is shown opaque. */
    if (!opaque)
        for (i = 0; i < pixels; i++)
            image->rgba[i * 4u + 3u] = 255;
    image->has_alpha = transparent && opaque;
    if (v.hot_x < v.width && v.hot_y < v.height) {
        image->hot_x = (long)v.hot_x;
        image->hot_y = (long)v.hot_y;
    }
    return CODEC_OK;
}
