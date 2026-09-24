#include "../formats/xwd/decode.h"
#include "../formats/xwd/encode.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    H_SIZE, H_VERSION, H_FORMAT, H_DEPTH, H_WIDTH, H_HEIGHT, H_XOFFSET,
    H_BYTE_ORDER, H_UNIT, H_BIT_ORDER, H_PAD, H_BPP, H_BPL, H_VISUAL,
    H_RED, H_GREEN, H_BLUE, H_BITS_PER_RGB, H_CMAP_ENTRIES, H_NCOLORS
};

static uint8_t data[4096];
static size_t used;
static int little;

static void put32(uint8_t *p, unsigned long value)
{
    unsigned i;
    for (i = 0; i < 4; i++)
        p[little ? i : 3u - i] = (uint8_t)(value >> (8u * i));
}

static void set(unsigned field, unsigned long value)
{
    put32(data + 4u * field, value);
}

/* A ZPixmap header with a 4-byte window name; colours and raster follow. */
static void header(unsigned long format, unsigned long depth, unsigned long bpp,
                   unsigned long width, unsigned long height, unsigned long bpl,
                   unsigned long visual)
{
    memset(data, 0, sizeof data);
    set(H_SIZE, 104); set(H_VERSION, 7); set(H_FORMAT, format);
    set(H_DEPTH, depth); set(H_WIDTH, width); set(H_HEIGHT, height);
    set(H_BYTE_ORDER, 1); set(H_UNIT, 8); set(H_BIT_ORDER, 1); set(H_PAD, 8);
    set(H_BPP, bpp); set(H_BPL, bpl); set(H_VISUAL, visual);
    memcpy(data + 100, "win", 4);
    used = 104;
}

static void color(unsigned r, unsigned g, unsigned b)
{
    uint8_t *p = data + used;
    unsigned long n = (used - 104u) / 12u;
    put32(p, n);
    p[little ? 5 : 4] = (uint8_t)(r >> 8); p[little ? 4 : 5] = (uint8_t)r;
    p[little ? 7 : 6] = (uint8_t)(g >> 8); p[little ? 6 : 7] = (uint8_t)g;
    p[little ? 9 : 8] = (uint8_t)(b >> 8); p[little ? 8 : 9] = (uint8_t)b;
    p[10] = 7;
    used += 12;
    set(H_NCOLORS, n + 1u);
    set(H_CMAP_ENTRIES, n + 1u);
}

static void bytes(const char *raster, size_t size)
{
    memcpy(data + used, raster, size);
    used += size;
}

static void masks(unsigned long r, unsigned long g, unsigned long b)
{
    set(H_RED, r); set(H_GREEN, g); set(H_BLUE, b);
}

static enum codec_result decode(size_t length)
{
    struct xwd_image image;
    enum codec_result result = xwd_decode(data, length, &image);
    if (result == CODEC_OK) {
        assert(image.rgba != NULL);
        xwd_free(&image);
    } else {
        assert(image.rgba == NULL && image.width == 0 && image.height == 0);
    }
    return result;
}

/* Expect pixels given as 0xRRGGBB, all opaque. */
static void expect(unsigned width, unsigned height, const unsigned long *rgb)
{
    struct xwd_image image;
    size_t i;
    assert(xwd_decode(data, used, &image) == CODEC_OK);
    assert(image.width == width && image.height == height);
    for (i = 0; i < (size_t)width * height; i++) {
        const uint8_t *p = image.rgba + i * 4u;
        if (p[0] != (uint8_t)(rgb[i] >> 16) || p[1] != (uint8_t)(rgb[i] >> 8) ||
            p[2] != (uint8_t)rgb[i] || p[3] != 255) {
            fprintf(stderr, "pixel %zu: %02x%02x%02x%02x, want %06lx\n",
                    i, p[0], p[1], p[2], p[3], rgb[i]);
            assert(0);
        }
    }
    xwd_free(&image);
    /* Every shorter file is truncated. */
    assert(decode(used - 1u) == CODEC_TRUNCATED);
}

static void test_pseudocolor(void)
{
    const unsigned long want[6] = {0xff0000, 0x00ff00, 0x0000ff,
                                   0x0000ff, 0xff0000, 0x808080};
    for (little = 0; little < 2; little++) {
        header(2, 8, 8, 3, 2, 4, 3);
        color(0xffff, 0, 0); color(0, 0xffff, 0); color(0, 0, 0xffff);
        color(0x8080, 0x807f, 0x80ff);
        bytes("\0\1\2\x55" "\2\0\3\x55", 8);
        expect(3, 2, want);
        /* The colormap's pixel field is ignored; entries index by position. */
        put32(data + 104, 9);
        expect(3, 2, want);
        /* A pixel past the colormap's end is invalid. */
        data[used - 8] = 4;
        assert(decode(used) == CODEC_INVALID);
        /* A header, name or colormap cut short is truncated. */
        assert(decode(99) == CODEC_TRUNCATED);
        assert(decode(103) == CODEC_TRUNCATED);
        assert(decode(104 + 47) == CODEC_TRUNCATED);
        assert(decode(7) == CODEC_TRUNCATED);
    }
    little = 0;
}

static void test_truecolor(void)
{
    const unsigned long want[2] = {0x123456, 0xabcdef};
    for (little = 0; little < 2; little++) {
        /* Depth 24 in 32 bits: the top byte is ignored, not alpha. */
        header(2, 24, 32, 2, 1, 8, 4);
        masks(0xff0000, 0xff00, 0xff);
        bytes("\x7f\x12\x34\x56" "\xff\xab\xcd\xef", 8);
        expect(2, 1, want);
        set(H_BYTE_ORDER, 0);
        memcpy(data + 104, "\x56\x34\x12\x7f" "\xef\xcd\xab\0", 8);
        expect(2, 1, want);
        /* BGR masks. */
        set(H_BYTE_ORDER, 1);
        masks(0xff, 0xff00, 0xff0000);
        memcpy(data + 104, "\0\x56\x34\x12" "\0\xef\xcd\xab", 8);
        expect(2, 1, want);

        /* Packed 24-bit pixels, both byte orders, padded to 32 bits. */
        header(2, 24, 24, 2, 1, 8, 5);
        masks(0xff0000, 0xff00, 0xff);
        set(H_PAD, 32);
        bytes("\x12\x34\x56\xab\xcd\xef\0\0", 8);
        expect(2, 1, want);
        set(H_BYTE_ORDER, 0);
        memcpy(data + 104, "\x56\x34\x12\xef\xcd\xab", 6);
        expect(2, 1, want);
        /* No colormap and zero masks leaves nothing to show. */
        masks(0, 0, 0);
        assert(decode(used) == CODEC_INVALID);
    }
    little = 0;

    /* 5-6-5 in 16 bits, least significant byte first; fields scale to 0-255. */
    {
        const unsigned long want565[3] = {0xffffff, 0x840000, 0x000400};
        header(2, 16, 16, 3, 1, 6, 4);
        masks(0xf800, 0x07e0, 0x001f);
        set(H_BYTE_ORDER, 0);
        bytes("\xff\xff" "\x00\x80" "\x20\x00", 6);
        expect(3, 1, want565);
    }
    /* A server's TrueColor colormap gives each field's colour, as xwud shows it. */
    {
        const unsigned long want332[1] = {0x405060};
        header(2, 8, 8, 1, 1, 1, 4);
        masks(0x07, 0x38, 0xc0);
        color(0x6060, 0, 0x6060); color(0, 0x5050, 0); color(0x4040, 0, 0);
        bytes("\x0a", 1);
        expect(1, 1, want332);
        data[used - 1] = 0x0b;
        assert(decode(used) == CODEC_INVALID);
    }
    /* Depth 15 in 16 bits masks off the top bit. */
    {
        const unsigned long want555[1] = {0xff0000};
        header(2, 15, 16, 1, 1, 2, 4);
        masks(0x7c00, 0x03e0, 0x001f);
        bytes("\xfc\x00", 2);
        expect(1, 1, want555);
    }
}

static void test_directcolor(void)
{
    /* With a colormap, each channel's field indexes its own column. */
    const unsigned long want[2] = {0xff0080, 0x00ff00};
    header(2, 6, 8, 2, 1, 2, 5);
    masks(0x30, 0x0c, 0x03);
    color(0, 0, 0); color(0xffff, 0xffff, 0x8080); color(0x4040, 0x4040, 0x4040);
    bytes("\x11\x04", 2);
    set(H_BPL, 2);
    expect(2, 1, want);
    data[used - 1] = 0x0c;
    assert(decode(used) == CODEC_INVALID);
    /* Without one, fields scale by their masks, as ImageMagick writes. */
    {
        const unsigned long scaled[2] = {0x550000, 0x00ffaa};
        header(2, 6, 8, 2, 1, 2, 5);
        masks(0x30, 0x0c, 0x03);
        bytes("\x10\x0e", 2);
        expect(2, 1, scaled);
    }
}

static void test_gray(void)
{
    /* 8-bit StaticGray without a colormap is a ramp. */
    {
        const unsigned long want[3] = {0, 0x808080, 0xffffff};
        header(2, 8, 8, 3, 1, 3, 0);
        bytes("\0\x80\xff", 3);
        expect(3, 1, want);
    }
    /* 4-bit: byte order picks the nibble for the leftmost pixel. */
    {
        const unsigned long msb[3] = {0x111111, 0xeeeeee, 0x333333};
        const unsigned long lsb[3] = {0xeeeeee, 0x111111, 0x000000};
        header(2, 4, 4, 3, 1, 2, 1);
        bytes("\x1e\x30", 2);
        expect(3, 1, msb);
        set(H_BYTE_ORDER, 0);
        expect(3, 1, lsb);
    }
    /* 1-bit without a colormap: 1 is black. Rows pad to bitmap_pad. */
    {
        const unsigned long want[6] = {0, 0xffffff, 0, 0xffffff, 0xffffff, 0};
        header(2, 1, 1, 3, 2, 2, 0);
        set(H_PAD, 16);
        bytes("\xa0\0" "\x20\0", 4);
        expect(3, 2, want);
        /* A zero bytes_per_line means the minimum. */
        set(H_BPL, 0);
        expect(3, 2, want);
        set(H_BPL, 1);
        assert(decode(used) == CODEC_INVALID);
        /* Least significant bit first. */
        set(H_BPL, 2);
        set(H_BIT_ORDER, 0);
        memcpy(data + 104, "\x05\0" "\x04\0", 4);
        expect(3, 2, want);
    }
    /* 32-bit units whose byte order differs from their bit order. */
    {
        const unsigned long want[9] = {0, 0xffffff, 0xffffff, 0xffffff,
                                       0xffffff, 0xffffff, 0xffffff, 0xffffff, 0};
        header(0, 1, 1, 9, 1, 4, 0);
        set(H_UNIT, 32); set(H_PAD, 32);
        set(H_BYTE_ORDER, 0); set(H_BIT_ORDER, 1);
        /* The unit is 0x80800000, stored least significant byte first. */
        bytes("\0\0\x80\x80", 4);
        expect(9, 1, want);
        /* An xoffset skips bits at the start of each row. */
        set(H_XOFFSET, 1);
        memcpy(data + 104, "\0\0\x40\x40", 4);
        expect(9, 1, want);
        set(H_XOFFSET, 24);
        assert(decode(used) == CODEC_INVALID);
    }
}

static void test_xy(void)
{
    /* XYBitmap with a colormap. */
    {
        const unsigned long want[2] = {0x0000ff, 0xffff00};
        header(0, 1, 1, 2, 1, 1, 3);
        color(0, 0, 0xffff); color(0xffff, 0xffff, 0);
        bytes("\x40", 1);
        expect(2, 1, want);
        set(H_DEPTH, 2);
        assert(decode(used) == CODEC_INVALID);
    }
    /* XYPixmap: two planes, the most significant first. */
    {
        const unsigned long want[4] = {0x000000, 0x555555, 0xaaaaaa, 0xffffff};
        header(1, 2, 2, 4, 1, 1, 0);
        bytes("\x30" "\x50", 2);
        expect(4, 1, want);
    }
    /* XYPixmap, depth 3, two rows of two pixels, through a colormap. */
    {
        const unsigned long want[4] = {0x000007, 0x000000, 0x000005, 0x000002};
        unsigned i;
        header(1, 3, 3, 2, 2, 1, 3);
        for (i = 0; i < 8; i++)
            color(0, 0, i * 257u);
        bytes("\x80\x80" "\x80\x40" "\x80\x80", 6);
        expect(2, 2, want);
    }
}

static void test_malformed(void)
{
    const unsigned long want[1] = {0x010203};
    header(2, 24, 32, 1, 1, 4, 4);
    masks(0xff0000, 0xff00, 0xff);
    bytes("\0\1\2\3", 4);
    expect(1, 1, want);

    set(H_VERSION, 6);
    assert(decode(used) == CODEC_INVALID);
    set(H_VERSION, 7);
    set(H_SIZE, 99);
    assert(decode(used) == CODEC_INVALID);
    set(H_SIZE, 109);
    assert(decode(used) == CODEC_TRUNCATED);
    set(H_SIZE, 0xfffffffful);
    assert(decode(used) == CODEC_TRUNCATED);
    set(H_SIZE, 104);
    set(H_NCOLORS, 0xfffffffful);
    assert(decode(used) == CODEC_TRUNCATED);
    set(H_NCOLORS, 0);
    set(H_BPL, 0xfffffffful);
    assert(decode(used) == CODEC_TRUNCATED);
    set(H_BPL, 3);
    assert(decode(used) == CODEC_INVALID);
    set(H_BPL, 4);
    assert(decode(used) == CODEC_OK);

    set(H_FORMAT, 3); assert(decode(used) == CODEC_INVALID); set(H_FORMAT, 2);
    set(H_VISUAL, 6); assert(decode(used) == CODEC_INVALID); set(H_VISUAL, 4);
    set(H_BYTE_ORDER, 2); assert(decode(used) == CODEC_INVALID); set(H_BYTE_ORDER, 1);
    set(H_BIT_ORDER, 2); assert(decode(used) == CODEC_INVALID); set(H_BIT_ORDER, 1);
    set(H_UNIT, 64); assert(decode(used) == CODEC_INVALID); set(H_UNIT, 8);
    set(H_PAD, 0); assert(decode(used) == CODEC_INVALID); set(H_PAD, 8);
    set(H_DEPTH, 0); assert(decode(used) == CODEC_INVALID);
    set(H_DEPTH, 33); assert(decode(used) == CODEC_INVALID); set(H_DEPTH, 24);
    set(H_BPP, 12); assert(decode(used) == CODEC_INVALID);
    set(H_BPP, 16); assert(decode(used) == CODEC_INVALID); set(H_BPP, 32);
    set(H_WIDTH, 0); assert(decode(used) == CODEC_INVALID);
    set(H_WIDTH, 65536); assert(decode(used) == CODEC_TOO_LARGE);
    set(H_WIDTH, 4097); set(H_HEIGHT, 4097); set(H_BPL, 0);
    assert(decode(used) == CODEC_TOO_LARGE);
    set(H_WIDTH, 1); set(H_HEIGHT, 0); assert(decode(used) == CODEC_INVALID);
    set(H_HEIGHT, 1);
    set(H_VISUAL, 3); assert(decode(used) == CODEC_INVALID);

    /* A huge xoffset can't wrap the row length. */
    header(0, 1, 1, 1, 1, 1, 0);
    set(H_XOFFSET, 0xfffffffful);
    bytes("\x80", 1);
    assert(decode(used) == CODEC_INVALID);
    set(H_BPL, 0);
    assert(decode(used) == CODEC_TRUNCATED);
    /* A deep XYPixmap needs every plane. */
    header(1, 32, 32, 1, 1, 1, 4);
    masks(0xff0000, 0xff00, 0xff);
    bytes("\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", 31);
    assert(decode(used) == CODEC_TRUNCATED);
}

static void test_encode(void)
{
    const uint8_t rgba[12] = {255,0,0,255, 10,20,30,255, 0,0,255,0};
    const unsigned long want[3] = {0xff0000, 0x0a141e, 0xffffff};
    uint8_t row[12];
    unsigned y;
    assert(!xwd_make_header(0, 1, data));
    assert(!xwd_make_header(65536, 1, data));
    assert(xwd_make_header(3, 2, data));
    used = XWD_WRITE_HEADER;
    assert(xwd_encode_row(rgba, 3, row, 11) == 0);
    for (y = 0; y < 2; y++) {
        assert(xwd_encode_row(rgba, 3, row, sizeof row) == 12);
        bytes((const char *)row, 12);
    }
    {
        unsigned long both[6];
        memcpy(both, want, sizeof want);
        memcpy(both + 3, want, sizeof want);
        expect(3, 2, both);
    }
}

int main(void)
{
    assert(xwd_decode(NULL, 0, NULL) == CODEC_INVALID);
    test_pseudocolor();
    test_truecolor();
    test_directcolor();
    test_gray();
    test_xy();
    test_malformed();
    test_encode();
    puts("xwd tests passed");
    return 0;
}
