#include "../formats/c64/decode.h"
#include "../formats/c64/encode.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t file[40000];
static uint8_t packed[60000];
static uint8_t rgba[320 * 200 * 4];
static uint8_t saved[C64_ENCODE_MAX];

static unsigned seed = 12345;
static unsigned rnd(void)
{
    seed = seed * 1103515245u + 12345u;
    return seed >> 16 & 0x7fffu;
}

static void fill_random(uint8_t *p, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++)
        p[i] = (uint8_t)rnd();
}

static void set_load(uint8_t *p, unsigned address)
{
    p[0] = (uint8_t)(address & 0xffu);
    p[1] = (uint8_t)(address >> 8);
}

/* Colour index of a decoded pixel. */
static int at(const struct c64_image *image, unsigned x, unsigned y)
{
    const uint8_t *p = image->rgba + ((size_t)y * image->width + x) * 4u;
    assert(p[3] == 255);
    return c64_index(p);
}

static void blended(const struct c64_image *image, unsigned x, unsigned y, unsigned a, unsigned b)
{
    const uint8_t *p = image->rgba + ((size_t)y * image->width + x) * 4u;
    uint8_t ca[3], cb[3];
    unsigned i;
    c64_colour(a, ca);
    c64_colour(b, cb);
    for (i = 0; i < 3; i++)
        assert(p[i] == (ca[i] + cb[i]) / 2u);
    assert(p[3] == 255);
}

static void decode_ok(size_t length, const char *name, struct c64_image *image,
                      unsigned width)
{
    enum codec_result r = c64_decode(file, length, name, image);
    if (r != CODEC_OK || image->width != width)
        fprintf(stderr, "%s (%zu): %d, width %u\n", name ? name : "(null)", length, r,
                image->width);
    assert(r == CODEC_OK);
    assert(image->width == width && image->height == 200);
}

static void test_palette(void)
{
    uint8_t rgb[3];
    unsigned i;

    for (i = 0; i < 16; i++) {
        c64_colour(i, rgb);
        assert(c64_index(rgb) == (int)i);
    }
    c64_colour(0, rgb);
    assert(rgb[0] == 0 && rgb[1] == 0 && rgb[2] == 0);
    c64_colour(2, rgb);
    assert(rgb[0] == 0x68 && rgb[1] == 0x37 && rgb[2] == 0x2b);
    rgb[0] = 1;
    rgb[1] = rgb[2] = 0;
    assert(c64_index(rgb) == -1);
}

/* Art Studio: bitmap at 2, matrix at 0x1f42; set bits take the high nibble. */
static void test_hires(void)
{
    struct c64_image image;
    unsigned cell, x, y;

    memset(file, 0, 9009);
    set_load(file, 0x2000);
    for (cell = 0; cell < 1000; cell++) {
        file[0x1f42 + cell] = (uint8_t)(cell % 16u << 4 | (15u - cell % 16u));
        for (y = 0; y < 8; y++)
            file[2 + cell * 8u + y] = (uint8_t)(0x80u >> y);  /* diagonal */
    }
    decode_ok(9009, "pic.art", &image, 320);
    for (y = 0; y < 200; y++)
        for (x = 0; x < 320; x++) {
            cell = y / 8u * 40u + x / 8u;
            if (x % 8u == y % 8u)
                assert(at(&image, x, y) == (int)(cell % 16u));
            else
                assert(at(&image, x, y) == (int)(15u - cell % 16u));
        }
    c64_free(&image);
    /* Hires bitmap: white on black; Run Paint: black on white. */
    memset(file, 0, 8002);
    file[2] = 0x80;
    decode_ok(8002, "x.hbm", &image, 320);
    assert(at(&image, 0, 0) == 1 && at(&image, 1, 0) == 0);
    c64_free(&image);
    decode_ok(8002, "x.rpo", &image, 320);
    assert(at(&image, 0, 0) == 0 && at(&image, 1, 0) == 1);
    c64_free(&image);
    /* Without a name 8002 bytes is a plain hires bitmap. */
    decode_ok(8002, NULL, &image, 320);
    assert(at(&image, 0, 0) == 1);
    c64_free(&image);
}

/* Koala: bit pairs 0 background, 1 matrix high, 2 matrix low, 3 colour RAM. */
static void test_multicolour(void)
{
    struct c64_image image;
    unsigned cell, x, y;

    memset(file, 0, 10003);
    set_load(file, 0x6000);
    for (cell = 0; cell < 1000; cell++) {
        file[0x1f42 + cell] = (uint8_t)((cell + 1u) % 16u << 4 | (cell + 2u) % 16u);
        file[0x232a + cell] = (uint8_t)(0xf0u | (cell + 3u) % 16u);  /* high nibble ignored */
        for (y = 0; y < 8; y++)
            file[2 + cell * 8u + y] = 0x1b;  /* 00 01 10 11 */
    }
    file[0x2712] = 0xf6;  /* background 6, high nibble ignored */
    decode_ok(10003, "pic.koa", &image, 320);
    for (y = 0; y < 200; y += 7)
        for (x = 0; x < 320; x++) {
            unsigned want[4];
            cell = y / 8u * 40u + x / 8u;
            want[0] = 6;
            want[1] = (cell + 1u) % 16u;
            want[2] = (cell + 2u) % 16u;
            want[3] = (cell + 3u) % 16u;
            assert(at(&image, x, y) == (int)want[x % 8u / 2u]);
        }
    c64_free(&image);
    /* Up to 64 bytes of junk after a Koala at $6000 or $4400 are ignored. */
    memset(file + 10003, 0xfe, 64);
    decode_ok(10067, NULL, &image, 320);
    assert(at(&image, 0, 0) == 6 && at(&image, 2, 0) == 1);
    c64_free(&image);
    set_load(file, 0x4400);
    decode_ok(10017, "x.kla", &image, 320);
    assert(at(&image, 0, 0) == 6);
    c64_free(&image);
    assert(c64_decode(file, 10068, "x.koa", &image) == CODEC_INVALID);
    set_load(file, 0x5000);
    assert(c64_decode(file, 10034, NULL, &image) == CODEC_INVALID);
    set_load(file, 0x6000);
    /* The same picture without a load address. */
    memmove(file, file + 2, 10001);
    decode_ok(10001, "pic.koa", &image, 320);
    assert(at(&image, 0, 0) == 6 && at(&image, 2, 0) == 1 && at(&image, 7, 0) == 3);
    c64_free(&image);
}

/* FLI Graph: colour RAM at 2, eight matrices from 0x402, bitmap at 0x2402,
   and the leftmost three columns cut off. */
static void test_fli(void)
{
    struct c64_image image;
    unsigned cell, x, y;

    memset(file, 0, 17409);
    set_load(file, 0x3c00);
    for (y = 0; y < 8; y++)
        for (cell = 0; cell < 1000; cell++)
            file[0x402 + y * 1024u + cell] = (uint8_t)(y << 4 | (cell % 16u));
    for (cell = 0; cell < 1000; cell++) {
        file[2 + cell] = (uint8_t)(cell / 40u % 16u);
        for (y = 0; y < 8; y++)
            file[0x2402 + cell * 8u + y] = 0x6c;  /* 01 10 11 00 */
    }
    decode_ok(17409, "x.fli", &image, 296);
    for (y = 0; y < 200; y++)
        for (x = 0; x < 296; x++) {
            unsigned sx = x + 24u, want;
            cell = y / 8u * 40u + sx / 8u;
            switch (sx % 8u / 2u) {
            case 0: want = y % 8u; break;
            case 1: want = cell % 16u; break;
            case 2: want = cell / 40u % 16u; break;
            default: want = 0; break;
            }
            assert(at(&image, x, y) == (int)want);
        }
    c64_free(&image);

    /* Blackmail FLI: a background for each line at 2, and FLI Editor's at 8. */
    memset(file, 0, 17665);
    set_load(file, 0x3b00);
    for (y = 0; y < 206; y++)
        file[2 + y] = (uint8_t)(y % 16u);
    decode_ok(17474, "x.bml", &image, 296);
    for (y = 0; y < 200; y++)
        assert(at(&image, 5, y) == (int)(y % 16u));
    c64_free(&image);
    set_load(file, 0x3800);
    decode_ok(17665, NULL, &image, 296);  /* $3800: FLI Editor */
    for (y = 0; y < 200; y++)
        assert(at(&image, 5, y) == (int)((y + 6u) % 16u));
    c64_free(&image);
    decode_ok(17665, "x.bml", &image, 296);
    assert(at(&image, 5, 0) == 0);
    c64_free(&image);

    /* AFLI: hires with a matrix per line. */
    memset(file, 0, 16385);
    for (y = 0; y < 8; y++)
        memset(file + 2 + y * 1024u, (int)(y << 4 | (15u - y)), 1000);
    for (cell = 0; cell < 1000; cell++)
        for (y = 0; y < 8; y++)
            file[0x2002 + cell * 8u + y] = 0xaa;
    decode_ok(16385, "x.afl", &image, 296);
    for (y = 0; y < 200; y++) {
        assert(at(&image, 0, y) == (int)(y % 8u));
        assert(at(&image, 1, y) == (int)(15u - y % 8u));
    }
    c64_free(&image);
}

/* Drazlace: two bitmaps share colours; the second moves right when asked. */
static void test_interlace(void)
{
    struct c64_image image;
    unsigned cell, y;

    memset(file, 0, 18242);
    set_load(file, 0x5800);
    for (cell = 0; cell < 1000; cell++) {
        file[2 + cell] = 3;             /* colour RAM */
        file[0x402 + cell] = 0x45;      /* matrix */
        for (y = 0; y < 8; y++) {
            file[0x802 + cell * 8u + y] = 0x1b;   /* 00 01 10 11 */
            file[0x2802 + cell * 8u + y] = 0xe4;  /* 11 10 01 00 */
        }
    }
    file[0x2742] = 7;
    decode_ok(18242, "x.drl", &image, 320);
    blended(&image, 0, 0, 7, 3);
    blended(&image, 2, 0, 4, 5);
    blended(&image, 4, 0, 5, 4);
    blended(&image, 6, 5, 3, 7);
    c64_free(&image);
    file[0x2744] = 1;
    decode_ok(18242, NULL, &image, 320);
    blended(&image, 0, 0, 7, 7);  /* shifted in: background */
    blended(&image, 1, 0, 7, 3);
    blended(&image, 2, 0, 4, 3);
    blended(&image, 3, 0, 4, 5);
    blended(&image, 7, 0, 3, 7);
    blended(&image, 8, 0, 7, 7);
    blended(&image, 9, 0, 7, 3);
    c64_free(&image);
    file[0x2744] = 2;
    assert(c64_decode(file, 18242, "x.drl", &image) == CODEC_INVALID);
    assert(image.rgba == NULL);

    /* Gunpaint's line backgrounds: 177 lines at 0x3f51, 20 at 0x47ea, the
       last of those repeated for the last three lines. Blank bitmaps show them. */
    memset(file, 0, 33603);
    set_load(file, 0x4000);
    for (y = 0; y < 177; y++)
        file[0x3f51 + y] = (uint8_t)(y % 16u);
    for (y = 0; y < 19; y++)
        file[0x47ea + y] = (uint8_t)((y + 5u) % 16u);
    file[0x47fd] = 9;
    decode_ok(33603, "x.gun", &image, 296);
    for (y = 0; y < 200; y++) {
        unsigned want = y < 177 ? y % 16u : y < 196 ? (y - 177u + 5u) % 16u : 9u;
        assert(at(&image, 0, y) == (int)want && at(&image, 295, y) == (int)want);
    }
    c64_free(&image);

    /* Interlace Hires Editor: black and grey bitmaps, blended. */
    memset(file, 0, 16194);
    file[2] = 0x80;
    file[0x2002] = 0xc0;
    decode_ok(16194, "x.ihe", &image, 320);
    blended(&image, 0, 0, 0, 0);
    blended(&image, 1, 0, 12, 0);
    blended(&image, 2, 0, 12, 12);
    c64_free(&image);

    /* Hireslace and ECI share a size; the extension picks, else ECI. */
    memset(file, 0, 32770);
    decode_ok(32770, "x.hle", &image, 320);
    c64_free(&image);
    decode_ok(32770, NULL, &image, 296);
    c64_free(&image);
    decode_ok(32770, "x.eci", &image, 296);
    c64_free(&image);
}

/* RLE-pack src: runs of four or more, and every escape byte, as records. */
static size_t pack(const uint8_t *src, size_t n, uint8_t *out, int escape,
                   int value_first, unsigned bias)
{
    size_t i = 0, o = 0;
    while (i < n) {
        size_t run = 1;
        while (i + run < n && src[i + run] == src[i] && run < 255u + bias)
            run++;
        if (run >= 4 || src[i] == escape) {
            out[o++] = (uint8_t)escape;
            if (value_first) {
                out[o++] = src[i];
                out[o++] = (uint8_t)(run - bias);
            } else {
                out[o++] = (uint8_t)(run - bias);
                out[o++] = src[i];
            }
            i += run;
        } else {
            out[o++] = src[i++];
        }
    }
    return o;
}

static void same_image(const struct c64_image *a, const struct c64_image *b)
{
    assert(a->width == b->width && a->height == b->height);
    assert(memcmp(a->rgba, b->rgba, (size_t)a->width * a->height * 4u) == 0);
}

/* Every prefix of packed[0..n) short of its last trailing bytes is
   truncated; the whole must match raw. */
static void check_packed(size_t n, size_t trailing, const char *name,
                         const struct c64_image *raw, size_t step)
{
    struct c64_image image;
    size_t cut;

    memcpy(file, packed, n);
    decode_ok(n, name, &image, raw->width);
    same_image(&image, raw);
    c64_free(&image);
    for (cut = 2; cut < n - trailing; cut += step) {
        enum codec_result r = c64_decode(file, cut, name, &image);
        if (r != CODEC_TRUNCATED)
            fprintf(stderr, "%s cut %zu of %zu: %d\n", name ? name : "(null)", cut, n, r);
        assert(r == CODEC_TRUNCATED);
        assert(image.rgba == NULL);
    }
}

static void test_packed(void)
{
    static uint8_t raw[34000];
    struct c64_image want;
    size_t n, i;

    /* Graphics Galaxy Koala: 0xFE value count after the load address. */
    fill_random(raw, 10003);
    memset(raw + 500, 0xfe, 40);
    memset(raw + 3000, 0x11, 700);
    set_load(raw, 0x6000);
    memcpy(file, raw, 10003);
    decode_ok(10003, "x.koa", &want, 320);
    memcpy(packed, raw, 2);
    n = 2 + pack(raw + 2, 10001, packed + 2, 0xfe, 1, 0);
    check_packed(n, 0, "x.gg", &want, 1);
    /* A run past the end is cut short; a zero run writes nothing. */
    packed[n] = 0xfe;
    packed[n + 1] = 0x22;
    packed[n + 2] = 0;
    memcpy(file, packed, n + 3);
    {
        struct c64_image image;
        memmove(file + 5, file + 2, n - 2);
        file[2] = 0xfe;
        file[3] = 0x77;
        file[4] = 0;  /* empty run first */
        decode_ok(n + 3, "x.gg", &image, 320);
        same_image(&image, &want);
        c64_free(&image);
        /* Overrunning final run: pack all but the last 10 bytes, then a run of 200. */
        memcpy(raw + 10003 - 10, "\x33\x33\x33\x33\x33\x33\x33\x33\x33\x33", 10);
        memcpy(file, raw, 10003);
        c64_free(&want);
        decode_ok(10003, "x.koa", &want, 320);
        memcpy(file, raw, 2);
        n = 2 + pack(raw + 2, 10001 - 10, file + 2, 0xfe, 1, 0);
        file[n] = 0xfe;
        file[n + 1] = 0x33;
        file[n + 2] = 200;
        decode_ok(n + 3, "x.gg", &image, 320);
        same_image(&image, &want);
        c64_free(&image);
    }
    /* Amica: 0xC2 count value, the same picture. */
    memcpy(packed, raw, 2);
    n = 2 + pack(raw + 2, 10001, packed + 2, 0xc2, 0, 0);
    packed[n++] = 0xc2;
    packed[n++] = 0;
    check_packed(n, 2, "x.ami", &want, 7);
    c64_free(&want);

    /* Doodle packed as JJ. */
    fill_random(raw, 9218);
    memset(raw + 100, 0, 300);
    set_load(raw, 0x5c00);
    memcpy(file, raw, 9218);
    decode_ok(9218, "x.dd", &want, 320);
    memcpy(packed, raw, 2);
    n = 2 + pack(raw + 2, 9024, packed + 2, 0xfe, 1, 0);
    check_packed(n, 0, "x.jj", &want, 5);
    /* A short .dd is a packed one. */
    check_packed(n, 0, "x.dd", &want, 50);
    c64_free(&want);

    /* Drazpaint and Drazlace: signature, escape at 15, escape count value. */
    fill_random(raw, 10051);
    set_load(raw, 0x5800);
    memcpy(file, raw, 10051);
    decode_ok(10051, "x.drz", &want, 320);
    memcpy(packed, raw, 2);
    memcpy(packed + 2, "DRAZPAINT 1.4", 13);
    packed[15] = 0x9d;
    n = 16 + pack(raw + 2, 10049, packed + 16, 0x9d, 0, 0);
    check_packed(n, 0, "x.drz", &want, 3);
    /* Without a name the signature is enough, once it is all there. */
    {
        struct c64_image image;
        size_t cut;
        decode_ok(n, NULL, &image, 320);
        same_image(&image, &want);
        c64_free(&image);
        for (cut = 15; cut < n; cut += 97) {
            enum codec_result r = c64_decode(file, cut, NULL, &image);
            if (r != CODEC_TRUNCATED)
                fprintf(stderr, "drz cut %zu: %d\n", cut, r);
            assert(r == CODEC_TRUNCATED);
        }
        assert(c64_decode(file, 14, NULL, &image) == CODEC_INVALID);
    }
    c64_free(&want);

    fill_random(raw, 18242);
    set_load(raw, 0x5800);
    raw[0x2744] = 1;
    memcpy(file, raw, 18242);
    decode_ok(18242, "x.drl", &want, 320);
    memcpy(packed, raw, 2);
    memcpy(packed + 2, "DRAZLACE! 1.0", 13);
    packed[15] = 0x00;
    n = 16 + pack(raw + 2, 18240, packed + 16, 0x00, 0, 0);
    check_packed(n, 0, "x.dlp", &want, 11);
    c64_free(&want);

    /* ECI packed: escape at 2, then escape count value into 32768 bytes. */
    fill_random(raw, 32770);
    set_load(raw, 0x4000);
    memcpy(file, raw, 32770);
    decode_ok(32770, "x.eci", &want, 296);
    memcpy(packed, raw, 2);
    packed[2] = 0x55;
    n = 3 + pack(raw + 2, 32768, packed + 3, 0x55, 0, 0);
    check_packed(n, 0, "x.ecp", &want, 101);
    c64_free(&want);

    /* Funpaint: 18-byte header; byte 16 non-zero means packed, escape at 17. */
    fill_random(raw, 33694);
    set_load(raw, 0x3ff0);
    memcpy(raw + 2, "FUNPAINT (MT) ", 14);
    raw[16] = 0;
    for (i = 0x3000; i < 0x3800; i++)
        raw[i] = 0;
    memcpy(file, raw, 33694);
    decode_ok(33694, "x.fun", &want, 296);
    memcpy(packed, raw, 16);
    packed[16] = 1;
    packed[17] = 0xab;
    n = 18 + pack(raw + 18, 33694 - 18, packed + 18, 0xab, 0, 0);
    check_packed(n, 0, "x.fp2", &want, 211);
    c64_free(&want);
    /* A Koala whose bitmap happens to start with a signature is still Koala:
       found by content, signatures need their program's load address. */
    memset(file, 0, 10003);
    set_load(file, 0x6000);
    memcpy(file + 2, "FUNPAINT (MT) ", 14);
    decode_ok(10003, NULL, &want, 320);
    c64_free(&want);
    memcpy(file + 2, "DRAZPAINT 1.4", 13);
    decode_ok(10003, NULL, &want, 320);
    c64_free(&want);
    /* A raw Funpaint of the wrong size. */
    memcpy(file, raw, 33694);
    assert(c64_decode(file, 33693, "x.fun", &want) == CODEC_TRUNCATED);
    assert(c64_decode(file, 20, "x.fun", &want) == CODEC_TRUNCATED);
}

/* Formats of the same size told apart by load address, then extension. */
static void test_recognition(void)
{
    struct c64_image image;
    unsigned cell, y;

    /* 10242 bytes: Blazing Paddles ($A000, background at 0x1f82), Artist 64
       ($4000, 0x2801), Rainbow Painter ($5C00, black) and Dolphin Ed ($5800,
       colour RAM first). A blank bitmap shows the background. */
    memset(file, 0, 10242);
    file[0x1f82] = 1;
    file[0x2801] = 2;
    file[0x7ea] = 5;
    set_load(file, 0xa000);
    decode_ok(10242, NULL, &image, 320);
    assert(at(&image, 0, 0) == 1);
    c64_free(&image);
    set_load(file, 0x4000);
    decode_ok(10242, NULL, &image, 320);
    assert(at(&image, 0, 0) == 2);
    c64_free(&image);
    set_load(file, 0x5c00);
    decode_ok(10242, NULL, &image, 320);
    assert(at(&image, 0, 0) == 0);
    c64_free(&image);
    set_load(file, 0x5800);
    decode_ok(10242, NULL, &image, 320);
    assert(at(&image, 0, 0) == 5);
    c64_free(&image);
    /* An unknown load address falls back to the first format of that size;
       the extension overrides the load address. */
    set_load(file, 0x1234);
    decode_ok(10242, NULL, &image, 320);
    assert(at(&image, 0, 0) == 1);
    c64_free(&image);
    decode_ok(10242, "PICTURE.A64", &image, 320);
    assert(at(&image, 0, 0) == 2);
    c64_free(&image);
    decode_ok(10242, "dir.koa/x.dol", &image, 320);
    assert(at(&image, 0, 0) == 5);
    c64_free(&image);
    /* An extension that doesn't fit the size is ignored. */
    decode_ok(10242, "x.koa", &image, 320);
    assert(at(&image, 0, 0) == 1);
    c64_free(&image);
    decode_ok(10242, "x.", &image, 320);
    c64_free(&image);
    decode_ok(10242, "x.toolong", &image, 320);
    c64_free(&image);

    /* 9218 bytes: Doodle at $5C00 (matrix first), Hi-Eddi at $2000. */
    memset(file, 0, 9218);
    for (cell = 0; cell < 1000; cell++) {
        file[2 + cell] = 0x21;
        file[0x2002 + cell] = 0x43;
    }
    for (y = 0; y < 8; y++)
        file[0x402 + y] = 0xff;
    set_load(file, 0x5c00);
    decode_ok(9218, NULL, &image, 320);
    assert(at(&image, 0, 0) == 2 && at(&image, 0, 8) == 1);
    c64_free(&image);
    set_load(file, 0x2000);
    decode_ok(9218, NULL, &image, 320);
    assert(at(&image, 0, 0) == 3);
    c64_free(&image);
    decode_ok(9218, "x.dd", &image, 320);
    assert(at(&image, 0, 0) == 2);
    c64_free(&image);
}

static void test_sizes(void)
{
    static const struct { const char *name; size_t size; } fixed[] = {
        { "a.koa", 10001 }, { "a.kla", 10003 }, { "a.bpl", 10242 }, { "a.ocp", 10018 },
        { "a.vid", 10050 }, { "a.p64", 10050 }, { "a.cdu", 10277 },
        { "a.che", 20482 }, { "a.ism", 10218 }, { "a.sar", 10219 }, { "a.pmg", 9332 },
        { "a.art", 9002 }, { "a.hed", 9194 }, { "a.dd", 9026 }, { "a.hbm", 8002 },
        { "a.afl", 16385 }, { "a.fd2", 17218 }, { "a.bml", 17474 }, { "a.flm", 17410 },
        { "a.mci", 19434 }, { "a.fp", 19266 }, { "a.gun", 33602 }, { "a.hlf", 24578 },
        { "a.hle", 32770 }, { "a.ihe", 16194 }, { "a.vhi", 17389 }, { "a.drz", 10051 },
        { "a.drl", 18242 }, { "a.mil", 10022 }, { "a.ffli", 26115 }
    };
    struct c64_image image;
    size_t i;

    memset(file, 0, sizeof file);
    file[2] = 'f';
    for (i = 0; i < sizeof fixed / sizeof fixed[0]; i++) {
        size_t n = fixed[i].size;
        unsigned width = strstr(".afl.fd2.bml.flm.gun.ffli", strchr(fixed[i].name, '.')) ? 296 : 320;
        decode_ok(n, fixed[i].name, &image, width);
        c64_free(&image);
        /* One byte short is truncated when the name says what it is, unless
           that length is another format's: content wins over the name.
           A short .dd is a packed Doodle. */
        if (c64_decode(file, n - 1, NULL, &image) == CODEC_OK) {
            c64_free(&image);
        } else if (strcmp(fixed[i].name, "a.dd") != 0) {
            assert(c64_decode(file, n - 1, fixed[i].name, &image) == CODEC_TRUNCATED);
            assert(image.rgba == NULL && image.width == 0);
        }
        /* An unlisted size is invalid without a name. */
        assert(c64_decode(file, n + 97, NULL, &image) == CODEC_INVALID);
    }
    /* Content alone finds every size above; a few are listed here. */
    decode_ok(10003, NULL, &image, 320);
    c64_free(&image);
    decode_ok(17409, NULL, &image, 296);
    c64_free(&image);
    decode_ok(9009, NULL, &image, 320);
    c64_free(&image);
    /* Packed formats with no signature need their extension. */
    assert(c64_decode(file, 5000, NULL, &image) == CODEC_INVALID);
    assert(c64_decode(file, 5000, "a.txt", &image) == CODEC_INVALID);
    assert(c64_decode(file, 1, "a.koa", &image) == CODEC_TRUNCATED);
    assert(c64_decode(file, 0, NULL, &image) == CODEC_TRUNCATED);
    assert(c64_decode(NULL, 10003, NULL, &image) == CODEC_TRUNCATED);
    assert(c64_decode(file, C64_MAX_FILE + 1, NULL, &image) == CODEC_INVALID);
    assert(c64_decode(file, 10003, NULL, NULL) == CODEC_INVALID);
    /* Micro Illustrator's packed variants are not supported. */
    file[7] = 1;
    assert(c64_decode(file, 10022, "a.mil", &image) == CODEC_INVALID);
    file[7] = 0;
    /* Flash FLI needs its 'f'. */
    file[2] = 0;
    assert(c64_decode(file, 26115, "a.ffli", &image) == CODEC_INVALID);
}

static void set_pixel(unsigned x, unsigned y, unsigned colour)
{
    uint8_t *p = rgba + ((size_t)y * 320u + x) * 4u;
    c64_colour(colour, p);
    p[3] = 255;
}

static void encode_decode(size_t want_size, const char *name)
{
    struct c64_image image;
    size_t size;
    assert(c64_encode(rgba, 320, 200, saved, &size) == CODEC_OK);
    assert(size == want_size);
    memcpy(file, saved, size);
    decode_ok(size, NULL, &image, 320);
    assert(memcmp(image.rgba, rgba, sizeof rgba) == 0);
    c64_free(&image);
    decode_ok(size, name, &image, 320);
    assert(memcmp(image.rgba, rgba, sizeof rgba) == 0);
    c64_free(&image);
}

static void test_encode(void)
{
    static uint8_t first[C64_ENCODE_MAX];
    unsigned x, y, round;
    size_t size;

    /* Random multicolour pictures: background 0 plus three colours per cell. */
    for (round = 0; round < 10; round++) {
        unsigned bg = rnd() % 16u;
        for (y = 0; y < 200; y++)
            for (x = 0; x < 320; x += 2) {
                unsigned cell = y / 8u * 40u + x / 8u, pick = rnd() % 4u, c;
                c = pick == 0 ? bg : (cell * 7u + pick * 5u + round) % 16u;
                set_pixel(x, y, c);
                set_pixel(x + 1, y, c);
            }
        encode_decode(C64_KOALA_SIZE, "x.koa");
        assert(saved[0] == 0x00 && saved[1] == 0x60);
        /* Saving the decoded picture again gives the same bytes. */
        memcpy(first, saved, C64_KOALA_SIZE);
        encode_decode(C64_KOALA_SIZE, "x.koa");
        assert(memcmp(first, saved, C64_KOALA_SIZE) == 0);
    }
    /* Random hires pictures: two colours per cell. */
    for (round = 0; round < 10; round++) {
        for (y = 0; y < 200; y++)
            for (x = 0; x < 320; x++) {
                unsigned cell = y / 8u * 40u + x / 8u;
                set_pixel(x, y, rnd() % 2u ? (cell + round) % 16u : (cell * 3u) % 16u);
            }
        encode_decode(C64_ART_STUDIO_SIZE, "x.art");
        assert(saved[0] == 0x00 && saved[1] == 0x20);
    }
    /* Paired pixels that don't fit Koala but do fit hires. */
    for (y = 0; y < 200; y++)
        for (x = 0; x < 320; x++)
            set_pixel(x, y, x / 8u % 2u ? 1 : 2);
    for (y = 0; y < 8; y++) {
        set_pixel(0, y, 3);
        set_pixel(1, y, 3);
    }
    /* Cell 0 has 2 and 3; others one colour of 1 or 2. Both fit Koala. */
    encode_decode(C64_KOALA_SIZE, "x.koa");
    /* Four colours in two cells with nothing in common: hires can't, Koala can't. */
    for (x = 0; x < 8; x += 2) {
        set_pixel(x, 0, x / 2u + 4u);
        set_pixel(x + 1, 0, x / 2u + 4u);
        set_pixel(8 + x, 0, x / 2u + 8u);
        set_pixel(9 + x, 0, x / 2u + 8u);
    }
    assert(c64_encode(rgba, 320, 200, saved, &size) == CODEC_INVALID && size == 0);
    /* Unpaired pixels with three colours in a cell. */
    for (y = 0; y < 200; y++)
        for (x = 0; x < 320; x++)
            set_pixel(x, y, 0);
    set_pixel(0, 0, 1);
    set_pixel(3, 0, 2);
    assert(c64_encode(rgba, 320, 200, saved, &size) == CODEC_INVALID);
    set_pixel(3, 0, 1);
    encode_decode(C64_ART_STUDIO_SIZE, "x.art");
    /* Transparent pixels become white; half-transparent ones aren't Pepto. */
    rgba[3] = 0;
    assert(c64_encode(rgba, 320, 200, saved, &size) == CODEC_OK);
    memcpy(file, saved, size);
    {
        struct c64_image image;
        decode_ok(size, NULL, &image, 320);
        assert(at(&image, 0, 0) == 1);
        c64_free(&image);
    }
    rgba[3] = 255;
    rgba[4 + 3] = 128;  /* black at half alpha is grey */
    assert(c64_encode(rgba, 320, 200, saved, &size) == CODEC_INVALID);
    rgba[4 + 3] = 255;
    rgba[0] ^= 1;
    assert(c64_encode(rgba, 320, 200, saved, &size) == CODEC_INVALID);
    rgba[0] ^= 1;
    assert(c64_encode(rgba, 296, 200, saved, &size) == CODEC_INVALID);
    assert(c64_encode(rgba, 320, 199, saved, &size) == CODEC_INVALID);
    assert(c64_encode(NULL, 320, 200, saved, &size) == CODEC_INVALID);
    assert(c64_encode(rgba, 320, 200, NULL, &size) == CODEC_INVALID);
    assert(c64_encode(rgba, 320, 200, saved, NULL) == CODEC_INVALID);
}

int main(void)
{
    test_palette();
    test_hires();
    test_multicolour();
    test_fli();
    test_interlace();
    test_packed();
    test_recognition();
    test_sizes();
    test_encode();
    puts("c64: all tests passed");
    return 0;
}
