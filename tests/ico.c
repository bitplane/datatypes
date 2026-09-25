#include "../formats/ico/decode.h"
#include "../formats/ico/encode.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t file[1 << 20];

static void le16(uint8_t *p, unsigned value)
{
    p[0] = (uint8_t)value; p[1] = (uint8_t)(value >> 8);
}

static void le32(uint8_t *p, unsigned long value)
{
    le16(p, (unsigned)(value & 0xffff)); le16(p + 2, (unsigned)(value >> 16));
}

/* A BITMAPINFOHEADER with a doubled height. Returns its size. */
static size_t info(uint8_t *p, unsigned header, long width, long height,
                   unsigned bpp, unsigned compression, unsigned used)
{
    memset(p, 0, header);
    le32(p, header); le32(p + 4, (unsigned long)width); le32(p + 8, (unsigned long)(height * 2));
    le16(p + 12, 1); le16(p + 14, bpp); le32(p + 16, compression); le32(p + 32, used);
    return header;
}

/* An icon file with one directory entry per image. Returns its length. */
static size_t icon(unsigned type, unsigned count, const uint8_t *const *images,
                   const size_t *sizes)
{
    size_t pos = 6 + 16u * count;
    unsigned i;
    memset(file, 0, 6);
    le16(file + 2, type); le16(file + 4, count);
    for (i = 0; i < count; i++) {
        uint8_t *e = file + 6 + 16 * i;
        memset(e, 0, 16);
        le16(e + 4, 3 + i); le16(e + 6, 7 + i);
        le32(e + 8, sizes[i]); le32(e + 12, pos);
        memcpy(file + pos, images[i], sizes[i]);
        pos += sizes[i];
    }
    return pos;
}

static size_t one(unsigned type, const uint8_t *image, size_t size)
{
    return icon(type, 1, &image, &size);
}

static enum codec_result decode(const uint8_t *data, size_t length, unsigned index,
                                struct ico_image *image)
{
    struct ico_entry entry;
    enum codec_result result = ico_entry(data, length, index, &entry);
    image->rgba = NULL;
    if (result != CODEC_OK) {
        assert(entry.width == 0 && entry.height == 0);
        return result;
    }
    assert(!entry.png);
    result = ico_decode_bmp(data, length, &entry, image);
    if (result == CODEC_OK) {
        assert(image->width == entry.width && image->height == entry.height);
    } else {
        assert(image->rgba == NULL);
    }
    return result;
}

static void expect(size_t length, const uint8_t *pixels, unsigned width, unsigned height)
{
    struct ico_image image;
    assert(decode(file, length, 0, &image) == CODEC_OK);
    assert(image.width == width && image.height == height);
    if (memcmp(image.rgba, pixels, (size_t)width * height * 4u) != 0) {
        size_t i;
        for (i = 0; i < (size_t)width * height * 4u; i++)
            fprintf(stderr, "%02x%s", image.rgba[i], i % 4 == 3 ? " " : "");
        fprintf(stderr, "\n");
        assert(0);
    }
    ico_free(&image);
}

static enum codec_result result(size_t length)
{
    struct ico_image image;
    enum codec_result r = decode(file, length, 0, &image);
    if (r == CODEC_OK)
        ico_free(&image);
    return r;
}

/* Cutting the file short is truncation. The entry may end where its pixels do,
   leaving out the AND mask. */
static void truncations(size_t length, size_t mask_start)
{
    size_t cut;
    for (cut = 0; cut < length; cut++)
        assert(result(cut) == CODEC_TRUNCATED);
    /* A directory size too short even for the pixels is ignored, as other
       readers do, so the whole mask is read. Between the pixels and the end
       of the mask, it cuts the mask short. */
    for (cut = 0; cut < length - 22; cut++) {
        le32(file + 14, cut);
        assert(result(length) == (cut > mask_start ? CODEC_TRUNCATED : CODEC_OK));
    }
    le32(file + 14, 0);
    assert(result(length - 1) == CODEC_TRUNCATED);
    le32(file + 14, length - 22);
    assert(result(length) == CODEC_OK);
}

static void palette_tests(void)
{
    uint8_t img[512];
    size_t n;
    /* 2x2, 1-bit: rows bottom up, each padded to 4 bytes. */
    const uint8_t mono[16] = {
        0x11,0x22,0x33,255, 0xaa,0xbb,0xcc,0,
        0xaa,0xbb,0xcc,255, 0x11,0x22,0x33,255};
    n = info(img, 40, 2, 2, 1, 0, 0);
    memcpy(img + n, "\x33\x22\x11\0\xcc\xbb\xaa\0", 8); n += 8;
    memcpy(img + n, "\x80\0\0\0" "\x40\0\0\0", 8); n += 8;   /* pixels */
    memcpy(img + n, "\0\0\0\0" "\x40\0\0\0", 8); n += 8;     /* mask */
    expect(one(1, img, n), mono, 2, 2);
    truncations(one(1, img, n), n - 8);

    /* 4-bit with a short palette: indexes past it are black. */
    {
        const uint8_t out[12] = {0x10,0x20,0x30,255, 0,0,0,255, 0x40,0x50,0x60,255};
        n = info(img, 40, 3, 1, 4, 0, 2);
        memcpy(img + n, "\x30\x20\x10\0\x60\x50\x40\0", 8); n += 8;
        memcpy(img + n, "\x05\x10\0\0", 4); n += 4;
        memcpy(img + n, "\0\0\0\0", 4); n += 4;
        expect(one(1, img, n), out, 3, 1);
    }

}

static void big_tests(void)
{
    static uint8_t img[4096];
    const uint8_t out[8] = {1,2,3,255, 4,5,6,255};
    size_t n = info(img, 40, 2, 1, 8, 0, 256);
    memset(img + n, 0, 1024);
    memcpy(img + n + 7 * 4, "\3\2\1\0", 4);
    memcpy(img + n + 200 * 4, "\6\5\4\0", 4);
    n += 1024;
    memcpy(img + n, "\x07\xc8\0\0" "\0\0\0\0", 8); n += 8;
    expect(one(1, img, n), out, 2, 1);
    /* Too many palette entries. */
    le32(img + 32, 257);
    assert(result(one(1, img, n)) == CODEC_INVALID);

    /* The OS/2 core header has 3-byte palette entries. */
    n = 0;
    le32(img, 12); le16(img + 4, 2); le16(img + 6, 2); le16(img + 8, 1); le16(img + 10, 1);
    n = 12;
    memcpy(img + n, "\3\2\1\6\5\4", 6); n += 6;
    memcpy(img + n, "\x40\0\0\0", 4); n += 4;
    memcpy(img + n, "\0\0\0\0", 4); n += 4;
    {
        const uint8_t core[8] = {1,2,3,255, 4,5,6,255};
        expect(one(1, img, n), core, 2, 1);
    }
    le16(img + 10, 32);
    assert(result(one(1, img, n)) == CODEC_INVALID);
}

static void true_colour_tests(void)
{
    uint8_t img[512];
    size_t n;

    /* 24-bit, 2x2, with the top-left pixel masked out. */
    {
        const uint8_t out[16] = {1,2,3,0, 4,5,6,255, 7,8,9,255, 10,11,12,255};
        n = info(img, 40, 2, 2, 24, 0, 0);
        memcpy(img + n, "\x09\x08\x07\x0c\x0b\x0a\0\0", 8); n += 8;
        memcpy(img + n, "\x03\x02\x01\x06\x05\x04\0\0", 8); n += 8;
        memcpy(img + n, "\0\0\0\0" "\x80\0\0\0", 8); n += 8;
        expect(one(1, img, n), out, 2, 2);
        truncations(one(1, img, n), n - 8);
        /* Without its AND mask the image is opaque. */
        {
            const uint8_t opaque[16] = {1,2,3,255, 4,5,6,255, 7,8,9,255, 10,11,12,255};
            expect(one(1, img, n - 8), opaque, 2, 2);
        }
        /* A partial mask is truncation. */
        assert(result(one(1, img, n - 1)) == CODEC_TRUNCATED);
        /* Extra bytes after the mask are allowed. */
        expect(one(1, img, n + 3), out, 2, 2);
        /* A larger header (V5) is skipped. */
        {
            uint8_t v5[600];
            size_t m = info(v5, 124, 2, 2, 24, 0, 0);
            memcpy(v5 + m, img + 40, n - 40);
            expect(one(1, v5, m + n - 40), out, 2, 2);
        }
    }

    /* 32-bit alpha wins over the mask. */
    {
        const uint8_t out[8] = {1,2,3,0x80, 4,5,6,0};
        n = info(img, 40, 2, 1, 32, 0, 0);
        memcpy(img + n, "\x03\x02\x01\x80\x06\x05\x04\x00", 8); n += 8;
        memcpy(img + n, "\x80\0\0\0", 4); n += 4;
        expect(one(1, img, n), out, 2, 1);
        /* All-zero alpha means the mask applies. */
        {
            const uint8_t masked[8] = {1,2,3,0, 4,5,6,255};
            img[40 + 3] = 0;
            expect(one(1, img, n), masked, 2, 1);
            /* And with no mask, opaque. */
            {
                const uint8_t opaque[8] = {1,2,3,255, 4,5,6,255};
                expect(one(1, img, n - 4), opaque, 2, 1);
            }
        }
    }

    /* 16-bit 5:5:5, scaled to 8 bits. */
    {
        const uint8_t out[8] = {255,0,0,255, 0,0x84,0x10,255};
        n = info(img, 40, 2, 1, 16, 0, 0);
        le16(img + n, 0x7c00); le16(img + n + 2, 0x0202); n += 4;
        memcpy(img + n, "\0\0\0\0", 4); n += 4;
        expect(one(1, img, n), out, 2, 1);
    }

    /* 16-bit 5:6:5 bitfields after a 40-byte header. */
    {
        const uint8_t out[8] = {0,255,0,255, 0x08,0,0xff,255};
        n = info(img, 40, 2, 1, 16, 3, 0);
        le32(img + n, 0xf800); le32(img + n + 4, 0x07e0); le32(img + n + 8, 0x001f); n += 12;
        le16(img + n, 0x07e0); le16(img + n + 2, 0x081f); n += 4;
        memcpy(img + n, "\0\0\0\0", 4); n += 4;
        expect(one(1, img, n), out, 2, 1);
        /* Bitfields on a palette image are invalid. */
        le16(img + 14, 8);
        assert(result(one(1, img, n)) == CODEC_INVALID);
    }

    /* 32-bit bitfields: the byte the colour masks leave is alpha. */
    {
        const uint8_t out[4] = {0x10,0x20,0x30,0x40};
        n = info(img, 40, 1, 1, 32, 3, 0);
        le32(img + n, 0xff0000); le32(img + n + 4, 0xff00); le32(img + n + 8, 0xff); n += 12;
        le32(img + n, 0x40102030); n += 4;
        memcpy(img + n, "\0\0\0\0", 4); n += 4;
        expect(one(1, img, n), out, 1, 1);
        /* In a V3 header, the masks are inside it and alpha is explicit. */
        {
            const uint8_t out2[4] = {0x10,0x20,0x30,255};
            n = info(img, 56, 1, 1, 32, 3, 0);
            le32(img + 40, 0xff0000); le32(img + 44, 0xff00); le32(img + 48, 0xff);
            le32(img + 52, 0);
            /* 0xff000000 is free, so it is still taken as alpha. */
            le32(img + n, 0xff102030); n += 4;
            memcpy(img + n, "\0\0\0\0", 4); n += 4;
            expect(one(1, img, n), out2, 1, 1);
        }
    }
}

static void header_tests(void)
{
    uint8_t img[256];
    size_t n = info(img, 40, 1, 1, 24, 0, 0), len;
    memcpy(img + n, "\1\2\3\0" "\0\0\0\0", 8); n += 8;
    len = one(1, img, n);
    assert(result(len) == CODEC_OK);

    /* Reserved and type fields. */
    file[0] = 1; assert(result(len) == CODEC_INVALID); file[0] = 0;
    le16(file + 2, 0); assert(result(len) == CODEC_INVALID);
    le16(file + 2, 3); assert(result(len) == CODEC_INVALID);
    le16(file + 2, 1);
    le16(file + 4, 0); assert(result(len) == CODEC_INVALID);
    /* A directory longer than the file. */
    le16(file + 4, 5); assert(result(len) == CODEC_TRUNCATED);
    le16(file + 4, 1);
    /* Out of range indexes. */
    {
        struct ico_image image;
        assert(decode(file, len, 1, &image) == CODEC_INVALID);
        assert(decode(file, len, 65535, &image) == CODEC_INVALID);
    }
    /* Entry offsets and sizes that overflow or leave the file. */
    le32(file + 18, 0xffffffffu); assert(result(len) == CODEC_TRUNCATED);
    le32(file + 18, 22);
    le32(file + 14, 0xfffffff0u); assert(result(len) == CODEC_TRUNCATED);
    le32(file + 14, (unsigned long)n);

    /* Reserved header values. */
    le32(file + 22, 20); assert(result(len) == CODEC_INVALID);
    le32(file + 22, 40);
    le16(file + 22 + 14, 2); assert(result(len) == CODEC_INVALID);
    le16(file + 22 + 14, 0); assert(result(len) == CODEC_INVALID);
    le16(file + 22 + 14, 24);
    le32(file + 22 + 16, 1); assert(result(len) == CODEC_INVALID);   /* RLE8 */
    le32(file + 22 + 16, 4); assert(result(len) == CODEC_INVALID);   /* JPEG */
    le32(file + 22 + 16, 0);
    /* Sizes: zero, negative (top-down) and too large. */
    le32(file + 22 + 4, 0); assert(result(len) == CODEC_INVALID);
    le32(file + 22 + 4, 0xffffffffu); assert(result(len) == CODEC_INVALID);
    le32(file + 22 + 4, 70000); assert(result(len) == CODEC_TOO_LARGE);
    le32(file + 22 + 4, 5000);
    le32(file + 22 + 8, 2 * 5000); assert(result(len) == CODEC_TOO_LARGE);
    le32(file + 22 + 4, 1);
    le32(file + 22 + 8, 1); assert(result(len) == CODEC_INVALID);
    le32(file + 22 + 8, (unsigned long)-2L); assert(result(len) == CODEC_INVALID);
    le32(file + 22 + 8, 2);
    /* Plausible size, but the pixels aren't there. */
    le32(file + 22 + 4, 4000); le32(file + 22 + 8, 8000);
    assert(result(len) == CODEC_TRUNCATED);
    le32(file + 22 + 4, 1); le32(file + 22 + 8, 2);
    assert(result(len) == CODEC_OK);

    /* A bitfields header must contain all three colour masks. */
    n = info(img, 48, 1, 1, 16, 3, 0);
    len = one(1, img, n);
    assert(result(len) == CODEC_INVALID);
}

static size_t png(uint8_t *p, unsigned long width, unsigned long height,
                  unsigned bits, unsigned type)
{
    memcpy(p, "\x89PNG\r\n\x1a\n\0\0\0\x0dIHDR", 16);
    p[16] = (uint8_t)(width >> 24); p[17] = (uint8_t)(width >> 16);
    p[18] = (uint8_t)(width >> 8); p[19] = (uint8_t)width;
    p[20] = (uint8_t)(height >> 24); p[21] = (uint8_t)(height >> 16);
    p[22] = (uint8_t)(height >> 8); p[23] = (uint8_t)height;
    p[24] = (uint8_t)bits; p[25] = (uint8_t)type;
    memset(p + 26, 0, 20);
    return 46;
}

static void directory_tests(void)
{
    uint8_t a[128], b[128], c[128], d[64];
    const uint8_t *images[4] = {a, b, c, d};
    size_t sizes[4], len;
    struct ico_entry entry;
    unsigned index, count;
    int cursor;

    /* a: 1x1 24-bit BMP; b: 2x1 8-bit PNG palette; c: 2x1 RGBA PNG; d: 2x1 PNG, cut off. */
    sizes[0] = info(a, 40, 1, 1, 24, 0, 0);
    memcpy(a + sizes[0], "\1\2\3\0" "\0\0\0\0", 8); sizes[0] += 8;
    sizes[1] = png(b, 2, 1, 8, 3);
    sizes[2] = png(c, 2, 1, 8, 6);
    sizes[3] = 20;
    memcpy(d, c, 20);
    len = icon(2, 4, images, sizes);

    assert(ico_directory(file, len, &count, &cursor) == CODEC_OK);
    assert(count == 4 && cursor);
    assert(ico_entry(file, len, 0, &entry) == CODEC_OK);
    assert(!entry.png && entry.width == 1 && entry.depth == 24);
    assert(entry.hot_x == 3 && entry.hot_y == 7);
    assert(ico_entry(file, len, 1, &entry) == CODEC_OK);
    assert(entry.png && entry.width == 2 && entry.height == 1 && entry.depth == 8);
    assert(entry.offset == 6 + 64 + sizes[0] && entry.size == sizes[1]);
    assert(entry.hot_x == 4 && entry.hot_y == 8);
    assert(ico_entry(file, len, 2, &entry) == CODEC_OK);
    assert(entry.png && entry.depth == 32);
    assert(ico_entry(file, len, 3, &entry) == CODEC_TRUNCATED);
    /* Largest, then deepest, skipping unreadable entries. */
    assert(ico_best(file, len, &index) == CODEC_OK && index == 2);
    /* A PNG entry is not decoded here. */
    {
        struct ico_image image;
        assert(ico_entry(file, len, 2, &entry) == CODEC_OK);
        assert(ico_decode_bmp(file, len, &entry, &image) == CODEC_INVALID);
    }
    /* Icons have no hotspot. */
    le16(file + 2, 1);
    assert(ico_entry(file, len, 0, &entry) == CODEC_OK && entry.hot_x == 0 && entry.hot_y == 0);

    /* The first of equal entries wins. */
    images[1] = c;
    sizes[1] = sizes[2];
    len = icon(1, 3, images, sizes);
    assert(ico_best(file, len, &index) == CODEC_OK && index == 1);

    /* Bad PNG headers. */
    png(b, 2, 1, 3, 3);
    images[1] = b; sizes[1] = 46;
    len = icon(1, 2, images, sizes);
    assert(ico_entry(file, len, 1, &entry) == CODEC_INVALID);
    png(b, 2, 1, 8, 5);
    len = icon(1, 2, images, sizes);
    assert(ico_entry(file, len, 1, &entry) == CODEC_INVALID);
    png(b, 0, 1, 8, 2);
    len = icon(1, 2, images, sizes);
    assert(ico_entry(file, len, 1, &entry) == CODEC_INVALID);
    png(b, 70000, 1, 8, 2);
    len = icon(1, 2, images, sizes);
    assert(ico_entry(file, len, 1, &entry) == CODEC_TOO_LARGE);
    png(b, 2, 1, 8, 2);
    memcpy(b + 12, "IDAT", 4);
    len = icon(1, 2, images, sizes);
    assert(ico_entry(file, len, 1, &entry) == CODEC_INVALID);
    /* Only the BMP is readable, so it is the best. */
    assert(ico_best(file, len, &index) == CODEC_OK && index == 0);
    /* With nothing readable, the first error is reported. */
    images[0] = d; sizes[0] = 20;
    len = icon(1, 1, images, sizes);
    assert(ico_best(file, len, &index) == CODEC_TRUNCATED);
    images[1] = d; sizes[1] = 20;
    le32(b + 16, 0);
    images[0] = b; sizes[0] = 46;
    len = icon(1, 2, images, sizes);
    assert(ico_best(file, len, &index) == CODEC_INVALID);
    assert(ico_best(file, 3, &index) == CODEC_TRUNCATED);
}

static void encode_tests(void)
{
    static uint8_t rgba[256 * 256 * 4], out[300000];
    struct ico_image image;
    size_t size, i;
    unsigned w, h;

    assert(ico_encode_capacity(0, 1) == 0);
    assert(ico_encode_capacity(257, 1) == 0);
    assert(ico_encode_capacity(256, 256) != 0);

    for (w = 1; w <= 33; w += 8) {
        for (h = 1; h <= 3; h++) {
            for (i = 0; i < (size_t)w * h; i++) {
                rgba[i * 4] = (uint8_t)(i * 7); rgba[i * 4 + 1] = (uint8_t)(i * 13);
                rgba[i * 4 + 2] = (uint8_t)(i * 29); rgba[i * 4 + 3] = 255;
            }
            /* Opaque: 24-bit. */
            size = ico_encode(rgba, w, h, 0, 0, 0, out, sizeof out);
            assert(size != 0 && out[6 + 6] == 24 && out[2] == 1);
            memcpy(file, out, size);
            assert(decode(file, size, 0, &image) == CODEC_OK);
            assert(memcmp(image.rgba, rgba, (size_t)w * h * 4) == 0);
            ico_free(&image);
            /* Transparent: 32-bit, and a cursor. */
            rgba[3] = 0; rgba[((size_t)w * h - 1) * 4 + 3] = 0x40;
            size = ico_encode(rgba, w, h, 1, w - 1, h - 1, out, sizeof out);
            assert(size != 0 && out[2] == 2);
            memcpy(file, out, size);
            {
                struct ico_entry entry;
                assert(ico_entry(file, size, 0, &entry) == CODEC_OK);
                assert(entry.depth == 32 && entry.hot_x == w - 1 && entry.hot_y == h - 1);
            }
            assert(decode(file, size, 0, &image) == CODEC_OK);
            assert(memcmp(image.rgba, rgba, (size_t)w * h * 4) == 0);
            ico_free(&image);
            /* Too small a buffer. */
            assert(ico_encode(rgba, w, h, 0, 0, 0, out, size - 1) == 0);
        }
    }
    /* 256 is stored as 0 in the directory. */
    memset(rgba, 0x80, sizeof rgba);
    size = ico_encode(rgba, 256, 256, 0, 0, 0, out, sizeof out);
    assert(size == ico_encode_capacity(256, 256) && out[6] == 0 && out[7] == 0);
    memcpy(file, out, size);
    assert(decode(file, size, 0, &image) == CODEC_OK && image.width == 256);
    ico_free(&image);
    assert(ico_encode(rgba, 257, 1, 0, 0, 0, out, sizeof out) == 0);
}

int main(void)
{
    palette_tests();
    big_tests();
    true_colour_tests();
    header_tests();
    directory_tests();
    encode_tests();
    puts("ico tests passed");
    return 0;
}
