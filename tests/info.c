#include "../formats/info/decode.h"
#include "../formats/info/encode.h"
#include "common/zlib.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- building icons ---- */

struct buf { uint8_t data[1 << 17]; size_t n; };

static void u8(struct buf *b, unsigned v) { b->data[b->n++] = (uint8_t)v; }
static void u16(struct buf *b, unsigned v) { u8(b, v >> 8); u8(b, v); }
static void u32(struct buf *b, uint32_t v) { u16(b, v >> 16); u16(b, v & 0xffff); }
static void bytes(struct buf *b, const void *p, size_t n) { memcpy(b->data + b->n, p, n); b->n += n; }
static void put32_at(struct buf *b, size_t at, uint32_t v)
{
    b->data[at] = (uint8_t)(v >> 24);
    b->data[at + 1] = (uint8_t)(v >> 16);
    b->data[at + 2] = (uint8_t)(v >> 8);
    b->data[at + 3] = (uint8_t)v;
}

struct icon_spec {
    unsigned flags;          /* gadget flags */
    int normal, selected;    /* GadgetRender and SelectRender set */
    unsigned revision;
    int drawer;
    const char *tool;
    const char *const *tooltypes; /* NULL-terminated */
    const char *window;
};

static void diskobject(struct buf *b, const struct icon_spec *s)
{
    size_t start = b->n;
    memset(b->data + start, 0, 78);
    b->n += 78;
    b->data[start] = 0xe3;
    b->data[start + 1] = 0x10;
    b->data[start + 3] = 1;
    b->data[start + 16] = (uint8_t)(s->flags >> 8);
    b->data[start + 17] = (uint8_t)s->flags;
    put32_at(b, start + 22, s->normal ? 0x1234 : 0);
    put32_at(b, start + 26, s->selected ? 0x1234 : 0);
    put32_at(b, start + 44, s->revision);
    b->data[start + 48] = s->drawer ? 2 : 4;
    put32_at(b, start + 50, s->tool ? 1 : 0);
    put32_at(b, start + 54, s->tooltypes ? 1 : 0);
    put32_at(b, start + 66, s->drawer ? 1 : 0);
    put32_at(b, start + 70, s->window ? 1 : 0);
    if (s->drawer) {
        memset(b->data + b->n, 0x11, 56);
        b->n += 56;
    }
}

static void string(struct buf *b, const char *s)
{
    u32(b, (uint32_t)strlen(s) + 1);
    bytes(b, s, strlen(s) + 1);
}

/* The strings after the images, then DrawerData2 at revision 1. */
static void strings(struct buf *b, const struct icon_spec *s)
{
    unsigned n = 0, i;
    if (s->tool)
        string(b, s->tool);
    if (s->tooltypes) {
        while (s->tooltypes[n])
            n++;
        u32(b, (n + 1) * 4);
        for (i = 0; i < n; i++)
            string(b, s->tooltypes[i]);
    }
    if (s->window)
        string(b, s->window);
    if (s->drawer && s->revision == 1) {
        u32(b, 0);
        u16(b, 0);
    }
}

/* A planar image from pens, one hex digit each. */
static unsigned hexdigit(char c) { return c <= '9' ? (unsigned)(c - '0') : (unsigned)(c - 'a' + 10); }

static void planar(struct buf *b, unsigned w, unsigned h, unsigned depth, unsigned pick,
                   unsigned on_off, const char *pens)
{
    unsigned row = ((w + 15) >> 4) * 2, p, x, y;
    u16(b, 0); u16(b, 0); u16(b, w); u16(b, h); u16(b, depth);
    u32(b, 1);
    u8(b, pick); u8(b, on_off);
    u32(b, 0);
    for (p = 0; p < depth; p++)
        for (y = 0; y < h; y++) {
            uint8_t line[64] = {0};
            for (x = 0; x < w; x++)
                if (hexdigit(pens[y * w + x]) & 1u << p)
                    line[x / 8] |= (uint8_t)(0x80 >> (x & 7));
            bytes(b, line, row);
        }
}

/* ---- NewIcons text ---- */

struct ni { char text[4096]; size_t n; uint32_t bits; unsigned have; };

static void ni_start(struct ni *s, const char *prefix)
{
    strcpy(s->text, prefix);
    s->n = strlen(prefix);
    s->bits = 0;
    s->have = 0;
}

static void ni_char(struct ni *s, unsigned v)
{
    s->text[s->n++] = (char)(v < 0x50 ? v + 0x20 : v + 0x51);
    s->text[s->n] = 0;
}

static void ni_put(struct ni *s, unsigned value, unsigned bits)
{
    s->bits = s->bits << bits | value;
    s->have += bits;
    while (s->have >= 7) {
        s->have -= 7;
        ni_char(s, s->bits >> s->have & 0x7f);
    }
}

/* End a line: flush leftover bits, padded with zeros. */
static const char *ni_end(struct ni *s)
{
    if (s->have)
        ni_char(s, s->bits << (7 - s->have) & 0x7f);
    s->have = 0;
    return s->text;
}

static char ni_lines[8][4096];

/* IM1 or IM2 lines for an image: header and palette, then all pixels. */
static void newicon(int which, int transparent, unsigned w, unsigned h,
                    const uint8_t (*palette)[3], unsigned colours, const uint8_t *pens,
                    const char **lines)
{
    struct ni s;
    char head[16];
    unsigned i, bits = 1;

    snprintf(head, sizeof head, "IM%d=%c%c%c%c%c", which, transparent ? 'B' : 'C',
             0x21 + w, 0x21 + h, 0x21 + (colours >> 6), 0x21 + (colours & 63));
    ni_start(&s, head);
    for (i = 0; i < colours; i++) {
        ni_put(&s, palette[i][0], 8);
        ni_put(&s, palette[i][1], 8);
        ni_put(&s, palette[i][2], 8);
    }
    strcpy(ni_lines[which * 2 - 2], ni_end(&s));
    while ((1u << bits) < colours)
        bits++;
    snprintf(head, sizeof head, "IM%d=", which);
    ni_start(&s, head);
    for (i = 0; i < w * h; i++)
        ni_put(&s, pens[i], bits);
    strcpy(ni_lines[which * 2 - 1], ni_end(&s));
    lines[0] = ni_lines[which * 2 - 2];
    lines[1] = ni_lines[which * 2 - 1];
}

/* ---- FORM ICON ---- */

static size_t form_start(struct buf *b)
{
    size_t at = b->n;
    bytes(b, "FORM\0\0\0\0ICON", 12);
    return at;
}

static void form_end(struct buf *b, size_t at) { put32_at(b, at + 4, (uint32_t)(b->n - at - 8)); }

static void chunk(struct buf *b, const char *id, const void *body, size_t n)
{
    bytes(b, id, 4);
    u32(b, (uint32_t)n);
    bytes(b, body, n);
    if (n & 1)
        u8(b, 0);
}

static void face(struct buf *b, unsigned w, unsigned h)
{
    uint8_t f[6] = {(uint8_t)(w - 1), (uint8_t)(h - 1), 0, 0x11, 0, 0};
    chunk(b, "FACE", f, 6);
}

/* An IMAG chunk with raw pixels (format 0) or given packed data (format 1). */
static void imag(struct buf *b, unsigned transparent, unsigned colours, unsigned flags,
                 unsigned format, unsigned palette_format, unsigned depth,
                 const uint8_t *image, size_t image_bytes,
                 const uint8_t *palette, size_t palette_bytes)
{
    uint8_t body[4096];
    size_t n = 10;
    body[0] = (uint8_t)transparent;
    body[1] = (uint8_t)(colours - 1);
    body[2] = (uint8_t)flags;
    body[3] = (uint8_t)format;
    body[4] = (uint8_t)palette_format;
    body[5] = (uint8_t)depth;
    body[6] = (uint8_t)((image_bytes - 1) >> 8);
    body[7] = (uint8_t)(image_bytes - 1);
    body[8] = (uint8_t)(palette_bytes ? (palette_bytes - 1) >> 8 : 0);
    body[9] = (uint8_t)(palette_bytes ? palette_bytes - 1 : 0);
    memcpy(body + n, image, image_bytes);
    n += image_bytes;
    if (palette_bytes) {
        memcpy(body + n, palette, palette_bytes);
        n += palette_bytes;
    }
    chunk(b, "IMAG", body, n);
}

static void argb(struct buf *b, const uint8_t *pixels, size_t count)
{
    uint8_t body[8192];
    size_t packed;
    assert(zlib_deflate(pixels, count * 4, body + 10, sizeof body - 10, 9, &packed) == CODEC_OK);
    memset(body, 0, 10);
    body[3] = 1;
    body[4] = (uint8_t)(packed >> 24);
    body[5] = (uint8_t)(packed >> 16);
    body[6] = (uint8_t)(packed >> 8);
    body[7] = (uint8_t)packed;
    chunk(b, "ARGB", body, 10 + packed);
}

/* ---- checking ---- */

static void parse_ok(const struct buf *b, struct info_icon *icon, unsigned count)
{
    assert(info_parse(b->data, b->n, icon) == CODEC_OK);
    assert(icon->count == count);
}

static void decode_ok(const struct buf *b, const struct info_icon *icon, unsigned index,
                      struct info_image *image, unsigned w, unsigned h)
{
    assert(info_decode(b->data, b->n, icon, index, image) == CODEC_OK);
    assert(image->width == w && image->height == h && image->rgba != NULL);
}

static void pixel(const struct info_image *image, unsigned x, unsigned y,
                  unsigned r, unsigned g, unsigned bl, unsigned a)
{
    const uint8_t *p = image->rgba + ((size_t)y * image->width + x) * 4;
    if (p[0] != r || p[1] != g || p[2] != bl || p[3] != a) {
        fprintf(stderr, "pixel %u,%u is %02x%02x%02x%02x, want %02x%02x%02x%02x\n",
                x, y, p[0], p[1], p[2], p[3], r, g, bl, a);
        assert(0);
    }
}

/* Every cut before the end is a truncation, except cuts in trailing data
   at or after keep, where the icon ends without it. */
static void prefixes(const struct buf *b, size_t keep)
{
    struct info_icon icon;
    size_t n;
    for (n = 0; n < b->n; n++) {
        enum codec_result r = info_parse(b->data, n, &icon);
        if (n == keep)
            assert(r == CODEC_OK);
        else
            assert(r == CODEC_TRUNCATED);
    }
}

/* ---- tests ---- */

static const char pens4[] = "0123" "3210";

static void test_planar(void)
{
    struct icon_spec s = {0x0004, 1, 0, 1, 0, NULL, NULL, NULL};
    struct buf *b = calloc(1, sizeof *b);
    struct info_icon icon;
    struct info_image image;

    /* OS 2 pens, plane 0 as the low bit. */
    diskobject(b, &s);
    planar(b, 4, 2, 2, 3, 0, pens4);
    strings(b, &s);
    parse_ok(b, &icon, 1);
    assert(icon.entries[0].kind == INFO_PLANAR && !icon.entries[0].selected);
    decode_ok(b, &icon, 0, &image, 4, 2);
    pixel(&image, 0, 0, 0xaa, 0xaa, 0xaa, 0xff);
    pixel(&image, 1, 0, 0x00, 0x00, 0x00, 0xff);
    pixel(&image, 2, 0, 0xff, 0xff, 0xff, 0xff);
    pixel(&image, 3, 0, 0x66, 0x88, 0xbb, 0xff);
    pixel(&image, 0, 1, 0x66, 0x88, 0xbb, 0xff);
    info_free(&image);
    assert(info_decode(b->data, b->n, &icon, 1, &image) == CODEC_INVALID);
    assert(image.rgba == NULL);

    /* Revision 0: OS 1.3 pens. */
    b->n = 0;
    s.revision = 0;
    diskobject(b, &s);
    planar(b, 4, 2, 2, 3, 0, pens4);
    parse_ok(b, &icon, 1);
    decode_ok(b, &icon, 0, &image, 4, 2);
    pixel(&image, 0, 0, 0x00, 0x55, 0xaa, 0xff);
    pixel(&image, 1, 0, 0xff, 0xff, 0xff, 0xff);
    pixel(&image, 2, 0, 0x00, 0x00, 0x20, 0xff);
    pixel(&image, 3, 0, 0xff, 0x8a, 0x00, 0xff);
    info_free(&image);

    /* Three planes: MagicWB, at either revision. */
    b->n = 0;
    diskobject(b, &s);
    planar(b, 8, 1, 3, 7, 0, "01234567");
    parse_ok(b, &icon, 1);
    decode_ok(b, &icon, 0, &image, 8, 1);
    pixel(&image, 0, 0, 0x95, 0x95, 0x95, 0xff);
    pixel(&image, 3, 0, 0x3b, 0x67, 0xa2, 0xff);
    pixel(&image, 7, 0, 0xff, 0xa9, 0x97, 0xff);
    info_free(&image);

    /* One plane, and a width over 16 so rows take two words. */
    b->n = 0;
    s.revision = 1;
    diskobject(b, &s);
    planar(b, 18, 1, 1, 1, 0, "100000000000000001");
    parse_ok(b, &icon, 1);
    decode_ok(b, &icon, 0, &image, 18, 1);
    pixel(&image, 0, 0, 0, 0, 0, 0xff);
    pixel(&image, 1, 0, 0xaa, 0xaa, 0xaa, 0xff);
    pixel(&image, 17, 0, 0, 0, 0, 0xff);
    info_free(&image);

    /* PlanePick 0 draws PlaneOnOff everywhere; picked planes land on their bit. */
    b->n = 0;
    diskobject(b, &s);
    planar(b, 2, 1, 1, 0, 3, "10");
    parse_ok(b, &icon, 1);
    decode_ok(b, &icon, 0, &image, 2, 1);
    pixel(&image, 0, 0, 0x66, 0x88, 0xbb, 0xff);
    pixel(&image, 1, 0, 0x66, 0x88, 0xbb, 0xff);
    info_free(&image);
    b->n = 0;
    diskobject(b, &s);
    planar(b, 2, 1, 1, 2, 1, "10");
    parse_ok(b, &icon, 1);
    decode_ok(b, &icon, 0, &image, 2, 1);
    pixel(&image, 0, 0, 0x66, 0x88, 0xbb, 0xff);   /* plane on bit 1, bit 0 set */
    pixel(&image, 1, 0, 0x00, 0x00, 0x00, 0xff);
    info_free(&image);

    /* Two planes put on bits 1 and 2 reach MagicWB's pens. */
    b->n = 0;
    diskobject(b, &s);
    planar(b, 2, 1, 2, 6, 0, "21");
    parse_ok(b, &icon, 1);
    decode_ok(b, &icon, 0, &image, 2, 1);
    pixel(&image, 0, 0, 0x7b, 0x7b, 0x7b, 0xff);   /* pen 4 */
    pixel(&image, 1, 0, 0xff, 0xff, 0xff, 0xff);   /* pen 2 */
    info_free(&image);

    /* Deeper images get AROS's default screen of their depth: the first four
       pens, the last four, the pointer's 17-19 and black elsewhere. */
    b->n = 0;
    diskobject(b, &s);
    planar(b, 5, 1, 4, 15, 0, "0c5f3");
    parse_ok(b, &icon, 1);
    decode_ok(b, &icon, 0, &image, 5, 1);
    pixel(&image, 0, 0, 0xaa, 0xaa, 0xaa, 0xff);
    pixel(&image, 1, 0, 0xee, 0x44, 0x44, 0xff);   /* pen 12 */
    pixel(&image, 2, 0, 0x00, 0x00, 0x00, 0xff);   /* pen 5 */
    pixel(&image, 3, 0, 0xee, 0x99, 0x00, 0xff);   /* pen 15 */
    pixel(&image, 4, 0, 0x66, 0x88, 0xbb, 0xff);
    info_free(&image);
    /* One plane on bit 0 over PlaneOnOff 0xfc: pens 252 and 253 of 256. */
    b->n = 0;
    diskobject(b, &s);
    planar(b, 2, 1, 1, 1, 0xfc, "01");
    parse_ok(b, &icon, 1);
    decode_ok(b, &icon, 0, &image, 2, 1);
    pixel(&image, 0, 0, 0xee, 0x44, 0x44, 0xff);
    pixel(&image, 1, 0, 0x55, 0xdd, 0x55, 0xff);
    info_free(&image);
    /* Five planes reach the pointer's pens; nine planes use only eight. */
    b->n = 0;
    diskobject(b, &s);
    planar(b, 1, 1, 1, 1, 0x10, "1");
    parse_ok(b, &icon, 1);
    decode_ok(b, &icon, 0, &image, 1, 1);
    pixel(&image, 0, 0, 0xbb, 0x00, 0x00, 0xff);   /* pen 17 */
    info_free(&image);
    b->n = 0;
    diskobject(b, &s);
    planar(b, 1, 1, 9, 0xff, 0, "1");
    parse_ok(b, &icon, 1);
    decode_ok(b, &icon, 0, &image, 1, 1);
    pixel(&image, 0, 0, 0x00, 0x00, 0x00, 0xff);
    info_free(&image);

    /* Empty images are skipped. */
    b->n = 0;
    diskobject(b, &s);
    planar(b, 0, 0, 2, 3, 0, "");
    assert(info_parse(b->data, b->n, &icon) == CODEC_INVALID);

    /* A negative size is invalid. */
    b->n = 0;
    diskobject(b, &s);
    planar(b, 2, 1, 1, 1, 0, "01");
    b->data[78 + 4] = 0xff;
    assert(info_parse(b->data, b->n, &icon) == CODEC_INVALID);
    free(b);
}

static void test_selected_and_strings(void)
{
    static const char *const tts[] = {"FILETYPE=text", "DONOTWAIT", "", NULL};
    struct icon_spec s = {0x0006, 1, 0, 1, 1, "SYS:Utilities/MultiView", tts, "CON:0/0/640/200"};
    struct buf *b = calloc(1, sizeof *b);
    struct info_icon icon;
    struct info_image image;
    size_t end;

    /* GADGHIMAGE implies a selected image even with a NULL SelectRender. */
    diskobject(b, &s);
    planar(b, 4, 2, 2, 3, 0, pens4);
    planar(b, 4, 2, 2, 3, 0, "3333" "0000");
    strings(b, &s);
    end = b->n;
    parse_ok(b, &icon, 2);
    assert(icon.entries[1].selected);
    decode_ok(b, &icon, 1, &image, 4, 2);
    pixel(&image, 0, 0, 0x66, 0x88, 0xbb, 0xff);
    pixel(&image, 0, 1, 0xaa, 0xaa, 0xaa, 0xff);
    info_free(&image);
    assert(info_best(&icon) == 0);
    prefixes(b, end);

    /* Without the flag, SelectRender says so. */
    b->n = 0;
    s.flags = 0x0004;
    s.selected = 1;
    diskobject(b, &s);
    planar(b, 4, 2, 2, 3, 0, pens4);
    planar(b, 4, 2, 2, 3, 0, pens4);
    strings(b, &s);
    parse_ok(b, &icon, 2);

    /* Trailing data that isn't a FORM ICON is ignored. */
    bytes(b, "junk", 4);
    parse_ok(b, &icon, 2);

    /* A string or tooltype array longer than the file is a truncation. */
    b->n = 0;
    s.selected = 0;
    s.tooltypes = NULL;
    s.window = NULL;
    diskobject(b, &s);
    planar(b, 4, 2, 2, 3, 0, pens4);
    u32(b, 0xfffffff0u);
    assert(info_parse(b->data, b->n, &icon) == CODEC_TRUNCATED);
    b->n = 0;
    s.tool = NULL;
    s.tooltypes = tts;
    diskobject(b, &s);
    planar(b, 4, 2, 2, 3, 0, pens4);
    u32(b, 0xfffffff0u);
    assert(info_parse(b->data, b->n, &icon) == CODEC_TRUNCATED);
    /* A tooltype array size under 4 holds no strings. */
    b->n = 0;
    s.drawer = 0;
    diskobject(b, &s);
    planar(b, 4, 2, 2, 3, 0, pens4);
    u32(b, 2);
    parse_ok(b, &icon, 1);

    /* Not an icon. */
    b->n = 0;
    diskobject(b, &s);
    b->data[0] = 0xf3;
    assert(info_parse(b->data, b->n, &icon) == CODEC_INVALID);
    assert(info_parse((const uint8_t *)"This is ld.info", 15, &icon) == CODEC_INVALID);
    assert(info_parse(b->data, 0, &icon) == CODEC_TRUNCATED);
    free(b);
}

static const uint8_t ni_palette[5][3] = {
    {0, 0, 0}, {255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {17, 34, 51}
};

static void test_newicons(void)
{
    static const uint8_t pens1[6] = {0, 1, 2, 3, 4, 0};
    static const uint8_t pens2[6] = {4, 4, 4, 1, 1, 1};
    const char *im1[2], *im2[2];
    const char *tts[8];
    struct icon_spec s = {0x0004, 1, 0, 1, 0, NULL, tts, NULL};
    struct buf *b = calloc(1, sizeof *b);
    struct info_icon icon;
    struct info_image image;

    newicon(1, 1, 3, 2, ni_palette, 5, pens1, im1);
    newicon(2, 0, 3, 2, ni_palette, 5, pens2, im2);
    tts[0] = "FILETYPE=text";
    tts[1] = "*** DON'T EDIT THE FOLLOWING LINES!! ***";
    tts[2] = im1[0];
    tts[3] = im1[1];
    tts[4] = im2[0];
    tts[5] = im2[1];
    tts[6] = NULL;
    diskobject(b, &s);
    planar(b, 4, 2, 2, 3, 0, pens4);
    strings(b, &s);
    parse_ok(b, &icon, 3);
    assert(icon.entries[1].kind == INFO_NEWICON && !icon.entries[1].selected);
    assert(icon.entries[2].kind == INFO_NEWICON && icon.entries[2].selected);
    assert(info_best(&icon) == 1);
    decode_ok(b, &icon, 1, &image, 3, 2);
    pixel(&image, 0, 0, 0, 0, 0, 0);        /* 'B': pen 0 is transparent */
    pixel(&image, 1, 0, 255, 0, 0, 0xff);
    pixel(&image, 2, 0, 0, 255, 0, 0xff);
    pixel(&image, 0, 1, 0, 0, 255, 0xff);
    pixel(&image, 1, 1, 17, 34, 51, 0xff);
    pixel(&image, 2, 1, 0, 0, 0, 0);
    info_free(&image);
    decode_ok(b, &icon, 2, &image, 3, 2);
    pixel(&image, 0, 0, 17, 34, 51, 0xff);  /* 'C': opaque */
    pixel(&image, 0, 1, 255, 0, 0, 0xff);
    info_free(&image);
    prefixes(b, (size_t)-1);

    /* IM2 before IM1 keeps file order. */
    tts[2] = im2[0];
    tts[3] = im2[1];
    tts[4] = im1[0];
    tts[5] = im1[1];
    b->n = 0;
    diskobject(b, &s);
    planar(b, 4, 2, 2, 3, 0, pens4);
    strings(b, &s);
    parse_ok(b, &icon, 3);
    assert(icon.entries[1].selected && !icon.entries[2].selected);
    assert(info_best(&icon) == 2);

    /* Without the marker line, IM1= is only a tooltype. */
    tts[1] = "DONOTWAIT";
    b->n = 0;
    diskobject(b, &s);
    planar(b, 4, 2, 2, 3, 0, pens4);
    strings(b, &s);
    parse_ok(b, &icon, 1);
    tts[1] = "*** DON'T EDIT THE FOLLOWING LINES!! ***";

    /* The pixel lines are missing: the image can't be decoded. */
    tts[2] = im1[0];
    tts[3] = "OTHER";
    tts[4] = NULL;
    b->n = 0;
    diskobject(b, &s);
    planar(b, 4, 2, 2, 3, 0, pens4);
    strings(b, &s);
    parse_ok(b, &icon, 2);
    assert(info_decode(b->data, b->n, &icon, 1, &image) == CODEC_INVALID);
    assert(image.rgba == NULL);

    /* Bad headers: zero width, zero or too many colours, unknown transparency. */
    {
        static const char *bad[] = {"IM1=B!\"!&", "IM1=B\"\"!!", "IM1=B\"\"%\"", "IM1=X\"\"!&", "IM1=B\"\"!"};
        unsigned i;
        for (i = 0; i < sizeof bad / sizeof *bad; i++) {
            tts[2] = bad[i];
            tts[3] = NULL;
            b->n = 0;
            diskobject(b, &s);
            planar(b, 4, 2, 2, 3, 0, pens4);
            strings(b, &s);
            assert(info_parse(b->data, b->n, &icon) == CODEC_INVALID);
        }
    }

    /* Leftover bits end with the line; 0xd1 and up are runs of zero groups;
       a control character is invalid. One colour, one bit per pixel. */
    {
        static char head[] = "IM1=C#\"!\"\x9f\x9f\x9f\x9f";   /* 2x1, 1 colour: white */
        static char pix1[] = "IM1=\xd1";                      /* 7 zero bits */
        static char bad1[] = "IM1=\x10";
        static const uint8_t white[1][3] = {{255, 255, 255}};
        (void)white;
        tts[2] = head;
        tts[3] = pix1;
        tts[4] = NULL;
        b->n = 0;
        diskobject(b, &s);
        planar(b, 4, 2, 2, 3, 0, pens4);
        strings(b, &s);
        parse_ok(b, &icon, 2);
        decode_ok(b, &icon, 1, &image, 2, 1);
        pixel(&image, 0, 0, 0xff, 0xff, 0xff, 0xff);
        pixel(&image, 1, 0, 0xff, 0xff, 0xff, 0xff);
        info_free(&image);
        tts[3] = bad1;
        b->n = 0;
        diskobject(b, &s);
        planar(b, 4, 2, 2, 3, 0, pens4);
        strings(b, &s);
        parse_ok(b, &icon, 2);
        assert(info_decode(b->data, b->n, &icon, 1, &image) == CODEC_INVALID);
    }

    /* Pixel data split over lines, each line's spare bits dropped. */
    {
        static const uint8_t pens[9] = {1, 2, 3, 4, 0, 1, 2, 3, 4};
        struct ni t;
        char a[64], c[64];
        unsigned i;
        newicon(1, 0, 3, 3, ni_palette, 5, pens, im1);
        ni_start(&t, "IM1=");
        for (i = 0; i < 4; i++)
            ni_put(&t, pens[i], 3);           /* 12 bits: two characters, 2 spare */
        strcpy(a, ni_end(&t));
        ni_start(&t, "IM1=");
        for (; i < 9; i++)
            ni_put(&t, pens[i], 3);
        strcpy(c, ni_end(&t));
        tts[2] = im1[0];
        tts[3] = a;
        tts[4] = c;
        tts[5] = NULL;
        b->n = 0;
        diskobject(b, &s);
        planar(b, 4, 2, 2, 3, 0, pens4);
        strings(b, &s);
        parse_ok(b, &icon, 2);
        decode_ok(b, &icon, 1, &image, 3, 3);
        for (i = 0; i < 9; i++)
            pixel(&image, i % 3, i / 3, ni_palette[pens[i]][0], ni_palette[pens[i]][1],
                  ni_palette[pens[i]][2], 0xff);
        info_free(&image);
    }

    /* Pens past the palette show black. */
    {
        static const uint8_t pens[2] = {2, 3};
        newicon(1, 0, 2, 1, ni_palette, 3, pens, im1);
        tts[2] = im1[0];
        tts[3] = im1[1];
        tts[4] = NULL;
        b->n = 0;
        diskobject(b, &s);
        planar(b, 4, 2, 2, 3, 0, pens4);
        strings(b, &s);
        parse_ok(b, &icon, 2);
        decode_ok(b, &icon, 1, &image, 2, 1);
        pixel(&image, 0, 0, 0, 255, 0, 0xff);
        pixel(&image, 1, 0, 0, 0, 0, 0xff);
        info_free(&image);
    }
    free(b);
}

/* A bit stream, most significant bit first. */
struct bw { uint8_t *out; size_t n; uint32_t acc; unsigned have; };

static void bw_put(struct bw *w, unsigned value, unsigned bits)
{
    w->acc = w->acc << bits | value;
    w->have += bits;
    while (w->have >= 8) {
        w->have -= 8;
        w->out[w->n++] = (uint8_t)(w->acc >> w->have);
    }
}

static size_t bw_end(struct bw *w)
{
    if (w->have)
        bw_put(w, 0, 8 - w->have);
    return w->n;
}

/* Pack values the OS 3.5 way, as one literal run. */
static size_t literal35(const uint8_t *v, size_t n, unsigned depth, uint8_t *out)
{
    struct bw w = {out, 0, 0, 0};
    size_t i;
    bw_put(&w, (unsigned)(n - 1), 8);
    for (i = 0; i < n; i++)
        bw_put(&w, v[i], depth);
    return bw_end(&w);
}

static void test_glowicons(void)
{
    static const uint8_t pal[3 * 3] = {1, 2, 3, 200, 100, 50, 9, 8, 7};
    static const uint8_t raw[6] = {0, 1, 2, 2, 1, 0};
    struct icon_spec s = {0x0004, 1, 0, 1, 1, NULL, NULL, NULL};
    struct buf *b = calloc(1, sizeof *b);
    struct info_icon icon;
    struct info_image image;
    uint8_t packed[64], packed_pal[64], px[6 * 4];
    size_t form, end, n, pn;
    unsigned i;

    /* A drawer: DrawerData, DrawerData2, then FACE, raw IMAG, packed IMAG
       without a palette, and an ARGB pair. */
    diskobject(b, &s);
    planar(b, 4, 2, 2, 3, 0, pens4);
    strings(b, &s);
    end = b->n;
    form = form_start(b);
    face(b, 3, 2);
    imag(b, 0, 3, 3, 0, 0, 8, raw, 6, pal, 9);
    /* A repeat of pen 2 (x3), then a literal 1, 1, 0 at depth 2. */
    {
        struct bw w = {packed, 0, 0, 0};
        bw_put(&w, 0xfe, 8);
        bw_put(&w, 2, 2);
        bw_put(&w, 2, 8);
        bw_put(&w, 1, 2);
        bw_put(&w, 1, 2);
        bw_put(&w, 0, 2);
        n = bw_end(&w);
    }
    imag(b, 1, 3, 1, 1, 0, 2, packed, n, NULL, 0);
    for (i = 0; i < 6; i++) {
        px[i * 4] = (uint8_t)(i * 40);          /* A */
        px[i * 4 + 1] = (uint8_t)i;             /* R */
        px[i * 4 + 2] = (uint8_t)(i + 10);      /* G */
        px[i * 4 + 3] = (uint8_t)(i + 20);      /* B */
    }
    argb(b, px, 6);
    argb(b, px, 6);
    form_end(b, form);
    parse_ok(b, &icon, 5);
    assert(icon.entries[1].kind == INFO_IMAG && !icon.entries[1].selected);
    assert(icon.entries[2].kind == INFO_IMAG && icon.entries[2].selected);
    assert(icon.entries[3].kind == INFO_ARGB && icon.entries[4].selected);
    assert(info_best(&icon) == 3);
    /* Raw IMAG with transparent pen 0. */
    decode_ok(b, &icon, 1, &image, 3, 2);
    pixel(&image, 0, 0, 1, 2, 3, 0);
    pixel(&image, 1, 0, 200, 100, 50, 0xff);
    pixel(&image, 2, 0, 9, 8, 7, 0xff);
    pixel(&image, 2, 1, 1, 2, 3, 0);
    info_free(&image);
    /* Packed IMAG using the first palette, transparent pen 1. */
    decode_ok(b, &icon, 2, &image, 3, 2);
    pixel(&image, 0, 0, 9, 8, 7, 0xff);
    pixel(&image, 2, 0, 9, 8, 7, 0xff);
    pixel(&image, 0, 1, 200, 100, 50, 0);
    pixel(&image, 1, 1, 200, 100, 50, 0);
    pixel(&image, 2, 1, 1, 2, 3, 0xff);
    info_free(&image);
    decode_ok(b, &icon, 3, &image, 3, 2);
    for (i = 0; i < 6; i++)
        pixel(&image, i % 3, i / 3, i, i + 10, i + 20, i * 40);
    info_free(&image);
    prefixes(b, end);

    /* A packed palette, and pens past the palette in black. */
    b->n = 0;
    s.drawer = 0;
    diskobject(b, &s);
    planar(b, 4, 2, 2, 3, 0, pens4);
    form = form_start(b);
    face(b, 3, 2);
    {
        static const uint8_t pens[6] = {0, 1, 3, 3, 1, 0};
        n = literal35(pens, 6, 2, packed);
        pn = literal35(pal, 6, 8, packed_pal);
        imag(b, 0, 2, 2, 1, 1, 2, packed, n, packed_pal, pn);
    }
    form_end(b, form);
    parse_ok(b, &icon, 2);
    decode_ok(b, &icon, 1, &image, 3, 2);
    pixel(&image, 0, 0, 1, 2, 3, 0xff);         /* no transparency flag */
    pixel(&image, 1, 0, 200, 100, 50, 0xff);
    pixel(&image, 2, 0, 0, 0, 0, 0xff);
    info_free(&image);

    /* A packed stream that runs out is invalid. */
    {
        struct buf *c = calloc(1, sizeof *c);
        static const uint8_t pens[6] = {0, 1, 1, 1, 1, 0};
        diskobject(c, &s);
        planar(c, 4, 2, 2, 3, 0, pens4);
        form = form_start(c);
        face(c, 3, 2);
        n = literal35(pens, 4, 2, packed);
        imag(c, 0, 3, 2, 1, 0, 2, packed, n, pal, 9);
        form_end(c, form);
        parse_ok(c, &icon, 2);
        assert(info_decode(c->data, c->n, &icon, 1, &image) == CODEC_INVALID);
        assert(image.rgba == NULL);
        free(c);
    }

    /* A repeat past the last pixel is clamped. */
    {
        struct buf *c = calloc(1, sizeof *c);
        uint8_t over[2] = {0x81, 0x40};           /* pen 1 x128 at depth 2 */
        diskobject(c, &s);
        planar(c, 4, 2, 2, 3, 0, pens4);
        form = form_start(c);
        face(c, 3, 2);
        imag(c, 0, 3, 2, 1, 0, 2, over, 2, pal, 9);
        form_end(c, form);
        parse_ok(c, &icon, 2);
        decode_ok(c, &icon, 1, &image, 3, 2);
        pixel(&image, 2, 1, 200, 100, 50, 0xff);
        info_free(&image);
        free(c);
    }
    free(b);
}

static void form_case(const uint8_t *chunks, size_t n, enum codec_result want, unsigned count)
{
    struct icon_spec s = {0x0004, 1, 0, 1, 0, NULL, NULL, NULL};
    struct buf *b = calloc(1, sizeof *b);
    struct info_icon icon;
    size_t form;
    diskobject(b, &s);
    planar(b, 4, 2, 2, 3, 0, pens4);
    form = form_start(b);
    bytes(b, chunks, n);
    form_end(b, form);
    assert(info_parse(b->data, b->n, &icon) == want);
    if (want == CODEC_OK)
        assert(icon.count == count);
    free(b);
}

static void test_form_errors(void)
{
    static const uint8_t pal[9] = {0};
    static const uint8_t raw[4] = {0};
    struct buf *c = calloc(1, sizeof *c);
    size_t mark;

    /* IMAG before FACE is ignored, as is a third IMAG, and unknown chunks. */
    imag(c, 0, 3, 2, 0, 0, 8, raw, 4, pal, 9);
    face(c, 2, 2);
    chunk(c, "png ", "odd", 3);
    imag(c, 0, 3, 2, 0, 0, 8, raw, 4, pal, 9);
    imag(c, 0, 3, 2, 0, 0, 8, raw, 4, pal, 9);
    imag(c, 0, 3, 2, 0, 0, 8, raw, 4, pal, 9);
    form_case(c->data, c->n, CODEC_OK, 3);

    /* Reserved formats, bad depth, sizes past the chunk, short raw data,
       a short palette, no palette at all. */
#define BAD(...) do { c->n = 0; face(c, 2, 2); __VA_ARGS__; form_case(c->data, c->n, CODEC_INVALID, 0); } while (0)
    BAD(imag(c, 0, 3, 2, 2, 0, 8, raw, 4, pal, 9));
    BAD(imag(c, 0, 3, 2, 0, 2, 8, raw, 4, pal, 9));
    BAD(imag(c, 0, 3, 2, 1, 0, 0, raw, 4, pal, 9));
    BAD(imag(c, 0, 3, 2, 1, 0, 9, raw, 4, pal, 9));
    BAD(imag(c, 0, 3, 2, 0, 0, 8, raw, 3, pal, 9));
    BAD(imag(c, 0, 4, 2, 0, 0, 8, raw, 4, pal, 9));
    BAD(imag(c, 0, 3, 0, 0, 0, 8, raw, 4, NULL, 0));
    BAD(mark = c->n; imag(c, 0, 3, 2, 0, 0, 8, raw, 4, pal, 9); c->data[mark + 8 + 7] = 0xff);
    BAD(chunk(c, "IMAG", raw, 4));
    BAD(chunk(c, "ARGB", raw, 4));
    BAD(mark = c->n; argb(c, (const uint8_t *)"abcdefghijklmnop", 4); c->data[mark + 12] = 0x7f);
    BAD(mark = c->n; argb(c, (const uint8_t *)"abcdefghijklmnop", 4); memset(c->data + mark + 12, 0, 4));
#undef BAD

    /* A chunk past the FORM's end, or a partial chunk header. */
    c->n = 0;
    face(c, 2, 2);
    bytes(c, "IMAG\0\0\1\0", 8);
    form_case(c->data, c->n, CODEC_INVALID, 0);
    c->n = 0;
    face(c, 2, 2);
    bytes(c, "IMA", 3);
    form_case(c->data, c->n, CODEC_INVALID, 0);

    /* An ARGB stream that is corrupt, or holds the wrong number of pixels,
       parses but fails to decode. */
    {
        struct icon_spec s = {0x0004, 1, 0, 1, 0, NULL, NULL, NULL};
        struct buf *b = calloc(1, sizeof *b);
        struct info_icon icon;
        struct info_image image;
        size_t form;
        uint8_t px[16] = {0};
        diskobject(b, &s);
        planar(b, 4, 2, 2, 3, 0, pens4);
        form = form_start(b);
        face(b, 2, 2);
        argb(b, px, 3);
        mark = b->n;
        argb(b, px, 4);
        form_end(b, form);
        parse_ok(b, &icon, 3);
        assert(info_decode(b->data, b->n, &icon, 1, &image) == CODEC_INVALID);
        assert(info_decode(b->data, b->n, &icon, 2, &image) == CODEC_OK);
        info_free(&image);
        /* OS4 and MorphOS store the packed size less one. */
        b->data[mark + 8 + 7]--;
        parse_ok(b, &icon, 3);
        assert(info_decode(b->data, b->n, &icon, 2, &image) == CODEC_OK);
        info_free(&image);
        b->data[mark + 8 + 10 + 3] ^= 0x55;
        assert(info_decode(b->data, b->n, &icon, 2, &image) == CODEC_INVALID);
        assert(image.rgba == NULL);
        free(b);
    }

    /* The FORM's own size: too small, or past the file. */
    {
        struct icon_spec s = {0x0004, 1, 0, 1, 0, NULL, NULL, NULL};
        struct buf *b = calloc(1, sizeof *b);
        struct info_icon icon;
        diskobject(b, &s);
        planar(b, 4, 2, 2, 3, 0, pens4);
        bytes(b, "FORM\0\0\0\3ICON", 12);
        assert(info_parse(b->data, b->n, &icon) == CODEC_INVALID);
        put32_at(b, b->n - 8, 0x100);
        assert(info_parse(b->data, b->n, &icon) == CODEC_TRUNCATED);
        /* Another FORM type is only trailing data. */
        memcpy(b->data + b->n - 4, "ILBM", 4);
        parse_ok(b, &icon, 1);
        free(b);
    }
    free(c);
}

/* A revision 0 drawer may still carry DrawerData2 before its FORM ICON. */
static void test_revision0_drawer(void)
{
    struct icon_spec s = {0x0004, 1, 0, 0, 1, NULL, NULL, NULL};
    struct buf *b = calloc(1, sizeof *b);
    struct info_icon icon;
    uint8_t px[4] = {255, 1, 2, 3};
    size_t form;

    diskobject(b, &s);
    planar(b, 4, 2, 2, 3, 0, pens4);
    u32(b, 0);
    u16(b, 0);
    form = form_start(b);
    face(b, 1, 1);
    argb(b, px, 1);
    form_end(b, form);
    parse_ok(b, &icon, 2);
    /* Without them it is found in place. */
    memmove(b->data + form - 6, b->data + form, b->n - form);
    b->n -= 6;
    parse_ok(b, &icon, 2);
    free(b);
}

static void roundtrip(const uint8_t *rgba, unsigned w, unsigned h, enum info_kind want)
{
    size_t capacity = info_encode_capacity(w, h), size;
    uint8_t *out = malloc(capacity);
    struct info_icon icon;
    struct info_image image;
    unsigned best;

    assert(capacity != 0);
    size = info_encode(rgba, w, h, out, capacity);
    assert(size != 0 && size <= capacity);
    assert(info_encode(rgba, w, h, out, capacity - 1) == 0);
    assert(info_parse(out, size, &icon) == CODEC_OK);
    assert(icon.count == 2 && icon.entries[0].kind == INFO_PLANAR);
    best = info_best(&icon);
    assert(best == 1 && icon.entries[1].kind == want);
    assert(info_decode(out, size, &icon, best, &image) == CODEC_OK);
    assert(image.width == w && image.height == h);
    assert(memcmp(image.rgba, rgba, (size_t)w * h * 4) == 0);
    info_free(&image);
    assert(info_decode(out, size, &icon, 0, &image) == CODEC_OK);
    info_free(&image);
    free(out);
}

static void test_encode(void)
{
    static uint8_t rgba[256 * 256 * 4];
    struct info_icon icon;
    struct info_image image;
    size_t i, capacity, size;
    uint8_t *out;

    /* Few colours with on/off alpha: IMAG. Transparent colours don't survive. */
    for (i = 0; i < 20 * 10; i++) {
        uint8_t *p = rgba + i * 4;
        p[0] = (uint8_t)(i % 7 * 30);
        p[1] = (uint8_t)(i / 20 * 20);
        p[2] = 99;
        p[3] = i % 5 == 0 ? 0 : 255;
        if (p[3] == 0)
            p[0] = p[1] = p[2] = 0;
    }
    roundtrip(rgba, 20, 10, INFO_IMAG);
    /* One opaque colour; one pixel. */
    memset(rgba, 0x80, 4 * 4);
    rgba[3] = rgba[7] = rgba[11] = rgba[15] = 255;
    roundtrip(rgba, 2, 2, INFO_IMAG);
    roundtrip(rgba, 1, 1, INFO_IMAG);
    /* Only transparent pixels. */
    memset(rgba, 0, 3 * 4);
    roundtrip(rgba, 3, 1, INFO_IMAG);
    /* 256 opaque colours fit; 255 plus transparency fit; 256 plus it don't. */
    for (i = 0; i < 256; i++) {
        rgba[i * 4] = (uint8_t)i;
        rgba[i * 4 + 1] = rgba[i * 4 + 2] = 7;
        rgba[i * 4 + 3] = 255;
    }
    roundtrip(rgba, 16, 16, INFO_IMAG);
    memset(rgba, 0, 4);
    roundtrip(rgba, 16, 16, INFO_IMAG);
    for (i = 0; i < 256; i++)
        rgba[i * 4 + 1] = 9;
    memcpy(rgba + 256 * 4, "\x01\x02\x03\xff", 4);
    memset(rgba + 257 * 4, 0, 4);
    roundtrip(rgba, 129, 2, INFO_ARGB);
    /* Partial alpha: ARGB. */
    for (i = 0; i < 256 * 256; i++) {
        rgba[i * 4] = (uint8_t)i;
        rgba[i * 4 + 1] = (uint8_t)(i >> 8);
        rgba[i * 4 + 2] = (uint8_t)(i * 7);
        rgba[i * 4 + 3] = (uint8_t)(i * 13);
    }
    roundtrip(rgba, 256, 256, INFO_ARGB);
    roundtrip(rgba, 5, 3, INFO_ARGB);
    /* Long runs pack; a 256x256 of one colour. */
    for (i = 0; i < 256 * 256; i++)
        memcpy(rgba + i * 4, i < 1000 ? "\x10\x20\x30\xff" : "\x40\x50\x60\xff", 4);
    roundtrip(rgba, 256, 256, INFO_IMAG);

    /* Too big or empty. */
    assert(info_encode_capacity(257, 1) == 0);
    assert(info_encode_capacity(1, 257) == 0);
    assert(info_encode_capacity(0, 1) == 0);

    /* The old image: the four OS 2 pens, transparency as the background. */
    {
        static const uint8_t four[5 * 4] = {
            0xaa, 0xaa, 0xaa, 255, 0, 0, 0, 255, 255, 255, 255, 255, 0x66, 0x88, 0xbb, 255,
            250, 250, 250, 0
        };
        capacity = info_encode_capacity(5, 1);
        out = malloc(capacity);
        size = info_encode(four, 5, 1, out, capacity);
        assert(size != 0);
        assert(info_parse(out, size, &icon) == CODEC_OK);
        assert(info_decode(out, size, &icon, 0, &image) == CODEC_OK);
        pixel(&image, 0, 0, 0xaa, 0xaa, 0xaa, 255);
        pixel(&image, 1, 0, 0, 0, 0, 255);
        pixel(&image, 2, 0, 255, 255, 255, 255);
        pixel(&image, 3, 0, 0x66, 0x88, 0xbb, 255);
        pixel(&image, 4, 0, 0xaa, 0xaa, 0xaa, 255);
        info_free(&image);
        free(out);
    }
}

static void test_bad_index(void)
{
    struct icon_spec s = {0x0004, 1, 0, 1, 0, NULL, NULL, NULL};
    struct buf *b = calloc(1, sizeof *b);
    struct info_icon icon;
    struct info_image image;
    diskobject(b, &s);
    planar(b, 4, 2, 2, 3, 0, pens4);
    parse_ok(b, &icon, 1);
    assert(info_decode(b->data, b->n, &icon, 1, &image) == CODEC_INVALID);
    assert(info_decode(b->data, b->n, &icon, 0xffffffffu, &image) == CODEC_INVALID);
    free(b);
}

int main(void)
{
    test_planar();
    test_selected_and_strings();
    test_newicons();
    test_glowicons();
    test_form_errors();
    test_revision0_drawer();
    test_encode();
    test_bad_index();
    puts("info: ok");
    return 0;
}
