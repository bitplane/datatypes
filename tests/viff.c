#include "../formats/viff/decode.h"
#include "../formats/viff/encode.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    F_WIDTH, F_HEIGHT, F_SUBROW, F_STARTX, F_STARTY, F_PIXSIZX, F_PIXSIZY,
    F_LOCATION, F_LOCATION_DIM, F_IMAGES, F_BANDS, F_STORAGE, F_ENCODE,
    F_MAP_SCHEME, F_MAP_TYPE, F_MAP_BANDS, F_MAP_ENTRIES, F_MAP_SUBROW,
    F_MAP_ENABLE, F_MAPS_PER_CYCLE, F_COLOR_MODEL
};

static uint8_t data[8192];
static size_t used, start;
static int little;

static void put32(uint8_t *p, unsigned long value)
{
    unsigned i;
    for (i = 0; i < 4; i++)
        p[little ? i : 3u - i] = (uint8_t)(value >> (8u * i));
}

/* Set a field of the header that starts at start. */
static void set(unsigned field, unsigned long value)
{
    put32(data + start + 520u + 4u * field, value);
}

/* Append a header for a raw, implicitly located image with no map. */
static void header(unsigned long width, unsigned long height,
                   unsigned long bands, unsigned long storage)
{
    start = used;
    memset(data + start, 0, 1024);
    data[start] = 0xab; data[start + 1] = 1; data[start + 2] = 1;
    data[start + 3] = 3; data[start + 4] = little ? 8 : 2;
    memcpy(data + start + 8, "comment", 7);
    set(F_WIDTH, width); set(F_HEIGHT, height);
    set(F_STARTX, 0xfffffffful); set(F_STARTY, 0xfffffffful);
    set(F_PIXSIZX, 0x3f800000ul); set(F_PIXSIZY, 0x3f800000ul);
    set(F_LOCATION, 1); set(F_IMAGES, 1); set(F_BANDS, bands);
    set(F_STORAGE, storage); set(F_MAP_ENABLE, 1);
    set(F_COLOR_MODEL, bands >= 3 ? 15 : 0);
    used += 1024;
}

static void map(unsigned long scheme, unsigned long bands, unsigned long entries)
{
    set(F_MAP_SCHEME, scheme); set(F_MAP_TYPE, 1);
    set(F_MAP_BANDS, bands); set(F_MAP_ENTRIES, entries);
}

static void bytes(const char *raster, size_t size)
{
    memcpy(data + used, raster, size);
    used += size;
}

static enum codec_result decode_at(size_t length, unsigned index)
{
    struct viff_image image;
    enum codec_result result = viff_decode(data, length, index, &image);
    if (result == CODEC_OK) {
        assert(image.rgba != NULL);
        viff_free(&image);
    } else {
        assert(image.rgba == NULL && image.width == 0 && image.height == 0);
    }
    return result;
}

static enum codec_result decode(size_t length)
{
    return decode_at(length, 0);
}

/* Expect image index to hold pixels given as 0xRRGGBBAA. */
static void expect_at(unsigned index, unsigned width, unsigned height,
                      const unsigned long *rgba)
{
    struct viff_image image;
    size_t i;
    assert(viff_decode(data, used, index, &image) == CODEC_OK);
    assert(image.width == width && image.height == height);
    for (i = 0; i < (size_t)width * height; i++) {
        const uint8_t *p = image.rgba + i * 4u;
        unsigned long got = (unsigned long)p[0] << 24 | (unsigned long)p[1] << 16 |
                            (unsigned long)p[2] << 8 | p[3];
        if (got != rgba[i]) {
            fprintf(stderr, "pixel %zu: %08lx, want %08lx\n", i, got, rgba[i]);
            assert(0);
        }
    }
    viff_free(&image);
}

/* Expect a single-image file; every shorter file is truncated. */
static void expect(unsigned width, unsigned height, const unsigned long *rgba)
{
    unsigned count;
    expect_at(0, width, height, rgba);
    assert(viff_count(data, used, &count) == CODEC_OK && count == 1);
    assert(decode(used - 1u) == CODEC_TRUNCATED);
    assert(decode(1023) == CODEC_TRUNCATED);
    assert(decode(1024) == CODEC_TRUNCATED);
    assert(decode_at(used, 1) == CODEC_INVALID);
}

static void test_gray(void)
{
    const unsigned long want[6] = {0x000000ff, 0x7f7f7fff, 0xffffffff,
                                   0x010101ff, 0x020202ff, 0x030303ff};
    for (little = 0; little < 2; little++) {
        used = 0;
        header(3, 2, 1, 1);
        bytes("\0\x7f\xff\1\2\3", 6);
        expect(3, 2, want);
        /* Any color model but these three is unsupported. */
        set(F_COLOR_MODEL, 1);
        expect(3, 2, want);
        set(F_COLOR_MODEL, 3);
        assert(decode(used) == CODEC_INVALID);
    }
    little = 0;
}

static void test_rgb(void)
{
    const unsigned long rgb[2] = {0x102030ff, 0x405060ff};
    const unsigned long rgba[2] = {0x10203000, 0x40506080};
    for (little = 0; little < 2; little++) {
        /* Bands are planes: all red, all green, all blue. */
        used = 0;
        header(2, 1, 3, 1);
        bytes("\x10\x40\x20\x50\x30\x60", 6);
        expect(2, 1, rgb);
        /* Color model none still means RGB. */
        set(F_COLOR_MODEL, 0);
        expect(2, 1, rgb);
        /* A declared alpha band is kept, even when it is zero. */
        used = 0;
        header(2, 1, 4, 1);
        bytes("\x10\x40\x20\x50\x30\x60\x00\x80", 8);
        expect(2, 1, rgba);
    }
    little = 0;
}

static void test_bits(void)
{
    /* Least significant bit first, 1 is black, rows start on a byte. */
    const unsigned long want[20] = {
        0x000000ff, 0xffffffff, 0x000000ff, 0xffffffff, 0xffffffff,
        0x000000ff, 0xffffffff, 0x000000ff, 0x000000ff, 0x000000ff,
        0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0x000000ff,
        0x000000ff, 0x000000ff, 0x000000ff, 0x000000ff, 0xffffffff};
    for (little = 0; little < 2; little++) {
        used = 0;
        header(10, 2, 1, 0);
        bytes("\xa5\xff\xf0\x01", 4);
        expect(10, 2, want);
        /* A map is ignored. */
        used = 0;
        header(10, 2, 1, 0);
        map(1, 3, 2);
        bytes("\x11\x22\x33\x44\x55\x66", 6);
        bytes("\xa5\xff\xf0\x01", 4);
        expect(10, 2, want);
        /* Several bit planes are unsupported. */
        used = 0;
        header(10, 2, 3, 0);
        bytes("\xa5\xff\xf0\x01\xa5\xff\xf0\x01\xa5\xff\xf0\x01", 12);
        assert(decode(used) == CODEC_INVALID);
        assert(decode(used - 1) == CODEC_TRUNCATED);
    }
    little = 0;
}

static void test_maps(void)
{
    const unsigned long rgb[4] = {0x0a141eff, 0x28323cff, 0x46505aff, 0x0a141eff};
    const unsigned long gray[3] = {0x0a0a0aff, 0x282828ff, 0x0a0a0aff};
    const unsigned long two[2] = {0x0a1e0aff, 0x142814ff};
    for (little = 0; little < 2; little++) {
        /* Each map band holds one component of every entry. An index past the
           map's end takes the first entry. */
        used = 0;
        header(4, 1, 1, 1);
        map(1, 3, 3);
        bytes("\x0a\x28\x46" "\x14\x32\x50" "\x1e\x3c\x5a", 9);
        bytes("\0\1\2\3", 4);
        expect(4, 1, rgb);
        assert(decode(1024 + 8) == CODEC_TRUNCATED);
        /* The shared scheme reads the same. */
        set(F_MAP_SCHEME, 3);
        expect(4, 1, rgb);
        /* A map with no type holds bytes. */
        set(F_MAP_TYPE, 0);
        expect(4, 1, rgb);
        /* Wider cells, a cycled or grouped scheme, or no entries are unsupported. */
        set(F_MAP_TYPE, 2);
        assert(decode(used) == CODEC_TRUNCATED);
        assert(decode(used + 9u) == CODEC_INVALID);
        set(F_MAP_TYPE, 3);
        assert(decode(used) == CODEC_INVALID);
        set(F_MAP_TYPE, 1);
        set(F_MAP_SCHEME, 2);
        assert(decode(used) == CODEC_INVALID);
        set(F_MAP_SCHEME, 4);
        assert(decode(used) == CODEC_INVALID);
        set(F_MAP_SCHEME, 1);
        set(F_MAP_ENTRIES, 0);
        assert(decode(used) == CODEC_INVALID);
        /* A scheme with no map bands has no map. */
        used = 0;
        header(3, 1, 1, 1);
        map(2, 0, 3);
        bytes("\x0a\x28\x0a", 3);
        expect(3, 1, gray);
        /* A one-band map is gray. */
        used = 0;
        header(3, 1, 1, 1);
        map(1, 1, 2);
        bytes("\x0a\x28", 2);
        bytes("\0\1\2", 3);
        expect(3, 1, gray);
        /* Two map bands give red and blue from the first, green from the second. */
        used = 0;
        header(2, 1, 1, 1);
        map(1, 2, 2);
        bytes("\x0a\x14\x1e\x28", 4);
        bytes("\0\1", 2);
        expect(2, 1, two);
        /* A map over several bands is unsupported. */
        used = 0;
        header(1, 1, 3, 1);
        map(1, 3, 1);
        bytes("\1\2\3", 3);
        bytes("\0\0\0", 3);
        assert(decode(used) == CODEC_INVALID);
    }
    little = 0;
}

static void test_unsupported(void)
{
    static const unsigned long storage[] = {2, 4, 5, 6, 9, 10};
    static const size_t sample[] = {2, 4, 4, 8, 8, 16};
    size_t i;
    for (i = 0; i < sizeof storage / sizeof storage[0]; i++) {
        used = 0;
        header(1, 1, 1, storage[i]);
        memset(data + used, 0, sample[i]);
        used += sample[i];
        assert(decode(used - 1u) == CODEC_TRUNCATED);
        assert(decode(used) == CODEC_INVALID);
        /* Its size is still known, so a following image can be reached. */
        header(1, 1, 1, 1);
        bytes("\x80", 1);
        {
            const unsigned long want[1] = {0x808080ff};
            unsigned count;
            expect_at(1, 1, 1, want);
            assert(viff_count(data, used, &count) == CODEC_OK && count == 2);
        }
    }
    /* Unknown storage, other encodings, explicit locations, two bands. */
    used = 0;
    header(1, 1, 1, 3);
    bytes("\0\0\0\0\0\0\0\0", 8);
    assert(decode(used) == CODEC_INVALID);
    set(F_STORAGE, 1);
    set(F_ENCODE, 2);
    assert(decode(used) == CODEC_INVALID);
    set(F_ENCODE, 0);
    set(F_LOCATION, 2);
    assert(decode(used) == CODEC_INVALID);
    set(F_LOCATION, 1);
    set(F_IMAGES, 2);
    assert(decode(used) == CODEC_INVALID);
    set(F_IMAGES, 1);
    set(F_BANDS, 2);
    assert(decode(used) == CODEC_INVALID);
    set(F_BANDS, 5);
    assert(decode(used) == CODEC_INVALID);
    set(F_BANDS, 0);
    assert(decode(used) == CODEC_INVALID);
    set(F_BANDS, 1);
    assert(decode(used) == CODEC_OK);
    /* Bad magic or file type. */
    data[1] = 2;
    assert(decode(used) == CODEC_INVALID);
    data[1] = 1;
    data[0] = 0xaa;
    assert(decode(used) == CODEC_INVALID);
    assert(decode(0) == CODEC_INVALID);
}

static void test_limits(void)
{
    unsigned count;
    used = 0;
    header(0, 1, 1, 1);
    assert(decode(used) == CODEC_INVALID);
    set(F_WIDTH, 1); set(F_HEIGHT, 0);
    assert(decode(used) == CODEC_INVALID);
    set(F_HEIGHT, 65536);
    assert(decode(used) == CODEC_TOO_LARGE);
    set(F_WIDTH, 65536); set(F_HEIGHT, 1);
    assert(decode(used) == CODEC_TOO_LARGE);
    set(F_WIDTH, 0xfffffffful); set(F_HEIGHT, 0xfffffffful);
    assert(decode(used) == CODEC_TOO_LARGE);
    set(F_WIDTH, 4097); set(F_HEIGHT, 4096);
    assert(decode(used) == CODEC_TOO_LARGE);
    /* The largest image passes the limits and needs its data. */
    set(F_WIDTH, 4096);
    assert(decode(used) == CODEC_TRUNCATED);
    assert(viff_count(data, used, &count) == CODEC_TRUNCATED && count == 0);
    /* Huge bands, maps and sample sizes can't overflow. */
    set(F_WIDTH, 65535); set(F_HEIGHT, 256);
    set(F_BANDS, 0xfffffffful);
    assert(decode(used) == CODEC_TOO_LARGE);
    set(F_BANDS, 65535); set(F_STORAGE, 10);
    assert(decode(used) == CODEC_TRUNCATED);
    set(F_BANDS, 1); set(F_STORAGE, 1);
    map(1, 0xfffffffful, 0xfffffffful);
    assert(decode(used) == CODEC_TOO_LARGE);
    map(1, 65535, 65535);
    set(F_MAP_TYPE, 7);
    assert(decode(used) == CODEC_TRUNCATED);
    set(F_MAP_TYPE, 8);
    assert(decode(used) == CODEC_INVALID);
    assert(viff_decode(NULL, 0, 0, NULL) == CODEC_INVALID);
    assert(viff_count(NULL, 0, &count) == CODEC_INVALID);
    assert(viff_count(data, used, NULL) == CODEC_INVALID);
}

static void test_multi(void)
{
    const unsigned long first[2] = {0x0a0a0aff, 0xc8c8c8ff};
    const unsigned long second[1] = {0x010203ff};
    const unsigned long third[1] = {0xffffffff};
    unsigned count;
    size_t end;
    for (little = 0; little < 2; little++) {
        used = 0;
        header(2, 1, 1, 1);
        bytes("\x0a\xc8", 2);
        little = !little;               /* each image has its own byte order */
        header(1, 1, 3, 1);
        bytes("\1\2\3", 3);
        little = !little;
        header(1, 1, 1, 0);
        bytes("\0", 1);
        end = used;
        assert(viff_count(data, used, &count) == CODEC_OK && count == 3);
        expect_at(0, 2, 1, first);
        expect_at(1, 1, 1, second);
        expect_at(2, 1, 1, third);
        assert(decode_at(used, 3) == CODEC_INVALID);
        assert(decode_at(used, 0xffffffffu) == CODEC_INVALID);
        /* Trailing bytes that aren't an image are ignored. */
        bytes("junk", 4);
        assert(viff_count(data, used, &count) == CODEC_OK && count == 3);
        assert(decode_at(used, 3) == CODEC_INVALID);
        /* A cut-off last image isn't counted and can't be loaded. */
        used = end - 1u;
        assert(viff_count(data, used, &count) == CODEC_OK && count == 2);
        assert(decode_at(used, 2) == CODEC_TRUNCATED);
        expect_at(1, 1, 1, second);
        /* Just the third image's magic byte. */
        used = start + 1u;
        assert(viff_count(data, used, &count) == CODEC_OK && count == 2);
        assert(decode_at(used, 2) == CODEC_TRUNCATED);
        /* An image that can't be sized ends the count and can't be loaded. */
        used = end;
        data[start + 1u] = 2;
        assert(viff_count(data, used, &count) == CODEC_OK && count == 2);
        assert(decode_at(used, 2) == CODEC_INVALID);
        data[start + 1u] = 1;
    }
    little = 0;
}

static void test_encode(void)
{
    const uint8_t rgba[8] = {0x10, 0x20, 0x30, 0xff, 0x40, 0x50, 0x60, 0x7f};
    const unsigned long opaque[2] = {0x102030ff, 0x405060ff};
    const unsigned long translucent[2] = {0x102030ff, 0x4050607f};
    uint8_t row[2];
    unsigned band;

    assert(!viff_row_has_alpha(rgba, 1));
    assert(viff_row_has_alpha(rgba, 2));
    assert(!viff_row_has_alpha(NULL, 2));
    assert(!viff_make_header(0, 1, 3, data));
    assert(!viff_make_header(1, 65536, 3, data));
    assert(!viff_make_header(1, 1, 2, data));
    assert(!viff_make_header(1, 1, 3, NULL));
    assert(viff_encode_band(rgba, 2, 4, row, 2) == 0);
    assert(viff_encode_band(rgba, 2, 0, row, 1) == 0);
    assert(viff_encode_band(NULL, 2, 0, row, 2) == 0);

    /* An opaque image saves three bands, which decode back. */
    assert(viff_make_header(2, 1, 3, data));
    used = 1024;
    for (band = 0; band < 3; band++) {
        assert(viff_encode_band(rgba, 2, band, row, sizeof row) == 2);
        bytes((const char *)row, 2);
    }
    little = 0;
    start = 0;
    expect(2, 1, opaque);
    /* With alpha, a fourth band. */
    assert(viff_make_header(2, 1, 4, data));
    used = 1024;
    for (band = 0; band < 4; band++) {
        assert(viff_encode_band(rgba, 2, band, row, sizeof row) == 2);
        bytes((const char *)row, 2);
    }
    expect(2, 1, translucent);
}

int main(void)
{
    test_gray();
    test_rgb();
    test_bits();
    test_maps();
    test_unsupported();
    test_limits();
    test_multi();
    test_encode();
    puts("viff: ok");
    return 0;
}
