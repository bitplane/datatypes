#include "../formats/dcx/decode.h"
#include "../formats/dcx/encode.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t file[8192];
static size_t used;

static void put32(size_t at, uint32_t value)
{
    file[at] = (uint8_t)value; file[at + 1] = (uint8_t)(value >> 8);
    file[at + 2] = (uint8_t)(value >> 16); file[at + 3] = (uint8_t)(value >> 24);
}

/* Start a file with room for slots directory entries, all zero. */
static void begin(unsigned slots)
{
    memset(file, 0, sizeof file);
    put32(0, DCX_MAGIC);
    used = 4u + slots * 4u;
}

static void pcx_header(size_t at, unsigned width, unsigned height,
                       unsigned bits, unsigned planes, unsigned bytes_per_line)
{
    uint8_t *h = file + at;
    h[0] = 0x0a; h[1] = 5; h[2] = 0; h[3] = (uint8_t)bits;
    h[8] = (uint8_t)(width - 1u); h[9] = (uint8_t)((width - 1u) >> 8);
    h[10] = (uint8_t)(height - 1u); h[11] = (uint8_t)((height - 1u) >> 8);
    h[65] = (uint8_t)planes;
    h[66] = (uint8_t)bytes_per_line; h[67] = (uint8_t)(bytes_per_line >> 8);
    h[68] = 1;
}

/* Append an uncompressed 2x1 8-bit page, indexes 0 and 1, with a palette. */
static size_t add_indexed(const uint8_t first[3], const uint8_t second[3])
{
    size_t at = used;
    pcx_header(at, 2, 1, 8, 1, 2);
    file[at + 128] = 0; file[at + 129] = 1;
    file[at + 130] = 0x0c;
    memcpy(file + at + 131, first, 3);
    memcpy(file + at + 134, second, 3);
    used = at + 131 + 768;
    return at;
}

/* Append a 3x1 24-bit RLE page. */
static size_t add_rgb(void)
{
    static const uint8_t rgba[12] = {255,0,0,255, 0,255,0,255, 0,0,255,255};
    size_t at = used, count;
    assert(pcx_make_header(3, 1, file + at));
    count = pcx_encode_row(rgba, 3, file + at + 128, sizeof file - at - 128);
    assert(count != 0);
    used = at + 128 + count;
    return at;
}

static void expect(size_t length, unsigned index, const uint8_t *pixels,
                   unsigned width, unsigned height)
{
    struct pcx_image image;
    assert(dcx_decode(file, length, index, &image) == CODEC_OK);
    assert(image.width == width && image.height == height);
    assert(memcmp(image.rgba, pixels, (size_t)width * height * 4u) == 0);
    pcx_free(&image);
}

static enum codec_result decode(size_t length, unsigned index)
{
    struct pcx_image image;
    enum codec_result result = dcx_decode(file, length, index, &image);
    assert(result != CODEC_OK || image.rgba != NULL);
    pcx_free(&image);
    return result;
}

static const uint8_t red[3] = {255,0,0}, green[3] = {0,255,0},
                     blue[3] = {0,0,255}, white[3] = {255,255,255};
static const uint8_t rgb[12] = {255,0,0,255, 0,255,0,255, 0,0,255,255};
static const uint8_t red_green[8] = {255,0,0,255, 0,255,0,255};
static const uint8_t blue_white[8] = {0,0,255,255, 255,255,255,255};

static void multi_page(void)
{
    unsigned count;
    size_t p0, p1, p2;

    /* Each 8-bit page has its own palette at the end of the page. */
    begin(4);
    p0 = add_indexed(red, green);
    p1 = add_rgb();
    p2 = add_indexed(blue, white);
    put32(4, (uint32_t)p0); put32(8, (uint32_t)p1); put32(12, (uint32_t)p2);
    assert(dcx_count(file, used, &count) == CODEC_OK && count == 3);
    expect(used, 0, red_green, 2, 1);
    expect(used, 1, rgb, 3, 1);
    expect(used, 2, blue_white, 2, 1);
    assert(decode(used, 3) == CODEC_INVALID);
    assert(decode(used, 0xffffffffu) == CODEC_INVALID);

    /* Directory order need not be file order. */
    put32(4, (uint32_t)p2); put32(12, (uint32_t)p0);
    expect(used, 0, blue_white, 2, 1);
    expect(used, 2, red_green, 2, 1);
    expect(used, 1, rgb, 3, 1);

    /* Two entries may share a page. */
    put32(12, (uint32_t)p1);
    expect(used, 0, blue_white, 2, 1);
    expect(used, 1, rgb, 3, 1);
    expect(used, 2, rgb, 3, 1);

    /* The last page's truncated palette is an error, not a gray image. */
    put32(4, (uint32_t)p0); put32(8, (uint32_t)p1); put32(12, (uint32_t)p2);
    assert(decode(used - 1, 2) == CODEC_INVALID);
    assert(decode(used - 1, 1) == CODEC_OK);
    /* Truncated in a page's pixels. */
    assert(decode(p1 + 129, 1) == CODEC_TRUNCATED);
}

/* A one-page file whose uncompressed page holds these pixel bytes. */
static void single(unsigned width, unsigned bits, unsigned planes,
                   unsigned bytes_per_line, const uint8_t *pixels, size_t size)
{
    begin(2);
    put32(4, (uint32_t)used);
    pcx_header(used, width, 1, bits, planes, bytes_per_line);
    memcpy(file + used + 128, pixels, size);
    used += 128 + size;
}

static void set_palette(unsigned index, const uint8_t colour[3])
{
    memcpy(file + 12 + 16 + index * 3u, colour, 3);
}

static void pages(void)
{
    static const uint8_t wbw[12] = {255,255,255,255, 0,0,0,255, 255,255,255,255};
    static const uint8_t rgr[12] = {255,0,0,255, 0,255,0,255, 255,0,0,255};
    static const uint8_t ramp4[16] = {0,0,0,255, 255,255,255,255,
                                      0,170,0,255, 0,170,170,255};
    static const uint8_t yellow[4] = {170,170,0,255};
    static const uint8_t rgba[8] = {1,2,3,4, 5,6,7,8};
    static const uint8_t gray[8] = {7,7,7,255, 200,200,200,255};
    static const uint8_t black[3] = {0,0,0};
    static const uint8_t planar6[4] = {0x00, 0x80, 0x80, 0x00};
    static const uint8_t mono = 0xa0, packed2 = 0x1b;
    static const uint8_t packed4[2] = {0x10, 0x00}, planes8x4[8] = {1,5, 2,6, 3,7, 4,8};
    static const uint8_t gray8[2] = {7, 200};
    unsigned i;

    /* 1-bit, odd bytes per line (ImageMagick and netpbm write them), no palette. */
    single(3, 1, 1, 1, &mono, 1);
    expect(used, 0, wbw, 3, 1);
    /* A real two-colour palette is used... */
    set_palette(0, green); set_palette(1, red);
    expect(used, 0, rgr, 3, 1);
    /* ...but two equal colours, as Pillow writes, mean black and white. */
    for (i = 0; i < 16; i++)
        set_palette(i, i < 8 ? black : white);
    expect(used, 0, wbw, 3, 1);

    /* 2-bit packed and 1-bit planar without a palette use netpbm's colours. */
    single(4, 2, 1, 1, &packed2, 1);
    expect(used, 0, ramp4, 4, 1);
    single(1, 1, 4, 1, planar6, 4);
    expect(used, 0, yellow, 1, 1);
    /* 4-bit packed with a palette. */
    single(2, 4, 1, 2, packed4, 2);
    set_palette(0, blue); set_palette(1, white);
    {
        static const uint8_t wb[8] = {255,255,255,255, 0,0,255,255};
        expect(used, 0, wb, 2, 1);
    }

    /* 8-bit RGBA: four planes, the fourth alpha, kept as declared. */
    single(2, 8, 4, 2, planes8x4, 8);
    expect(used, 0, rgba, 2, 1);
    /* Gray 8-bit without a palette, as palette info 2 declares. */
    single(2, 8, 1, 2, gray8, 2);
    file[12 + 68] = 2;
    expect(used, 0, gray, 2, 1);
    file[12 + 68] = 1;
    assert(decode(used, 0) == CODEC_INVALID);
    /* Too few bytes per line, zero, and unsupported depths. */
    single(3, 1, 1, 0, &mono, 1);
    assert(decode(used, 0) == CODEC_INVALID);
    single(17, 1, 1, 2, planar6, 4);
    assert(decode(used, 0) == CODEC_INVALID);
    single(1, 8, 2, 1, planar6, 2);
    assert(decode(used, 0) == CODEC_INVALID);
    single(1, 16, 1, 2, planar6, 2);
    assert(decode(used, 0) == CODEC_INVALID);
    single(1, 4, 2, 1, planar6, 2);
    assert(decode(used, 0) == CODEC_INVALID);
}

static void directory(void)
{
    unsigned count = 99, i;
    size_t p0;

    /* ImageMagick's layout: 1024 slots, the unused ones zero. */
    begin(1024);
    p0 = add_rgb();
    put32(4, (uint32_t)p0);
    assert(dcx_count(file, used, &count) == CODEC_OK && count == 1);
    expect(used, 0, rgb, 3, 1);

    /* The smallest layout: one entry and the terminator. */
    begin(2);
    p0 = add_rgb();
    put32(4, (uint32_t)p0);
    expect(used, 0, rgb, 3, 1);

    /* Bad magic, no pages, truncation at each boundary. */
    file[0] ^= 1;
    assert(dcx_count(file, used, &count) == CODEC_INVALID && count == 0);
    file[0] ^= 1;
    assert(dcx_count(NULL, 0, &count) == CODEC_TRUNCATED);
    assert(dcx_count(file, 3, &count) == CODEC_TRUNCATED);
    assert(dcx_count(file, 4, &count) == CODEC_TRUNCATED);
    assert(dcx_count(file, 8, &count) == CODEC_TRUNCATED);
    assert(dcx_count(file, 11, &count) == CODEC_TRUNCATED);
    assert(dcx_count(file, 12, &count) == CODEC_TRUNCATED);
    assert(dcx_count(file, 12 + 127, &count) == CODEC_TRUNCATED);
    assert(dcx_count(file, 12 + 128, &count) == CODEC_OK);
    assert(decode(12 + 128, 0) == CODEC_TRUNCATED);
    for (i = 0; i < used; i++)
        assert(decode(i, 0) != CODEC_OK);
    put32(4, 0);
    assert(dcx_count(file, used, &count) == CODEC_INVALID);

    /* Offsets into the directory, past the end, or huge. */
    put32(4, 8);
    assert(dcx_count(file, used, &count) == CODEC_INVALID);
    put32(4, 11);
    assert(dcx_count(file, used, &count) == CODEC_INVALID);
    put32(4, (uint32_t)used);
    assert(dcx_count(file, used, &count) == CODEC_TRUNCATED);
    put32(4, 0xffffffffu);
    assert(dcx_count(file, used, &count) == CODEC_TRUNCATED);

    /* A full directory of 1024 pages has no terminator. */
    begin(1024);
    p0 = add_rgb();
    for (i = 0; i < 1024; i++)
        put32(4 + i * 4, (uint32_t)p0);
    assert(dcx_count(file, used, &count) == CODEC_OK && count == 1024);
    expect(used, 1023, rgb, 3, 1);
    assert(decode(used, 1024) == CODEC_INVALID);
    /* Pages inside the full directory are rejected. */
    put32(4 + 1023 * 4, 4 + 1023 * 4);
    assert(dcx_count(file, used, &count) == CODEC_INVALID);

    assert(dcx_decode(file, used, 0, NULL) == CODEC_INVALID);
}

static void writer(void)
{
    static const uint8_t source[8] = {10,20,30,255, 0,0,0,0};
    static const uint8_t expected[8] = {10,20,30,255, 255,255,255,255};
    uint8_t *out = malloc(DCX_DIRECTORY_SIZE + 128 + 64);
    struct pcx_image image;
    unsigned count;
    size_t size;

    assert(out != NULL);
    dcx_make_directory(out);
    assert(pcx_make_header(2, 1, out + DCX_DIRECTORY_SIZE));
    size = pcx_encode_row(source, 2, out + DCX_DIRECTORY_SIZE + 128, 64);
    assert(size != 0);
    size += DCX_DIRECTORY_SIZE + 128;
    assert(dcx_count(out, size, &count) == CODEC_OK && count == 1);
    assert(dcx_decode(out, size, 0, &image) == CODEC_OK);
    assert(image.width == 2 && image.height == 1);
    assert(memcmp(image.rgba, expected, sizeof expected) == 0);
    pcx_free(&image);
    free(out);
}

/* Optional: decode every page of files given on the command line to name.N.rgba. */
static void dump(const char *path)
{
    FILE *f = fopen(path, "rb");
    static uint8_t data[64u << 20];
    size_t length;
    unsigned count, i;
    assert(f != NULL);
    length = fread(data, 1, sizeof data, f);
    fclose(f);
    printf("%s: count %d\n", path, (int)dcx_count(data, length, &count));
    for (i = 0; i < count; i++) {
        struct pcx_image image;
        char name[512];
        enum codec_result r = dcx_decode(data, length, i, &image);
        printf("  page %u: result %d %ux%u\n", i, (int)r, image.width, image.height);
        if (r != CODEC_OK)
            continue;
        snprintf(name, sizeof name, "%s.%u.rgba", path, i);
        f = fopen(name, "wb");
        assert(f != NULL);
        fwrite(image.rgba, 4, (size_t)image.width * image.height, f);
        fclose(f);
        pcx_free(&image);
    }
}

int main(int argc, char **argv)
{
    int i;
    if (argc > 1) {
        for (i = 1; i < argc; i++)
            dump(argv[i]);
        return 0;
    }
    multi_page();
    pages();
    directory();
    writer();
    puts("dcx: ok");
    return 0;
}
