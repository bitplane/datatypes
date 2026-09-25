#include "../formats/stpaint/decode.h"
#include "../formats/stpaint/encode.h"
#include "common/atarist.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t file[300000];
static size_t len;
static uint8_t screen[32000];
static uint8_t stream[32000];
static uint32_t seed = 1;

static unsigned rnd(void)
{
    seed = seed * 1103515245u + 12345u;
    return (seed >> 16) & 0x7fffu;
}

static void b(unsigned v) { file[len++] = (uint8_t)v; }
static void w(unsigned v) { b(v >> 8); b(v & 255u); }
static void s(const char *text) { while (*text) b((uint8_t)*text++); }

static enum codec_result decode_n(size_t n, const char *name, struct stpaint_image *img)
{
    /* A private copy, so ASan catches reads past n. */
    uint8_t *copy = malloc(n ? n : 1);
    enum codec_result r;
    memcpy(copy, file, n);
    r = stpaint_decode(copy, n, name, img);
    free(copy);
    return r;
}

/* Every shorter file must fail as truncated. */
static void prefixes(const char *name, size_t from)
{
    struct stpaint_image img;
    size_t n;
    for (n = from; n < len; n++) {
        enum codec_result r = decode_n(n, name, &img);
        if (r != CODEC_TRUNCATED) {
            printf("%s: prefix %zu of %zu gave %d\n", name, n, len, (int)r);
            assert(0);
        }
        assert(img.rgba == NULL);
    }
}

/* Lays a stream out as columns step bytes apart, column after column. */
static void unstream(unsigned step)
{
    unsigned column, pos, n = 0;
    for (column = 0; column < step; column++)
        for (pos = column; pos < 32000; pos += step)
            screen[pos] = stream[n++];
    assert(n == 32000);
}

/* Checks img against screen in mode, with palette words pal (big-endian). */
static void expect(const struct stpaint_image *img, unsigned mode, const uint8_t *pal)
{
    unsigned planes = 4u >> mode, width = mode ? 640 : 320, height = mode == 2 ? 400 : 200;
    unsigned stride = width * planes / 8, colours = 1u << planes, x, y, i, p;
    int ste = 0;
    assert(img->width == width && img->height == height && img->rgba != NULL);
    for (i = 0; mode < 2 && i < colours; i++)
        if (((pal[i * 2] << 8) | pal[i * 2 + 1]) & 0x888)
            ste = 1;
    for (y = 0; y < height; y++)
        for (x = 0; x < width; x++) {
            const uint8_t *px = img->rgba + ((size_t)y * width + x) * 4;
            unsigned index = 0, r, g, bl;
            for (p = 0; p < planes; p++) {
                const uint8_t *word = screen + y * stride + (x / 16) * planes * 2 + p * 2;
                unsigned v = (unsigned)word[0] << 8 | word[1];
                index |= (v >> (15 - x % 16) & 1u) << p;
            }
            if (mode == 2) {
                r = g = bl = index ? 0 : 255;
            } else {
                unsigned c = (unsigned)pal[index * 2] << 8 | pal[index * 2 + 1];
                r = st_level(c >> 8, ste);
                g = st_level(c >> 4, ste);
                bl = st_level(c, ste);
            }
            if (px[0] != r || px[1] != g || px[2] != bl || px[3] != 255) {
                printf("pixel %u,%u: %u %u %u, expected %u %u %u\n", x, y, px[0], px[1], px[2], r, g, bl);
                assert(0);
            }
        }
}

static void check(const char *name, unsigned mode, const uint8_t *pal)
{
    struct stpaint_image img;
    enum codec_result r = decode_n(len, name, &img);
    if (r != CODEC_OK) {
        printf("%s (mode %u, %zu bytes): %d\n", name ? name : "(no name)", mode, len, (int)r);
        assert(0);
    }
    expect(&img, mode, pal);
    stpaint_free(&img);
    assert(img.rgba == NULL && img.width == 0);
}

static void status(const char *name, enum codec_result want)
{
    struct stpaint_image img;
    enum codec_result r = decode_n(len, name, &img);
    if (r != want) {
        printf("%s: got %d, want %d\n", name, (int)r, (int)want);
        assert(0);
    }
    if (r == CODEC_OK)
        stpaint_free(&img);
}

static void random_palette(uint8_t pal[32], unsigned mask)
{
    unsigned i;
    for (i = 0; i < 16; i++) {
        unsigned c = rnd() & mask;
        pal[i * 2] = (uint8_t)(c >> 8);
        pal[i * 2 + 1] = (uint8_t)c;
    }
}

/* ---- Tiny ---- */

static unsigned words[16000];

static void tiny_file(unsigned mode, const uint8_t pal[32], size_t tail)
{
    unsigned i, n, set, column, y, ctrl_at, nctrl = 0, nwords = 0;
    uint8_t ctrl[20000];
    unsigned data[16001];

    /* The word stream: a long repeat, a short one, then literals in both forms. */
    for (i = 0; i < 300; i++) words[i] = 0x1234;
    ctrl[nctrl++] = 0; ctrl[nctrl++] = 300 >> 8; ctrl[nctrl++] = 300 & 255; data[nwords++] = 0x1234;
    for (i = 300; i < 305; i++) words[i] = 0xabcd;
    ctrl[nctrl++] = 5; data[nwords++] = 0xabcd;
    ctrl[nctrl++] = 0x80;
    for (i = 305; i < 433; i++) data[nwords++] = words[i] = rnd() * 3u;
    ctrl[nctrl++] = 1; ctrl[nctrl++] = 1000 >> 8; ctrl[nctrl++] = 1000 & 255;
    for (i = 433; i < 1433; i++) data[nwords++] = words[i] = rnd() * 5u;
    for (n = 1433; n < 16000;) {
        unsigned k = 16000 - n < 97 ? 16000 - n : 97;
        ctrl[nctrl++] = (uint8_t)(256 - k);
        for (i = 0; i < k; i++, n++) data[nwords++] = words[n] = rnd() ^ (n << 3);
    }
    /* The original packer leaves a command and a word over. */
    ctrl[nctrl++] = 2;
    data[nwords++] = 0x5555;

    n = 0;
    for (set = 0; set < 4; set++)
        for (column = set; column < 80; column += 4)
            for (y = 0; y < 200; y++, n++) {
                screen[y * 160 + column * 2] = (uint8_t)(words[n] >> 8);
                screen[y * 160 + column * 2 + 1] = (uint8_t)words[n];
            }

    len = 0;
    b(mode);
    if (mode > 2) { b(0x1b); b(0xfb); w(31); }
    for (i = 0; i < 32; i++) b(pal[i]);
    w(nctrl);
    w(nwords);
    ctrl_at = (unsigned)len;
    (void)ctrl_at;
    for (i = 0; i < nctrl; i++) b(ctrl[i]);
    for (i = 0; i < nwords; i++) w(data[i]);
    for (i = 0; i < tail; i++) b(0);
}

static void test_tiny(void)
{
    uint8_t pal[32];
    struct stpaint_image img;

    /* Low resolution with an ST palette, medium with a fourth bit only in a
       colour it doesn't use, and high resolution, where the palette is ignored. */
    random_palette(pal, 0x777);
    tiny_file(0, pal, 0);
    check("pic.tny", 0, pal);
    prefixes("pic.tny", 0);
    pal[30] |= 0x08;
    tiny_file(4, pal, 100);
    check("PIC.TN2", 1, pal);
    pal[1] |= 0x80;
    tiny_file(1, pal, 0);
    check("dir.x/pic.tn5", 1, pal);
    tiny_file(2, pal, 0);
    check("pic.tn3", 2, pal);
    tiny_file(5, pal, 0);
    check("pic.tn6", 2, pal);
    prefixes("pic.tn6", 0);
    /* Low resolution with an STE bit in any colour is STE. */
    tiny_file(3, pal, 512);
    check("Pic.Tn4", 0, pal);

    /* No signature: the name decides. */
    status("pic.tny.bak", CODEC_INVALID);
    status("pic.tny/", CODEC_INVALID);
    status("a.tny:pic", CODEC_INVALID);
    status("pic", CODEC_INVALID);
    status(NULL, CODEC_INVALID);
    assert(stpaint_decode(file, len, NULL, &img) == CODEC_INVALID);

    /* More than the block padding after the streams: not a Tiny file. */
    tiny_file(3, pal, 513);
    status("pic.tny", CODEC_INVALID);
    tiny_file(0, pal, 0);
    file[0] = 6;
    status("pic.tny", CODEC_INVALID);

    /* Streams that run out before the screen is full. */
    len = 0;
    b(0);
    for (unsigned i = 0; i < 32; i++) b(0);
    w(1); w(1); b(5); w(0x1111);
    status("pic.tny", CODEC_INVALID);
    len = 0;
    b(0);
    for (unsigned i = 0; i < 32; i++) b(0);
    w(2); w(1); b(0); b(1); w(0x1111);
    status("pic.tny", CODEC_INVALID);
    len = 0;
    b(0);
    for (unsigned i = 0; i < 32; i++) b(0);
    w(1); w(0); b(5);
    status("pic.tny", CODEC_INVALID);
    /* Lengths that point past the file. */
    len = 0;
    b(0);
    for (unsigned i = 0; i < 32; i++) b(0);
    w(0xffff); w(0xffff);
    status("pic.tny", CODEC_TRUNCATED);
}

/* ---- CrackArt ---- */

static void ca_header(unsigned compressed, unsigned mode, const uint8_t pal[32])
{
    static const unsigned words_for[3] = { 16, 4, 0 };
    unsigned i;
    len = 0;
    s("CA");
    b(compressed);
    b(mode);
    for (i = 0; mode < 3 && i < words_for[mode] * 2; i++) b(pal[i]);
}

static void test_crackart(void)
{
    uint8_t pal[32];
    unsigned mode, i, n, esc = 0xcd, delta = 0x3c;

    random_palette(pal, 0xfff);
    for (mode = 0; mode < 3; mode++) {
        ca_header(0, mode, pal);
        for (i = 0; i < 32000; i++) b(screen[i] = (uint8_t)rnd());
        check("x.ca1", mode, pal);
        check(NULL, mode, pal);
        prefixes("x.ca1", 0);
        b(1); b(2);
        check("x.ca1", mode, pal);
    }

    /* Compressed, down columns 7 bytes apart. */
    random_palette(pal, 0x777);
    for (mode = 0; mode < 3; mode++) {
        ca_header(1, mode, pal);
        b(esc); b(delta); w(7);
        n = 0;
        for (i = 0; i < 10; i++) { unsigned v = rnd() & 0x7f; b(v); stream[n++] = (uint8_t)v; }
        b(esc); b(esc); stream[n++] = (uint8_t)esc;
        b(esc); b(0); b(9); b(0x11); for (i = 0; i < 10; i++) stream[n++] = 0x11;
        b(esc); b(1); b(0x01); b(0x23); b(0x22); for (i = 0; i < 0x124; i++) stream[n++] = 0x22;
        b(esc); b(2); b(0x01); b(0x02); for (i = 0; i < 0x103; i++) stream[n++] = (uint8_t)delta;
        b(esc); b(7); b(0x33); for (i = 0; i < 8; i++) stream[n++] = 0x33;
        while (n < 20000) { unsigned v = rnd() & 0x7f; b(v); stream[n++] = (uint8_t)v; }
        /* End: the rest stays delta. */
        b(esc); b(2); b(0);
        while (n < 32000) stream[n++] = (uint8_t)delta;
        unstream(7);
        check("x.ca2", mode, pal);
        prefixes("x.ca2", 0);
    }

    /* A run past the end is clipped, and a stream that fills the screen needs
       no end marker; anything after it is ignored. */
    ca_header(1, 0, pal);
    b(esc); b(delta); w(160);
    b(esc); b(1); b(0xff); b(0xff); b(0x42);
    b(esc); b(2); b(0);
    memset(stream, 0x42, sizeof stream);
    unstream(160);
    check("x.ca1", 0, pal);

    /* Step 0: the whole picture is the delta byte. */
    ca_header(1, 2, pal);
    b(esc); b(0xff); w(0);
    memset(screen, 0xff, sizeof screen);
    check("x.ca3", 2, pal);

    ca_header(1, 0, pal);
    b(esc); b(0); w(32000);
    status("x.ca1", CODEC_INVALID);
    ca_header(2, 0, pal);
    status("x.ca1", CODEC_INVALID);
    status(NULL, CODEC_INVALID);
    ca_header(0, 3, pal);
    status("x.ca1", CODEC_INVALID);
    /* A bad signature is invalid under its own name. */
    ca_header(1, 0, pal);
    file[1] = 'B';
    status("x.ca1", CODEC_INVALID);
}

/* ---- Imagic ---- */

static void ic_header(unsigned mode, const uint8_t pal[32], unsigned esc)
{
    unsigned i;
    len = 0;
    s("IMDC");
    w(mode);
    for (i = 0; i < 32; i++) b(pal[i]);
    while (len < 0x40) b(0);
    b(0xc8); b(2); b(esc);
}

static void test_imagic(void)
{
    uint8_t pal[32];
    unsigned mode, i, n, esc = 0x8d;

    random_palette(pal, 0xfff);
    for (mode = 0; mode < 3; mode++) {
        ic_header(mode, pal, esc);
        n = 0;
        for (i = 0; i < 10; i++) { unsigned v = rnd() & 0x7f; b(v); stream[n++] = (uint8_t)v; }
        b(esc); b(esc); stream[n++] = (uint8_t)esc;
        b(esc); b(0); b(4); b(0x55); for (i = 0; i < 5; i++) stream[n++] = 0x55;
        /* Long counts: 257 + 256 per extra 1 + the low byte, after a skipped byte. */
        b(esc); b(1); b(0); b(11); b(0x66); for (i = 0; i < 268; i++) stream[n++] = 0x66;
        b(esc); b(1); b(1); b(1); b(0); b(3); b(0x77); for (i = 0; i < 257 + 512 + 3; i++) stream[n++] = 0x77;
        /* The signature block, which makes nothing. */
        b(esc); b(2); b(2); b(0x45); b(0x3b); b(0x29); b(0);
        b(esc); b(2); b(5); for (i = 0; i < 6; i++) stream[n++] = 0;
        b(esc); b(2); b(1); b(0); b(2); for (i = 0; i < 259; i++) stream[n++] = 0;
        b(esc); b(9); b(0x88); for (i = 0; i < 10; i++) stream[n++] = 0x88;
        while (n < 25000) { unsigned v = rnd() & 0x7f; b(v); stream[n++] = (uint8_t)v; }
        /* End: the rest is 0. */
        b(esc); b(2); b(0);
        while (n < 32000) stream[n++] = 0;
        unstream(160);
        check("x.ic1", mode, pal);
        check(NULL, mode, pal);
        prefixes("x.ic1", 0);
    }

    /* A long count as big as the file allows is clipped, as is the last run. */
    ic_header(0, pal, esc);
    b(esc); b(1);
    for (i = 0; i < 2000; i++) b(1);
    b(0); b(0); b(0x12);
    memset(stream, 0x12, sizeof stream);
    unstream(160);
    check("x.ic1", 0, pal);

    ic_header(3, pal, esc);
    status("x.ic1", CODEC_INVALID);
    ic_header(0, pal, esc);
    file[4] = 1;
    status("x.ic1", CODEC_INVALID);
    ic_header(0, pal, esc);
    file[0x40] = 0;
    status("x.ic1", CODEC_INVALID);
    ic_header(0, pal, esc);
    file[0] = 'J';
    status("x.ic1", CODEC_INVALID);
    /* A skip block that never ends. */
    ic_header(0, pal, esc);
    b(esc); b(2); b(2); b(1); b(2);
    status("x.ic1", CODEC_TRUNCATED);
}

/* ---- STAD ---- */

static void test_stad(void)
{
    unsigned vertical, i, n;
    struct stpaint_image img;

    for (vertical = 0; vertical < 2; vertical++) {
        len = 0;
        s(vertical ? "pM86" : "pM85");
        b(0x55); b(0xff); b(0x66);
        n = 0;
        b(0x55); b(9); for (i = 0; i < 10; i++) stream[n++] = 0xff;
        b(0x66); b(0x12); b(4); for (i = 0; i < 5; i++) stream[n++] = 0x12;
        while (n < 31900) {
            unsigned v = rnd() & 0xff;
            if (v == 0x55 || v == 0x66) continue;
            b(v); stream[n++] = (uint8_t)v;
        }
        b(0x55); b(99); for (i = 0; i < 100; i++) stream[n++] = 0xff;
        unstream(vertical ? 80 : 1);
        check("x.pac", 2, NULL);
        check(NULL, 2, NULL);
        prefixes("x.pac", 0);
        /* One byte over, clipped. */
        file[len - 1] = 100;
        check("x.pac", 2, NULL);
        /* One byte short: some writers do that, and it is left white. */
        file[len - 1] = 98;
        stream[31999] = 0;
        unstream(vertical ? 80 : 1);
        check("x.pac", 2, NULL);
        file[len - 1] = 97;
        status("x.pac", CODEC_TRUNCATED);
    }
    len = 0;
    s("pM87");
    b(0); b(0); b(0);
    status("x.pac", CODEC_INVALID);
    /* When ID and SPECIAL are equal, ID wins. */
    len = 0;
    s("pM85");
    b(0x55); b(0xaa); b(0x55);
    for (i = 0; i < 125; i++) { b(0x55); b(255); }
    assert(decode_n(len, "x.pac", &img) == CODEC_OK);
    assert(img.rgba[0] == 0 && img.rgba[4 * 640 * 400 - 4] == 255);
    stpaint_free(&img);
}

/* ---- Dali ---- */

static void dali_file(const uint8_t pal[32], unsigned zero_at)
{
    unsigned i, x, y, counts = 41;
    uint32_t values[41];
    char text[32];

    len = 0;
    for (i = 0; i < 32; i++) b(pal[i]);
    snprintf(text, sizeof text, "%u\r\n%u\r\n", counts, (counts - 1) * 4);
    s(text);
    /* Runs of 250 and 150 longwords, which cross the columns. The last
       count is left over, as in real files. */
    for (i = 0; i < counts; i++) b(i == zero_at ? 0 : i % 2 ? 150 : 250);
    for (i = 0; i < counts - 1; i++) {
        values[i] = (uint32_t)rnd() << 17 ^ rnd();
        w(values[i] >> 16);
        w(values[i] & 0xffff);
    }
    /* Longword n of the stream is at column n / 200, line n % 200. */
    for (x = 0; x < 40; x++)
        for (y = 0; y < 200; y++) {
            unsigned n = x * 200 + y, e = 0, k = 0;
            while (k + (e % 2 ? 150u : 250u) <= n)
                k += e++ % 2 ? 150u : 250u;
            screen[y * 160 + x * 4] = (uint8_t)(values[e] >> 24);
            screen[y * 160 + x * 4 + 1] = (uint8_t)(values[e] >> 16);
            screen[y * 160 + x * 4 + 2] = (uint8_t)(values[e] >> 8);
            screen[y * 160 + x * 4 + 3] = (uint8_t)values[e];
        }
}

static void test_dali(void)
{
    uint8_t pal[32];

    random_palette(pal, 0x777);
    dali_file(pal, 99);
    check("x.lpk", 0, pal);
    check("x.MPK", 1, pal);
    check("x.hpk", 2, pal);
    prefixes("x.lpk", 0);
    status(NULL, CODEC_INVALID);
    dali_file(pal, 7);
    status("x.lpk", CODEC_INVALID);

    /* The count table ends before the screen is full. */
    dali_file(pal, 99);
    memcpy(file + 32, "30", 2);
    status("x.lpk", CODEC_INVALID);
    dali_file(pal, 99);
    file[32] = 'x';
    status("x.lpk", CODEC_INVALID);
    dali_file(pal, 99);
    file[34] = '\n';
    status("x.lpk", CODEC_INVALID);
    len = 32;
    s("32001\r\n1\r\n");
    status("x.lpk", CODEC_INVALID);
    len = 32;
    s("0\r\n1\r\n");
    status("x.lpk", CODEC_INVALID);
    len = 32;
    s("5\r\n0\r\n");
    status("x.lpk", CODEC_INVALID);
}

/* ---- Pablo ---- */

static void test_pablo(void)
{
    uint8_t pal[32];
    unsigned mode, i;

    random_palette(pal, 0xfff);
    for (mode = 0; mode < 3; mode++) {
        len = 0;
        s("PABLO PACKED PICTURE: Groupe CDND \r\n32036\r\n");
        b(mode); b(0); w(32036);
        for (i = 0; i < 32; i++) b(pal[i]);
        for (i = 0; i < 32000; i++) b(screen[i] = (uint8_t)rnd());
        check("x.ppp", mode, pal);
        check("x.pa3", mode, pal);
        check(NULL, mode, pal);
        prefixes("x.ppp", 0);
    }
    file[0x2c] = 29;
    status("x.pa3", CODEC_INVALID);
    file[0x2c] = 0;
    file[0x2b] = 3;
    status("x.pa3", CODEC_INVALID);
    file[0x2b] = 0;
    file[0] = 'Q';
    status("x.pa3", CODEC_INVALID);
}

/* ---- Picworks ---- */

static void test_picworks(void)
{
    unsigned i, n = 0;

    len = 0;
    w(3);
    w(0);
    w(3); w(5);
    w(0); w(100);
    w(1); w(0);
    for (i = 0; i < 24; i++) b(screen[n++] = (uint8_t)rnd());
    for (i = 0; i < 8; i++) b(stream[i] = (uint8_t)rnd());
    for (i = 0; i < 40; i++) screen[n++] = stream[i % 8];
    for (i = 0; i < 8; i++) b(stream[i] = (uint8_t)rnd());
    for (i = 0; i < 800; i++) screen[n++] = stream[i % 8];
    for (i = 0; i < 8; i++) b(screen[n++] = (uint8_t)rnd());
    for (i = 0; i < 8; i++) b(rnd());
    while (n < 32000) b(screen[n++] = (uint8_t)rnd());
    check("x.cp3", 2, NULL);
    prefixes("x.cp3", 0);
    status(NULL, CODEC_INVALID);
    b(0);
    status("x.cp3", CODEC_INVALID);
    len = 0;
    w(1); w(0); w(0); w(0xffff);
    for (i = 0; i < 40000; i++) b(0);
    status("x.cp3", CODEC_INVALID);
}

/* ---- PaintShop ---- */

static void psc_header(unsigned width, unsigned height)
{
    len = 0;
    s("tm89");
    w(0); w(0);
    b(2); b(1);
    w(width - 1);
    w(height - 1);
}

static void expect_mono(const struct stpaint_image *img, unsigned width, unsigned height,
                        const uint8_t *bits)
{
    unsigned stride = (width + 7) / 8, x, y;
    assert(img->width == width && img->height == height);
    for (y = 0; y < height; y++)
        for (x = 0; x < width; x++) {
            unsigned level = bits[y * stride + x / 8] >> (7 - x % 8) & 1 ? 0 : 255;
            const uint8_t *px = img->rgba + ((size_t)y * width + x) * 4;
            assert(px[0] == level && px[1] == level && px[2] == level && px[3] == 255);
        }
}

static void test_paintshop(void)
{
    unsigned stride = 13, i, line = 0;
    struct stpaint_image img;

    /* 100x50: every line command. */
    psc_header(100, 50);
    b(0); memset(screen + line++ * stride, 0, stride);
    b(200); memset(screen + line++ * stride, 0xff, stride);
    b(100); b(0x5a); memset(screen + line++ * stride, 0x5a, stride);
    b(102); b(0x12); b(0x34);
    for (i = 0; i < stride; i++) screen[line * stride + i] = i & 1 ? 0x34 : 0x12;
    line++;
    b(110);
    for (i = 0; i < stride; i++) b(screen[line * stride + i] = (uint8_t)rnd());
    line++;
    b(10); b(4);
    for (i = 0; i < 5; i++, line++) memcpy(screen + line * stride, screen + (line - 1) * stride, stride);
    while (line < 50) {
        b(110);
        for (i = 0; i < stride; i++) b(screen[line * stride + i] = (uint8_t)rnd());
        line++;
    }
    b(0xff);
    assert(decode_n(len, "x.psc", &img) == CODEC_OK);
    expect_mono(&img, 100, 50, screen);
    stpaint_free(&img);
    assert(decode_n(len, NULL, &img) == CODEC_OK);
    stpaint_free(&img);
    prefixes("x.psc", 0);

    /* 16x400: 257 + n lines copied. */
    psc_header(16, 400);
    b(100); b(0xc3);
    b(12); b(142);
    b(0xff);
    memset(screen, 0xc3, 800);
    assert(decode_n(len, "x.psc", &img) == CODEC_OK);
    expect_mono(&img, 16, 400, screen);
    stpaint_free(&img);
    file[17] = 143;
    status("x.psc", CODEC_INVALID);

    /* Stored. */
    psc_header(640, 400);
    b(99);
    for (i = 0; i < 32000; i++) b(screen[i] = (uint8_t)rnd());
    b(0xff);
    assert(decode_n(len, "x.psc", &img) == CODEC_OK);
    expect_mono(&img, 640, 400, screen);
    stpaint_free(&img);
    prefixes("x.psc", 0);

    psc_header(641, 1);
    b(0); b(0xff);
    status("x.psc", CODEC_INVALID);
    psc_header(8, 401);
    b(0); b(0xff);
    status("x.psc", CODEC_INVALID);
    psc_header(8, 2);
    b(10); b(0); b(0xff);
    status("x.psc", CODEC_INVALID);
    psc_header(8, 2);
    b(0); b(10); b(1); b(0xff);
    status("x.psc", CODEC_INVALID);
    psc_header(8, 2);
    b(0); b(55); b(0xff);
    status("x.psc", CODEC_INVALID);
    psc_header(8, 1);
    file[8] = 3;
    b(0); b(0xff);
    status("x.psc", CODEC_INVALID);
    /* The smallest picture. */
    psc_header(1, 1);
    b(200); b(0xff);
    assert(decode_n(len, "x.psc", &img) == CODEC_OK);
    assert(img.width == 1 && img.height == 1 && img.rgba[0] == 0);
    stpaint_free(&img);
}

/* ---- Writer ---- */

static void roundtrip(const uint8_t *rgba, unsigned width, unsigned height)
{
    static uint8_t out[TINY_MAX_SIZE];
    struct stpaint_image img;
    size_t size;
    assert(tiny_encode(rgba, width, height, out, &size) == CODEC_OK);
    assert(size <= TINY_MAX_SIZE);
    memcpy(file, out, size);
    len = size;
    assert(decode_n(len, "x.tny", &img) == CODEC_OK);
    assert(img.width == width && img.height == height);
    assert(memcmp(img.rgba, rgba, (size_t)width * height * 4) == 0);
    stpaint_free(&img);
}

static void test_writer(void)
{
    static uint8_t rgba[640 * 400 * 4];
    static const uint8_t st[8] = { 0, 36, 73, 109, 146, 182, 219, 255 };
    unsigned i, x, y;
    size_t size;
    static uint8_t out[TINY_MAX_SIZE];

    /* Low resolution in 16 ST colours, with runs and noise. */
    for (y = 0; y < 200; y++)
        for (x = 0; x < 320; x++) {
            unsigned c = y < 50 ? 3 : y < 100 ? (x / 7) % 16 : rnd() % 16;
            uint8_t *p = rgba + (y * 320 + x) * 4;
            p[0] = st[c % 8]; p[1] = st[c / 2]; p[2] = st[(c * 3) % 8]; p[3] = 255;
        }
    roundtrip(rgba, 320, 200);
    /* Medium resolution in 3 STE colours, all with even levels, so the
       unused fourth colour has to mark the palette as STE. */
    for (y = 0; y < 200; y++)
        for (x = 0; x < 640; x++) {
            uint8_t *p = rgba + (y * 640 + x) * 4;
            p[0] = (uint8_t)(((x + y) % 3) * 68); p[1] = 34; p[2] = 238; p[3] = 255;
        }
    roundtrip(rgba, 640, 200);
    assert(tiny_encode(rgba, 640, 200, out, &size) == CODEC_OK);
    assert(out[0] == 1);
    /* High resolution in black and white, including a picture of one colour. */
    for (i = 0; i < 640 * 400; i++) {
        uint8_t v = (i / 640 + i % 640) % 3 ? 255 : 0;
        rgba[i * 4] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = v;
        rgba[i * 4 + 3] = 255;
    }
    roundtrip(rgba, 640, 400);
    memset(rgba, 255, 640 * 400 * 4);
    roundtrip(rgba, 640, 400);
    /* Transparent pixels go over white. */
    rgba[0] = rgba[1] = rgba[2] = rgba[3] = 0;
    assert(tiny_encode(rgba, 640, 400, out, &size) == CODEC_OK);
    memcpy(file, out, size);
    len = size;
    {
        struct stpaint_image img;
        assert(decode_n(len, "x.tny", &img) == CODEC_OK);
        assert(img.rgba[0] == 255 && img.rgba[1] == 255 && img.rgba[3] == 255);
        stpaint_free(&img);
    }

    /* What Tiny can't hold. */
    assert(tiny_encode(rgba, 320, 201, out, &size) == CODEC_INVALID);
    assert(tiny_encode(NULL, 320, 200, out, &size) == CODEC_INVALID);
    rgba[0] = 1;
    rgba[3] = 255;
    assert(tiny_encode(rgba, 640, 400, out, &size) == CODEC_INVALID);
    for (i = 0; i < 320 * 200; i++) {
        rgba[i * 4] = st[i % 8]; rgba[i * 4 + 1] = st[(i / 8) % 3]; rgba[i * 4 + 2] = 0;
        rgba[i * 4 + 3] = 255;
    }
    assert(tiny_encode(rgba, 320, 200, out, &size) == CODEC_INVALID);
    /* 16 STE colours with even levels leave no colour to mark the palette. */
    for (i = 0; i < 320 * 200; i++) {
        rgba[i * 4] = (uint8_t)((i % 8) * 34);
        rgba[i * 4 + 1] = (uint8_t)(((i / 8) % 2) * 34);
        rgba[i * 4 + 2] = 0;
    }
    assert(tiny_encode(rgba, 320, 200, out, &size) == CODEC_INVALID);
}

int main(void)
{
    struct stpaint_image img;

    setvbuf(stdout, NULL, _IONBF, 0);
    assert(stpaint_decode(file, 0, "x.tny", NULL) == CODEC_INVALID);
    assert(stpaint_decode(NULL, 0, "x.tny", &img) == CODEC_TRUNCATED);
    test_tiny();
    test_crackart();
    test_imagic();
    test_stad();
    test_dali();
    test_pablo();
    test_picworks();
    test_paintshop();
    test_writer();
    printf("stpaint: ok\n");
    return 0;
}
