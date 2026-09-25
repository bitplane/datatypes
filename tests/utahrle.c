#include "../formats/utahrle/decode.h"
#include "../formats/utahrle/encode.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Build files byte by byte. */
static uint8_t file[4096];
static size_t used;

static void put(unsigned byte) { file[used++] = (uint8_t)byte; }
static void put16(unsigned value) { put(value & 0xffu); put(value >> 8); }

static void header(unsigned width, unsigned height, unsigned flags,
                   unsigned ncolors, unsigned ncmap, unsigned cmaplen)
{
    put(0x52); put(0xcc);
    put16(7); put16(9); /* xpos and ypos are ignored */
    put16(width); put16(height);
    put(flags); put(ncolors); put(8); put(ncmap); put(cmaplen);
}

static void start(unsigned width, unsigned height, unsigned flags,
                  unsigned ncolors)
{
    used = 0;
    header(width, height, flags | 0x02, ncolors, 0, 8);
    put(0);
}

static void bytes(const char *values, unsigned count)
{
    unsigned i;
    put(0x05); put(count - 1u);
    for (i = 0; i < count; i++)
        put((uint8_t)values[i]);
    if (count & 1u)
        put(0);
}

static void run(unsigned value, unsigned count)
{
    put(0x06); put(count - 1u); put(value); put(0);
}

static void expect_at(const uint8_t *data, size_t length, unsigned index,
                      const uint8_t *pixels, unsigned width, unsigned height)
{
    struct utahrle_image image;
    assert(utahrle_decode(data, length, index, &image) == CODEC_OK);
    assert(image.width == width && image.height == height);
    assert(memcmp(image.rgba, pixels, (size_t)width * height * 4u) == 0);
    utahrle_free(&image);
}

static void expect(const uint8_t *pixels, unsigned width, unsigned height)
{
    expect_at(file, used, 0, pixels, width, height);
}

static enum codec_result decode(const uint8_t *data, size_t length)
{
    struct utahrle_image image;
    enum codec_result result = utahrle_decode(data, length, 0, &image);
    if (result == CODEC_OK)
        utahrle_free(&image);
    else
        assert(image.rgba == NULL && image.width == 0 && image.height == 0);
    return result;
}

static unsigned count(void)
{
    unsigned n = 99;
    assert(utahrle_count(file, used, &n) == CODEC_OK);
    return n;
}

/* Every prefix shorter than the full image is truncated, except that the
   final filler byte after the EOF opcode is optional. */
static void check_truncation(void)
{
    size_t full = used, cut;
    unsigned n;
    for (cut = 0; cut + 1u < full; cut++) {
        enum codec_result result = decode(file, cut);
        assert(result == (cut < 2 ? CODEC_INVALID : CODEC_TRUNCATED));
        assert(utahrle_count(file, cut, &n) == result && n == 0);
    }
    assert(decode(file, full - 1u) == CODEC_OK);
}

/* Write an image and decode it again. */
static void round_trip(const uint8_t *rgba, unsigned width, unsigned height,
                       unsigned ncolors, unsigned alpha)
{
    static uint8_t out[1 << 20];
    size_t capacity = utahrle_row_capacity(width), size, pos;
    unsigned needs = 0, y;
    struct utahrle_image image;
    uint8_t *row = malloc(capacity);

    assert(row != NULL);
    for (y = 0; y < height; y++)
        needs |= utahrle_row_needs(rgba + (size_t)y * width * 4u, width);
    assert(utahrle_make_header(width, height, needs, out));
    assert(out[11] == ncolors && (out[10] & 0x04) == alpha);
    pos = UTAHRLE_HEADER_SIZE;
    for (y = height; y-- > 0;) {
        size = utahrle_encode_row(rgba + (size_t)y * width * 4u, width, needs,
                                  y == height - 1u, row, capacity);
        assert(size != 0 && size <= capacity && pos + size < sizeof out);
        memcpy(out + pos, row, size);
        pos += size;
    }
    memcpy(out + pos, utahrle_end, 2);
    pos += 2;
    assert(utahrle_decode(out, pos, 0, &image) == CODEC_OK);
    assert(image.width == width && image.height == height);
    assert(memcmp(image.rgba, rgba, (size_t)width * height * 4u) == 0);
    utahrle_free(&image);
    free(row);
}

int main(void)
{
    struct utahrle_image image;
    unsigned n, i;

    /* RGB, stored bottom up: the first line is the bottom row. */
    {
        const uint8_t want[] = {
            10,20,30,255, 11,21,31,255,
            1,2,3,255, 4,5,6,255};
        start(2, 2, 0, 3);
        put(0x02); put(0); bytes("\1\4", 2);
        put(0x02); put(1); bytes("\2\5", 2);
        put(0x02); put(2); bytes("\3\6", 2);
        put(0x01); put(1);
        put(0x02); put(0); bytes("\12\13", 2);
        put(0x02); put(1); bytes("\24\25", 2);
        put(0x02); put(2); bytes("\36\37", 2);
        put(0x07); put(0);
        expect(want, 2, 2);
        check_truncation();
        assert(count() == 1);
    }

    /* Gray with runs, odd literal padding, and unwritten pixels as zero. */
    {
        const uint8_t want[] = {
            9,9,9,255, 9,9,9,255, 9,9,9,255, 7,7,7,255, 0,0,0,255};
        start(5, 1, 0, 1);
        run(9, 3); bytes("\7", 1);
        put(0x07);
        expect(want, 5, 1);
    }

    /* Long forms of every operand. */
    {
        const unsigned width = 300;
        start(width, 3, 0, 1);
        put(0x42); put(0); /* SetColor ignores the long bit */
        put(0x41); put(0); put16(1);
        put(0x43); put(0); put16(2);
        put(0x46); put(0); put16(width - 3u); put(0x22); put(0);
        put(0x01); put(1);
        put(0x45); put(0); put16(width - 1u);
        for (i = 0; i < width; i++)
            put(0x44);
        put(0x07); put(0);
        /* Rows top down: the 0x44 literal row, the run row, the empty row. */
        assert(utahrle_decode(file, used, 0, &image) == CODEC_OK);
        assert(image.width == width && image.height == 3);
        for (i = 0; i < width; i++) {
            assert(image.rgba[i * 4u] == 0x44);
            assert(image.rgba[(width + i) * 4u] == (i < 2 ? 0 : 0x22));
            assert(image.rgba[(2u * width + i) * 4u] == 0);
            assert(image.rgba[(2u * width + i) * 4u + 3] == 255);
        }
        utahrle_free(&image);
    }

    /* Runs and literals past the right edge are clipped; data below the top
       row is ignored, and skips saturate instead of wrapping. */
    {
        const uint8_t want[] = {5,5,5,255, 5,5,5,255, 1,1,1,255};
        start(3, 1, 0, 1);
        run(1, 200);
        put(0x02); put(0); put(0x43); put(0); put16(0xffff);
        put(0x43); put(0); put16(0xffff); bytes("\2\2\2", 3);
        put(0x02); put(0); run(5, 2); bytes("\1\3\3", 3);
        put(0x41); put(0); put16(0xffff); put(0x41); put(0); put16(0xffff);
        run(8, 3);
        put(0x07); put(0);
        expect(want, 3, 1);
    }

    /* Background: filled only when the clear flag is set. */
    {
        const uint8_t filled[] = {1,2,3,255, 7,2,3,255};
        const uint8_t zero[] = {0,0,0,255, 7,0,0,255};
        used = 0;
        header(2, 1, 0x01, 3, 0, 8);
        put(1); put(2); put(3);
        put(0x02); put(0); put(0x03); put(1); bytes("\7", 1);
        put(0x07); put(0);
        expect(filled, 2, 1);
        check_truncation();
        file[10] = 0x00;
        expect(zero, 2, 1);
    }

    /* A gray background is one byte, so it needs no padding. */
    {
        const uint8_t want[] = {6,6,6,255};
        used = 0;
        header(1, 1, 0x01, 1, 0, 8);
        put(6);
        put(0x07); put(0);
        expect(want, 1, 1);
    }

    /* Alpha is channel 255; unwritten pixels are transparent. */
    {
        const uint8_t want[] = {1,2,3,128, 0,0,0,0};
        start(2, 1, 0x04, 3);
        put(0x02); put(255); bytes("\200", 1);
        put(0x02); put(0); bytes("\1", 1);
        put(0x02); put(1); bytes("\2", 1);
        put(0x02); put(2); bytes("\3", 1);
        put(0x07); put(0);
        expect(want, 2, 1);
        /* Without the alpha flag, alpha data is dropped. */
        file[10] = 0x02;
        {
            const uint8_t opaque[] = {1,2,3,255, 0,0,0,255};
            expect(opaque, 2, 1);
        }
    }

    /* Declared alpha that is zero everywhere is kept. */
    {
        const uint8_t want[] = {50,50,50,0};
        start(1, 1, 0x04, 1);
        put(0x02); put(0); bytes("\62", 1);
        put(0x02); put(255); bytes("\0", 1);
        put(0x07); put(0);
        expect(want, 1, 1);
    }

    /* Undeclared channels are read and dropped. */
    {
        const uint8_t want[] = {4,4,4,255};
        start(1, 1, 0, 1);
        put(0x02); put(1); bytes("\11", 1);
        put(0x02); put(3); run(9, 1);
        put(0x02); put(0); bytes("\4", 1);
        put(0x07); put(0);
        expect(want, 1, 1);
    }

    /* Pseudocolour: one channel through three 16-bit maps, high bytes.
       Values past the end of a short map pass through. */
    {
        const uint8_t want[] = {255,0,128,255, 1,2,3,255, 3,3,3,255};
        used = 0;
        header(3, 1, 0x02, 1, 3, 1); /* two entries a map */
        put(0);
        put16(0xff00); put16(0x01ff);
        put16(0x0000); put16(0x0202);
        put16(0x80ff); put16(0x0303);
        put(0x02); put(0); bytes("\0\1\3", 3);
        put(0x07); put(0);
        expect(want, 3, 1);
        check_truncation();
    }

    /* RGB through three maps. One map for every channel is unsupported. */
    {
        const uint8_t want[] = {10,21,32,255};
        unsigned m, e;
        used = 0;
        header(1, 1, 0x02, 3, 3, 8);
        put(0);
        for (m = 0; m < 3; m++)
            for (e = 0; e < 256; e++)
                put16((e + 10u * m + 10u) << 8);
        for (m = 0; m < 3; m++) {
            put(0x02); put(m); bytes(m == 0 ? "\0" : m == 1 ? "\1" : "\2", 1);
        }
        put(0x07); put(0);
        expect(want, 1, 1);
        file[13] = 1;
        assert(decode(file, used) == CODEC_INVALID);
    }

    /* Gray through three maps with alpha; alpha is not mapped. */
    {
        const uint8_t want[] = {200,100,50,40};
        used = 0;
        header(1, 1, 0x06, 1, 3, 0); /* one entry a map */
        put(0);
        put16(200u << 8); put16(100u << 8); put16(50u << 8);
        put(0x02); put(0); bytes("\0", 1);
        put(0x02); put(255); bytes("\50", 1);
        put(0x07); put(0);
        expect(want, 1, 1);
    }

    /* Comments are padded to an even length. */
    {
        const uint8_t want[] = {3,3,3,255};
        used = 0;
        header(1, 1, 0x0a, 1, 0, 8);
        put(0);
        put16(3); put('a'); put('b'); put(0); put(0);
        put(0x02); put(0); bytes("\3", 1);
        put(0x07); put(0);
        expect(want, 1, 1);
        check_truncation();
        used = 0;
        header(1, 1, 0x0a, 1, 0, 8);
        put(0);
        put16(2); put('a'); put(0);
        put(0x02); put(0); bytes("\3", 1);
        put(0x07); put(0);
        expect(want, 1, 1);
    }

    /* Several images, with and without the filler after EOF. */
    {
        const uint8_t first[] = {1,1,1,255};
        const uint8_t second[] = {2,2,2,255, 2,2,2,255};
        const uint8_t third[] = {3,3,3,255};
        start(1, 1, 0, 1);
        bytes("\1", 1); put(0x07); put(0);
        header(2, 1, 0x02, 1, 0, 8); put(0);
        run(2, 2); put(0x07);
        header(1, 1, 0x02, 1, 0, 8); put(0);
        bytes("\3", 1); put(0x07); put(0);
        assert(count() == 3);
        expect_at(file, used, 0, first, 1, 1);
        expect_at(file, used, 1, second, 2, 1);
        expect_at(file, used, 2, third, 1, 1);
        assert(utahrle_decode(file, used, 3, &image) == CODEC_INVALID);
        assert(image.rgba == NULL);
        assert(utahrle_decode(file, used, 0xffffffffu, &image) == CODEC_INVALID);
        /* A broken later image ends the count; asking for it fails. */
        used -= 3;
        assert(count() == 2);
        assert(utahrle_decode(file, used, 2, &image) == CODEC_TRUNCATED);
        /* Trailing bytes that aren't an image are ignored. */
        start(1, 1, 0, 1);
        bytes("\1", 1); put(0x07); put(0); put(0x52); put(0);
        assert(count() == 1);
        expect(first, 1, 1);
    }

    /* Invalid headers and opcodes. */
    {
        start(1, 1, 0, 1);
        put(0x07); put(0);
        assert(decode(file, used) == CODEC_OK);
        file[0] = 0x53;
        assert(decode(file, used) == CODEC_INVALID);
        assert(utahrle_count(file, used, &n) == CODEC_INVALID && n == 0);
        file[0] = 0x52;
        file[12] = 16;
        assert(decode(file, used) == CODEC_INVALID);
        file[12] = 8;
        for (i = 0; i < 6; i++) {
            file[11] = (uint8_t)"\0\2\4\5\11\377"[i];
            assert(decode(file, used) == CODEC_INVALID);
        }
        file[11] = 1;
        for (i = 1; i < 3; i++) {
            file[13] = (uint8_t)i;
            assert(decode(file, used) == CODEC_INVALID);
        }
        file[13] = 4;
        assert(decode(file, used) == CODEC_INVALID);
        file[13] = 0;
        file[14] = 17;
        assert(decode(file, used) == CODEC_INVALID);
        file[14] = 16; /* big maps are fine when absent */
        assert(decode(file, used) == CODEC_OK);
        file[13] = 3; /* and truncated when promised */
        assert(decode(file, used) == CODEC_TRUNCATED);
        file[13] = 0;
        file[6] = 0;
        assert(decode(file, used) == CODEC_INVALID);
        file[6] = 1; file[8] = 0;
        assert(decode(file, used) == CODEC_INVALID);
        /* 4097 x 4097 is over the pixel limit, 4096 x 4096 is not. */
        file[6] = 0x01; file[7] = 0x10; file[8] = 0x01; file[9] = 0x10;
        assert(decode(file, used) == CODEC_TOO_LARGE);
        file[6] = 0x00; file[8] = 0x00;
        assert(decode(file, used) == CODEC_OK);
        file[6] = 0xff; file[7] = 0xff; file[8] = 1; file[9] = 0;
        assert(decode(file, used) == CODEC_OK);
        /* Unknown opcodes, including the unused 0 and 4. */
        for (i = 0; i < 4; i++) {
            start(1, 1, 0, 1);
            put("\0\4\10\77"[i]); put(0); put(0x07); put(0);
            assert(decode(file, used) == CODEC_INVALID);
        }
        /* No EOF opcode is truncation. */
        start(1, 1, 0, 1);
        bytes("\1", 1);
        assert(decode(file, used) == CODEC_TRUNCATED);
        assert(decode(file, 0) == CODEC_INVALID);
        assert(decode(file, 1) == CODEC_INVALID);
    }

    /* Writer: the simplest variant that keeps every pixel. */
    {
        const uint8_t gray[] = {0,0,0,255, 9,9,9,255, 9,9,9,255, 9,9,9,255,
                                200,200,200,255, 1,1,1,255};
        const uint8_t rgb[] = {1,2,3,255, 1,2,3,255, 4,5,6,255, 7,7,7,255};
        const uint8_t graya[] = {5,5,5,0, 6,6,6,255};
        const uint8_t rgba[] = {1,2,3,4, 5,6,7,8};
        static uint8_t wide[1000 * 3 * 4];
        uint8_t small[8];

        round_trip(gray, 3, 2, 1, 0);
        round_trip(gray, 6, 1, 1, 0);
        round_trip(rgb, 2, 2, 3, 0);
        round_trip(graya, 2, 1, 3, 4);
        round_trip(rgba, 1, 2, 3, 4);
        /* Long runs, long literals, and noise that alternates short runs. */
        srand(1);
        for (i = 0; i < sizeof wide; i++)
            wide[i] = i < 1000u * 4u ? (uint8_t)(i % 4u == 3 ? 255 : 42) :
                      i < 2000u * 4u ? (uint8_t)rand() :
                      (uint8_t)((i / 4u) % 7u < 3u ? 1 : (i / 12u) & 0xffu);
        round_trip(wide, 1000, 3, 3, 4);
        round_trip(wide, 1000, 1, 1, 0);
        for (i = 0; i < 1000u * 3u; i++)
            wide[i * 4u + 3] = 255;
        round_trip(wide, 1000, 3, 3, 0);
        /* Worst case for the capacity: AAAB AAAB ... in every channel. */
        for (i = 0; i < 1000u; i++)
            memset(wide + i * 4u, i % 4u == 3 ? 200 + (int)(i & 1u) : 100 + (int)(i / 4u & 1u), 4);
        round_trip(wide, 1000, 1, 3, 4);

        assert(!utahrle_make_header(0, 1, 0, file));
        assert(!utahrle_make_header(65536, 1, 0, file));
        assert(utahrle_encode_row(rgb, 2, 1, 0, small, sizeof small) == 0);
        assert(utahrle_encode_row(rgb, 2, 1, 1, small, 3) == 0);
    }

    puts("utahrle: ok");
    return 0;
}
