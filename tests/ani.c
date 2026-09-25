#include "../formats/ani/decode.h"
#include "../formats/ani/encode.h"
#include "../common/icoenc.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t file[1 << 20];
static size_t used;

static void put32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value; p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16); p[3] = (uint8_t)(value >> 24);
}

/* Append a chunk, padded to an even size. Returns its offset. */
static size_t chunk(const char *id, const void *data, size_t size)
{
    size_t at = used;
    memcpy(file + used, id, 4);
    put32(file + used + 4, (uint32_t)size);
    if (data != NULL)
        memcpy(file + used + 8, data, size);
    used += 8 + size;
    if (size & 1u)
        file[used++] = 0;
    return at;
}

/* Open a LIST; close_list fills in its size. */
static size_t open_list(const char *type)
{
    size_t at = chunk("LIST", NULL, 0);
    memcpy(file + used, type, 4);
    used += 4;
    return at;
}

static void close_list(size_t at)
{
    put32(file + at + 4, (uint32_t)(used - at - 8));
}

static void start(void)
{
    uint8_t anih[36];
    memset(file, 0, sizeof file);
    memcpy(file, "RIFF", 4);
    memcpy(file + 8, "ACON", 4);
    used = 12;
    memset(anih, 0, sizeof anih);
    put32(anih, 36);
    put32(anih + 32, 1);
    chunk("anih", anih, sizeof anih);
}

static size_t finish(void)
{
    put32(file + 4, (uint32_t)(used - 8));
    return used;
}

/* A solid w×h frame in colour c, as a cursor with hotspot (hx, hy). */
static size_t cursor_frame(uint8_t *out, unsigned w, unsigned h, const uint8_t c[4],
                           unsigned hx, unsigned hy)
{
    uint8_t *rgba = malloc((size_t)w * h * 4u);
    size_t i, size;
    for (i = 0; i < (size_t)w * h; i++)
        memcpy(rgba + i * 4u, c, 4);
    size = ico_encode(rgba, w, h, 1, hx, hy, out, ico_encode_capacity(w, h));
    free(rgba);
    assert(size != 0);
    return size;
}

static void add_frame(unsigned w, unsigned h, const uint8_t c[4], unsigned hx, unsigned hy)
{
    static uint8_t buffer[300000];
    size_t size = cursor_frame(buffer, w, h, c, hx, hy);
    chunk("icon", buffer, size);
}

static void expect_pixel(const uint8_t *data, size_t length, unsigned index,
                         unsigned w, unsigned h, const uint8_t c[4])
{
    struct ani_frame frame;
    struct ico_image image;
    assert(ani_frame(data, length, index, &frame) == CODEC_OK);
    assert(!frame.entry.png);
    assert(ico_decode_bmp(frame.ico, frame.length, &frame.entry, &image) == CODEC_OK);
    assert(image.width == w && image.height == h);
    assert(memcmp(image.rgba, c, 4) == 0);
    assert(memcmp(image.rgba + ((size_t)w * h - 1u) * 4u, c, 4) == 0);
    ico_free(&image);
}

static const uint8_t red[4] = {255, 0, 0, 255};
static const uint8_t green[4] = {0, 255, 0, 255};
static const uint8_t blue[4] = {0, 0, 255, 128};

static size_t three_frames(void)
{
    size_t list;
    start();
    list = open_list("fram");
    add_frame(32, 32, red, 1, 2);
    add_frame(16, 24, green, 3, 4);
    add_frame(48, 48, blue, 0, 0);
    close_list(list);
    return finish();
}

static void test_frames(void)
{
    struct ani_frame frame;
    unsigned count;
    size_t length = three_frames();

    assert(ani_count(file, length, &count) == CODEC_OK && count == 3);
    expect_pixel(file, length, 0, 32, 32, red);
    expect_pixel(file, length, 1, 16, 24, green);
    expect_pixel(file, length, 2, 48, 48, blue);
    assert(ani_frame(file, length, 1, &frame) == CODEC_OK);
    assert(frame.cursor && frame.entry.hot_x == 3 && frame.entry.hot_y == 4);
    assert(ani_frame(file, length, 3, &frame) == CODEC_INVALID);
    assert(frame.ico == NULL);
    assert(ani_frame(file, length, ~0u, &frame) == CODEC_INVALID);
}

/* Sizes as real writers get them wrong. */
static void test_sizes(void)
{
    unsigned count;
    size_t length = three_frames(), list = 12 + 8 + 36;

    /* The whole file length as the RIFF size. */
    put32(file + 4, (uint32_t)length);
    assert(ani_count(file, length, &count) == CODEC_OK && count == 3);
    put32(file + 4, (uint32_t)length + 1u);
    assert(ani_count(file, length, &count) == CODEC_TRUNCATED);
    /* A RIFF size that ends before the frames hides them. */
    put32(file + 4, 4);
    assert(ani_count(file, length, &count) == CODEC_INVALID);
    put32(file + 4, (uint32_t)(length - 8));
    /* A LIST size that leaves out the list type, so the last frame overruns it. */
    put32(file + list + 4, (uint32_t)(length - list - 8 - 4));
    assert(ani_count(file, length, &count) == CODEC_OK && count == 3);
    expect_pixel(file, length, 2, 48, 48, blue);
    /* A LIST that claims more than the file holds. */
    put32(file + list + 4, (uint32_t)(length - list - 8 + 2));
    assert(ani_count(file, length, &count) == CODEC_TRUNCATED);
}

/* Odd INFO strings, top-level frames, AF_ICON clear, other chunks. */
static void test_layout(void)
{
    uint8_t anih[36], steps[8] = {0};
    unsigned count;
    size_t list, length;

    memset(file, 0, sizeof file);
    memcpy(file, "RIFF", 4);
    memcpy(file + 8, "ACON", 4);
    used = 12;
    list = open_list("INFO");
    chunk("INAM", "odd", 3);
    chunk("IART", "x", 1);
    chunk("icon", "metadata", 8); /* INFO is metadata, not a frame list. */
    close_list(list);
    memset(anih, 0, sizeof anih);           /* cbSize 0 and no flags, as some writers */
    chunk("anih", anih, sizeof anih);
    chunk("rate", steps, sizeof steps);
    chunk("seq ", steps, sizeof steps);
    add_frame(8, 8, red, 0, 0);             /* not inside LIST fram */
    list = open_list("fram");
    add_frame(4, 4, green, 0, 0);
    close_list(list);
    length = finish();
    assert(ani_count(file, length, &count) == CODEC_OK && count == 2);
    expect_pixel(file, length, 0, 8, 8, red);
    expect_pixel(file, length, 1, 4, 4, green);
    /* Trailing bytes too few for a chunk header. */
    put32(file + 4, (uint32_t)(length - 8));
    assert(ani_count(file, length + 5, &count) == CODEC_OK && count == 2);
    /* The last chunk is odd and its pad byte is missing. */
    used = length;
    chunk("junk", "abc", 3);
    length = finish() - 1;
    put32(file + 4, (uint32_t)(length - 8));
    assert(ani_count(file, length, &count) == CODEC_OK && count == 2);
}

/* A frame with several entries loads the largest, then the deepest. */
static void test_best_entry(void)
{
    static uint8_t small[8192], large[8192], ico[16384];
    size_t a = cursor_frame(small, 16, 16, red, 0, 0);
    size_t b = cursor_frame(large, 32, 32, blue, 5, 6);
    size_t size = 6 + 32 + (a - 22) + (b - 22), length;
    struct ani_frame frame;
    unsigned count;

    memset(ico, 0, sizeof ico);
    memcpy(ico, small, 6);
    ico[4] = 2;
    memcpy(ico + 6, small + 6, 16);
    memcpy(ico + 22, large + 6, 16);
    put32(ico + 6 + 12, 38);
    put32(ico + 22 + 12, (uint32_t)(38 + a - 22));
    memcpy(ico + 38, small + 22, a - 22);
    memcpy(ico + 38 + a - 22, large + 22, b - 22);
    start();
    chunk("icon", ico, size);
    length = finish();
    assert(ani_count(file, length, &count) == CODEC_OK && count == 1);
    assert(ani_frame(file, length, 0, &frame) == CODEC_OK);
    assert(frame.entry.width == 32 && frame.entry.hot_x == 5 && frame.entry.hot_y == 6);
    expect_pixel(file, length, 0, 32, 32, blue);
    /* An icon (type 1) frame has no hotspot. */
    file[length - size + 2] = 1;
    assert(ani_frame(file, length, 0, &frame) == CODEC_OK && !frame.cursor);
}

static void test_malformed(void)
{
    struct ani_frame frame;
    unsigned count;
    size_t length = three_frames(), cut, first_icon;

    assert(ani_count(file, 0, &count) == CODEC_TRUNCATED);
    assert(ani_count(file, 11, &count) == CODEC_TRUNCATED);
    /* Every cut is truncation, at chunk boundaries too. */
    for (cut = 12; cut < length; cut++) {
        put32(file + 4, (uint32_t)(length - 8));
        assert(ani_count(file, cut, &count) == CODEC_TRUNCATED);
        assert(ani_frame(file, cut, 0, &frame) == CODEC_TRUNCATED);
    }
    memcpy(file + 8, "WAVE", 4);
    assert(ani_count(file, length, &count) == CODEC_INVALID);
    memcpy(file + 8, "ACON", 4);
    memcpy(file, "RIFX", 4);
    assert(ani_count(file, length, &count) == CODEC_INVALID);
    memcpy(file, "RIFF", 4);
    /* A chunk size that wraps. */
    first_icon = 12 + 8 + 36 + 12;
    put32(file + first_icon + 4, 0xfffffff8u);
    assert(ani_count(file, length, &count) == CODEC_TRUNCATED);
    assert(ani_frame(file, length, 0, &frame) == CODEC_TRUNCATED);

    /* No frames at all. */
    start();
    length = finish();
    assert(ani_count(file, length, &count) == CODEC_INVALID && count == 0);
    assert(ani_frame(file, length, 0, &frame) == CODEC_INVALID);

    /* A frame that isn't an ICO, as a raw frame would be. */
    start();
    chunk("icon", "\x28\0\0\0\x20\0\0\0", 8);
    length = finish();
    assert(ani_count(file, length, &count) == CODEC_OK && count == 1);
    assert(ani_frame(file, length, 0, &frame) == CODEC_INVALID);

    /* An ICO whose entry runs past the end of its chunk. */
    length = three_frames();
    put32(file + first_icon + 4, 22 + 40);
    put32(file + first_icon - 12 + 4, 4 + 8 + 22 + 40);
    put32(file + 4, (uint32_t)(first_icon + 8 + 22 + 40 - 8));
    assert(ani_frame(file, first_icon + 8 + 22 + 40, 0, &frame) == CODEC_TRUNCATED);
    assert(frame.ico == NULL);
}

static void test_encode(void)
{
    static uint8_t out[400000];
    uint8_t rgba[5 * 3 * 4];
    struct ani_frame frame;
    struct ico_image image;
    unsigned count, i;
    size_t size;

    for (i = 0; i < 15; i++) {
        rgba[i * 4] = (uint8_t)(i * 17);
        rgba[i * 4 + 1] = (uint8_t)(255 - i * 9);
        rgba[i * 4 + 2] = (uint8_t)(i * 3);
        rgba[i * 4 + 3] = 255;
    }
    assert(ani_encode_capacity(0, 3) == 0);
    assert(ani_encode_capacity(257, 3) == 0);
    assert(ani_encode(rgba, 5, 3, 2, 1, out, ani_encode_capacity(5, 3) - 1) == 0);
    size = ani_encode(rgba, 5, 3, 2, 1, out, sizeof out);
    assert(size != 0 && size <= ani_encode_capacity(5, 3) && size % 2 == 0);
    assert(memcmp(out, "RIFF", 4) == 0 && memcmp(out + 8, "ACON", 4) == 0);
    assert(out[4] == (uint8_t)(size - 8));
    assert(ani_count(out, size, &count) == CODEC_OK && count == 1);
    assert(ani_frame(out, size, 0, &frame) == CODEC_OK);
    assert(frame.cursor && frame.entry.hot_x == 2 && frame.entry.hot_y == 1);
    assert(frame.entry.depth == 24);
    assert(ico_decode_bmp(frame.ico, frame.length, &frame.entry, &image) == CODEC_OK);
    assert(image.width == 5 && image.height == 3);
    assert(memcmp(image.rgba, rgba, sizeof rgba) == 0);
    ico_free(&image);

    /* Alpha keeps a 32-bit entry. */
    rgba[7] = 0;
    rgba[11] = 77;
    size = ani_encode(rgba, 5, 3, 0, 0, out, sizeof out);
    assert(ani_frame(out, size, 0, &frame) == CODEC_OK && frame.entry.depth == 32);
    assert(ico_decode_bmp(frame.ico, frame.length, &frame.entry, &image) == CODEC_OK);
    assert(memcmp(image.rgba, rgba, sizeof rgba) == 0);
    ico_free(&image);

    /* 256×256 is the largest a frame holds. */
    {
        uint8_t *big = calloc(256u * 256u, 4);
        assert(big != NULL);
        size = ani_encode(big, 256, 256, 255, 255, out, sizeof out);
        assert(size != 0 && ani_frame(out, size, 0, &frame) == CODEC_OK);
        assert(frame.entry.width == 256 && frame.entry.hot_x == 255);
        free(big);
    }
}

int main(void)
{
    test_frames();
    test_sizes();
    test_layout();
    test_best_entry();
    test_malformed();
    test_encode();
    puts("ani: ok");
    return 0;
}
