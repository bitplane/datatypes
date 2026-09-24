#include "decode.h"
#include <stdlib.h>
#include <string.h>

#define XBM_MAX_PIXELS (16u * 1024u * 1024u)
/* Larger literals are reported as this, which no field accepts. */
#define XBM_HUGE 0x1000000ul

/* A small C lexer: enough for #define lines and one array initializer. */
enum token_kind { T_END, T_IDENT, T_NUMBER, T_PUNCT, T_BAD };

struct token {
    enum token_kind kind;
    int newline;             /* a line break came before this token */
    const uint8_t *text;
    size_t length;
    unsigned long value;     /* T_NUMBER, capped at XBM_HUGE */
};

struct lexer {
    const uint8_t *p, *end;
    int truncated;           /* a comment ran off the end */
};

static int is_alpha(int c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static int is_digit(int c) { return c >= '0' && c <= '9'; }

static int hex_value(int c)
{
    if (is_digit(c)) return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int skip_space(struct lexer *lx)
{
    int newline = 0;
    while (lx->p < lx->end) {
        uint8_t c = *lx->p;
        if (c == '\n') {
            newline = 1;
            lx->p++;
        } else if (c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v') {
            lx->p++;
        } else if (c == '\\' && lx->end - lx->p >= 2 && lx->p[1] == '\n') {
            lx->p += 2;
        } else if (c == '/' && lx->end - lx->p >= 2 && lx->p[1] == '*') {
            lx->p += 2;
            for (;;) {
                if (lx->end - lx->p < 2) {
                    lx->p = lx->end;
                    lx->truncated = 1;
                    return newline;
                }
                if (lx->p[0] == '*' && lx->p[1] == '/')
                    break;
                if (*lx->p == '\n')
                    newline = 1;
                lx->p++;
            }
            lx->p += 2;
        } else if (c == '/' && lx->end - lx->p >= 2 && lx->p[1] == '/') {
            while (lx->p < lx->end && *lx->p != '\n')
                lx->p++;
        } else {
            break;
        }
    }
    return newline;
}

static struct token lex(struct lexer *lx)
{
    struct token t;
    const uint8_t *p;

    memset(&t, 0, sizeof t);
    t.newline = skip_space(lx);
    p = t.text = lx->p;
    if (p == lx->end) {
        t.kind = T_END;
        return t;
    }
    if (is_alpha(*p)) {
        while (p < lx->end && (is_alpha(*p) || is_digit(*p)))
            p++;
        t.kind = T_IDENT;
    } else if (is_digit(*p)) {
        int base = 10, digits = 0, d;
        if (*p == '0' && lx->end - p >= 2 && (p[1] == 'x' || p[1] == 'X')) {
            base = 16;
            p += 2;
        }
        while (p < lx->end && (d = hex_value(*p)) >= 0 && d < base) {
            t.value = t.value * (unsigned long)base + (unsigned long)d;
            if (t.value > XBM_HUGE)
                t.value = XBM_HUGE;
            digits++;
            p++;
        }
        while (p < lx->end && (*p == 'u' || *p == 'U' || *p == 'l' || *p == 'L'))
            p++;
        t.kind = T_NUMBER;
        /* Not a number: X11 ships names such as 1x1_bits, so accept them. */
        if (digits == 0 || (p < lx->end && (is_alpha(*p) || is_digit(*p)))) {
            while (p < lx->end && (is_alpha(*p) || is_digit(*p)))
                p++;
            t.kind = T_IDENT;
        }
    } else {
        p++;
        t.kind = *t.text > ' ' && *t.text < 0x7f ? T_PUNCT : T_BAD;
    }
    t.length = (size_t)(p - t.text);
    lx->p = p;
    return t;
}

static int is_punct(struct token t, char c)
{
    return t.kind == T_PUNCT && *t.text == (uint8_t)c;
}

static int is_word(struct token t, const char *word)
{
    return t.kind == T_IDENT && strlen(word) == t.length &&
           memcmp(t.text, word, t.length) == 0;
}

/* The part of an identifier after its last underscore, as Xlib compares it. */
static int suffix_is(struct token t, const char *suffix)
{
    size_t start = t.length, n = strlen(suffix);
    while (start > 0 && t.text[start - 1] != '_')
        start--;
    return t.length - start == n && memcmp(t.text + start, suffix, n) == 0;
}

static int ends_with(struct token t, const char *tail)
{
    size_t n = strlen(tail);
    return t.length >= n && memcmp(t.text + t.length - n, tail, n) == 0;
}

/* Consume the rest of the current line. */
static void skip_line(struct lexer *lx)
{
    for (;;) {
        struct lexer save = *lx;
        struct token t = lex(lx);
        if (t.kind == T_END || t.newline) {
            *lx = save;
            return;
        }
    }
}

struct header {
    unsigned long width, height, hot_x, hot_y;
};

/* After '#': record width, height and hotspot defines, and skip anything else. */
static void directive(struct lexer *lx, struct header *h)
{
    struct lexer save = *lx;
    struct token name, value, t = lex(lx);

    if (t.newline || !is_word(t, "define")) {
        if (t.newline || t.kind == T_END)
            *lx = save;
        else
            skip_line(lx);
        return;
    }
    save = *lx;
    name = lex(lx);
    if (name.newline || name.kind != T_IDENT) {
        *lx = save;
        skip_line(lx);
        return;
    }
    save = *lx;
    value = lex(lx);
    if (value.newline || value.kind != T_NUMBER) {
        *lx = save;
        skip_line(lx);
        return;
    }
    if (suffix_is(name, "width"))
        h->width = value.value;
    else if (suffix_is(name, "height"))
        h->height = value.value;
    else if (ends_with(name, "x_hot"))
        h->hot_x = value.value;
    else if (ends_with(name, "y_hot"))
        h->hot_y = value.value;
    skip_line(lx);
}

/* Skip a braced initializer that is not the bitmap, after its '{'. */
static enum codec_result skip_braces(struct lexer *lx)
{
    unsigned depth = 1;
    while (depth != 0) {
        struct token t = lex(lx);
        if (t.kind == T_END)
            return CODEC_TRUNCATED;
        if (is_punct(t, '{'))
            depth++;
        else if (is_punct(t, '}'))
            depth--;
    }
    return CODEC_OK;
}

static enum codec_result read_bits(struct lexer *lx, unsigned unit_bits,
                                   struct xbm_image *image)
{
    unsigned width = image->width, height = image->height;
    size_t per_row = (width + unit_bits - 1u) / unit_bits;
    size_t units = per_row * height, i;
    unsigned long limit = unit_bits == 8 ? 0xfful : 0xfffful;

    for (i = 0; i < units; i++) {
        struct token t = lex(lx);
        unsigned long value;
        unsigned x0, b;
        uint8_t *row;
        int negative = 0;

        if (is_punct(t, '-')) {
            negative = 1;
            t = lex(lx);
        }
        if (t.kind == T_END || (!negative && is_punct(t, '}')))
            return CODEC_TRUNCATED;
        if (t.kind != T_NUMBER)
            return CODEC_INVALID;
        value = t.value;
        /* Signed char and short arrays hold the same bits as negative values. */
        if (negative && value != 0) {
            if (value > (limit >> 1) + 1u)
                return CODEC_INVALID;
            value = limit + 1u - value;
        }
        if (value > limit)
            return CODEC_INVALID;
        row = image->pixels + (i / per_row) * width;
        x0 = (unsigned)(i % per_row) * unit_bits;
        for (b = 0; b < unit_bits && x0 + b < width; b++)
            row[x0 + b] = (uint8_t)((value >> b) & 1u);
        if (i + 1 < units) {
            t = lex(lx);
            if (t.kind == T_END || is_punct(t, '}'))
                return CODEC_TRUNCATED;
            if (!is_punct(t, ','))
                return CODEC_INVALID;
        }
    }
    return CODEC_OK;
}

void xbm_free(struct xbm_image *image)
{
    free(image->pixels);
    image->pixels = NULL;
    image->width = image->height = 0;
    image->hot_x = image->hot_y = -1;
}

enum codec_result xbm_decode(const uint8_t *data, size_t length, struct xbm_image *image)
{
    struct header h = { 0, 0, XBM_HUGE, XBM_HUGE };
    struct lexer lx;
    struct token name;
    unsigned unit_bits = 0;
    enum codec_result result;

    if (image == NULL)
        return CODEC_INVALID;
    image->width = image->height = 0;
    image->hot_x = image->hot_y = -1;
    image->pixels = NULL;
    if (data == NULL)
        return CODEC_TRUNCATED;
    lx.p = data;
    lx.end = data + length;
    lx.truncated = 0;
    memset(&name, 0, sizeof name);

    /* Statements are scanned loosely, like Xlib: remember the element type
       and the last name, and treat "<type> <name>_bits[] = {" as the bitmap. */
    for (;;) {
        struct token t = lex(&lx);
        if (t.kind == T_END)
            return lx.truncated || (h.width != 0 && h.height != 0)
                   ? CODEC_TRUNCATED : CODEC_INVALID;
        if (t.kind == T_BAD)
            return CODEC_INVALID;
        if (is_punct(t, '#')) {
            directive(&lx, &h);
        } else if (is_punct(t, ';')) {
            unit_bits = 0;
            name.kind = T_END;
        } else if (is_word(t, "char")) {
            unit_bits = 8;
        } else if (is_word(t, "short")) {
            unit_bits = 16;
        } else if (t.kind == T_IDENT && !is_word(t, "static") &&
                   !is_word(t, "const") && !is_word(t, "unsigned") &&
                   !is_word(t, "signed")) {
            name = t;
        } else if (is_punct(t, '{')) {
            if (unit_bits != 0 && name.kind == T_IDENT && suffix_is(name, "bits"))
                break;
            result = skip_braces(&lx);
            if (result != CODEC_OK)
                return result;
        }
    }

    if (h.width == 0 || h.height == 0)
        return CODEC_INVALID;
    if (h.width > 65535u || h.height > 65535u ||
        (size_t)h.width * h.height > XBM_MAX_PIXELS)
        return CODEC_TOO_LARGE;
    image->pixels = calloc((size_t)h.width * h.height, 1);
    if (image->pixels == NULL)
        return CODEC_NO_MEMORY;
    image->width = (unsigned)h.width;
    image->height = (unsigned)h.height;
    result = read_bits(&lx, unit_bits, image);
    if (result != CODEC_OK) {
        xbm_free(image);
        return result;
    }
    if (h.hot_x < h.width && h.hot_y < h.height) {
        image->hot_x = (long)h.hot_x;
        image->hot_y = (long)h.hot_y;
    }
    return CODEC_OK;
}
