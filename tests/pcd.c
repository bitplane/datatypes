#include "../formats/pcd/decode.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define SECTOR 2048u
#define BASE_END (96u * SECTOR + 768u * 512u * 3u / 2u)
#define W4 1536u
#define H4 1024u
#define W16 3072u
#define H16 2048u

static uint8_t file[4u << 20];
static size_t size, bit;
static const size_t plain_sector[3] = { 4, 23, 96 };

/* Residual codes: 0 -> 0, 10 -> +5, 110 -> -3, 1110 -> +100. 1111 is
   unassigned. */
static const struct { unsigned bits, code; int delta; } codes[] = {
    { 1, 0x0, 0 }, { 2, 0x2, 5 }, { 3, 0x6, -3 }, { 4, 0xe, 100 } };

/* Table quirks: a code with a stray bit past its length (never matches)
   first, and a later duplicate of "10" (the first one wins). */
static int quirky_table;
/* Stream quirks, by row of the 4Base luma. */
static unsigned garbage_row = ~0u, long_row = ~0u;

static void put(uint32_t v, unsigned n)
{
    while (n-- > 0) {
        uint8_t m = (uint8_t)(0x80u >> (bit & 7u));
        file[bit >> 3] = (uint8_t)(v >> n & 1u ? file[bit >> 3] | m : file[bit >> 3] & ~m);
        bit++;
    }
}

static void emit(int delta)
{
    unsigned i;
    for (i = 0; codes[i].delta != delta; i++)
        ;
    put(codes[i].code, codes[i].bits);
}

static void entry(size_t *at, unsigned bits, unsigned code16, int delta)
{
    file[(*at)++] = (uint8_t)(bits - 1u);
    file[(*at)++] = (uint8_t)(code16 >> 8);
    file[(*at)++] = (uint8_t)code16;
    file[(*at)++] = (uint8_t)delta;
}

static void table(void)
{
    size_t at = bit >> 3, start = at++;
    unsigned i, n = 4;

    if (quirky_table) {
        entry(&at, 1, 0x4000, 100);
        n++;
    }
    for (i = 0; i < 4; i++)
        entry(&at, codes[i].bits, codes[i].code << (16u - codes[i].bits), codes[i].delta);
    if (quirky_table) {
        entry(&at, 2, 0x8000, -3);
        n++;
    }
    file[start] = (uint8_t)(n - 1u);
    bit = at * 8u;
}

static void sync_row(unsigned plane, unsigned row)
{
    put(0xfffffe, 24);
    put(plane << 14 | row << 1, 16);
}

static void header(unsigned res, unsigned rotation)
{
    memset(file, 0, sizeof file);
    memset(file, 0xff, 32);
    memcpy(file + 0x800, "PCD_IPI", 7);
    file[0xe02] = (uint8_t)(rotation | res << 2);
    size = 3 * SECTOR;
}

static uint8_t *luma(unsigned level, unsigned x, unsigned y)
{
    unsigned w = 192u << level;
    return file + plain_sector[level] * SECTOR + (size_t)(y / 2u) * 3u * w + (y & 1u) * w + x;
}

static uint8_t *chroma(unsigned level, unsigned plane, unsigned x, unsigned y)
{
    unsigned w = 192u << level;
    return file + plain_sector[level] * SECTOR + (size_t)y * 3u * w + 2u * w +
           (plane == 2 ? w / 2u : 0) + x;
}

/* Fill the three plain levels with one colour. */
static void base(uint8_t l, uint8_t c1, uint8_t c2)
{
    unsigned level, x, y;

    for (level = 0; level < 3; level++) {
        unsigned w = 192u << level, h = 128u << level;
        for (y = 0; y < h; y++)
            memset(luma(level, 0, y), l, w);
        for (y = 0; y < h / 2u; y++)
            for (x = 0; x < w / 2u; x++) {
                *chroma(level, 1, x, y) = c1;
                *chroma(level, 2, x, y) = c2;
            }
    }
    size = BASE_END;
}

static int d4(unsigned plane, unsigned x, unsigned y)
{
    if (plane != 0)
        return 100;             /* 4Base chroma rows are ignored */
    if (x == 0 && y == 0)
        return 100;
    if (x == 7 && y == 5)
        return 5;
    if (x == W4 - 1 && y == H4 - 1)
        return -3;
    return 0;
}

static int d16(unsigned plane, unsigned x, unsigned y)
{
    if (plane == 0)
        return x == 1000 && y == 1000 ? -3 : x == W16 - 1 && y == H16 - 1 ? 5 : 0;
    if (plane == 2)
        return x == 100 && y == 100 ? -3 : 0;
    return x == 400 && y == 300 ? 5 : 0;
}

/* Write one level's residuals from sector: tables, rows (with chroma rows
   when chroma is set) and the end marker. Returns the sector where
   ImageMagick looks for the next level. */
static size_t level(size_t sector, unsigned tables, int with_chroma, unsigned w, unsigned h,
                    int (*d)(unsigned, unsigned, unsigned))
{
    size_t end, loaded;
    unsigned t, x, y, p;

    bit = sector * SECTOR * 8u;
    for (t = 0; t < tables; t++)
        table();
    put(0, 5);                  /* padding before the first sync */
    for (y = 0; y < h; y++) {
        sync_row(0, y);
        for (x = 0; x < w; x++) {
            if (y == garbage_row && x == 1)
                put(0xf, 4);
            emit(y == garbage_row && x > 0 ? 5 : d(0, x, y));
        }
        if (y == long_row)
            emit(5), emit(5);
        if (with_chroma && y % 2u == 0)
            for (p = 2; p <= 3; p++) {
                sync_row(p, y);
                for (x = 0; x < w / 2u; x++)
                    emit(d(p, x, y / 2u));
            }
    }
    end = bit;
    sync_row(0, h);
    size = (bit + 7u) / 8u;
    loaded = (end + 40u) / 8u + 1u;
    return sector + (loaded - sector * SECTOR + SECTOR - 1u) / SECTOR + 12u;
}

static void pack4(uint8_t l, unsigned tables)
{
    header(1, 0);
    base(l, 156, 137);
    level(388, tables, 1, W4, H4, d4);
}

static void pack16(void)
{
    size_t next;

    header(2, 0);
    base(60, 156, 137);
    next = level(388, 1, 0, W4, H4, d4);
    level(next, 3, 1, W16, H16, d16);
}

static void pixel(const struct pcd_image *im, unsigned x, unsigned y, int r, int g, int b)
{
    const uint8_t *p = im->rgba + ((size_t)y * im->width + x) * 4u;
    if (p[0] != r || p[1] != g || p[2] != b || p[3] != 255) {
        fprintf(stderr, "(%u,%u) = %d %d %d %d, want %d %d %d\n", x, y, p[0], p[1], p[2], p[3],
                r, g, b);
        assert(0);
    }
}

static void grey(const struct pcd_image *im, unsigned x, unsigned y, int v)
{
    pixel(im, x, y, v, v, v);
}

static enum codec_result decode(size_t length, unsigned long index, struct pcd_image *im)
{
    return pcd_decode(file, length, index, im);
}

static unsigned long count(size_t length)
{
    unsigned long n = 99;
    assert(pcd_count(file, length, &n) == CODEC_OK);
    return n;
}

static void test_colour(void)
{
    static const uint8_t in[][3] = { { 0, 0, 0 }, { 255, 255, 255 }, { 100, 200, 50 },
                                     { 60, 156, 137 }, { 182, 156, 137 } };
    static const uint8_t out[][3] = { { 0, 194, 0 }, { 255, 194, 255 }, { 0, 198, 233 },
                                      { 82, 82, 82 }, { 247, 247, 247 } };
    struct pcd_image im;
    unsigned i;

    for (i = 0; i < 5; i++) {
        header(0, 0);
        base(in[i][0], in[i][1], in[i][2]);
        assert(decode(size, 0, &im) == CODEC_OK);
        assert(im.width == 192 && im.height == 128);
        pixel(&im, 0, 0, out[i][0], out[i][1], out[i][2]);
        pixel(&im, 191, 127, out[i][0], out[i][1], out[i][2]);
        pcd_free(&im);
    }
}

static void test_plain(void)
{
    struct pcd_image im;
    unsigned i;

    header(0, 0);
    base(60, 156, 137);
    assert(count(size) == 3);
    for (i = 0; i < 3; i++) {
        assert(decode(size, i, &im) == CODEC_OK);
        assert(im.width == 192u << i && im.height == 128u << i);
        grey(&im, 0, 0, 82);
        pcd_free(&im);
    }
    /* The default is the largest. */
    assert(decode(size, PCD_DEFAULT, &im) == CODEC_OK);
    assert(im.width == 768 && im.height == 512);
    pcd_free(&im);
    assert(decode(size, 3, &im) == CODEC_INVALID);
    assert(im.rgba == NULL);

    /* Chroma is interpolated between samples; the last column and row repeat. */
    *chroma(0, 2, 0, 0) = 238;
    *chroma(0, 2, 95, 63) = 238;
    assert(decode(size, 0, &im) == CODEC_OK);
    pixel(&im, 0, 0, 255, 0, 82);
    pixel(&im, 1, 0, 174, 34, 82);
    pixel(&im, 0, 1, 174, 34, 82);
    pixel(&im, 1, 1, 127, 58, 82);
    grey(&im, 2, 0, 82);
    grey(&im, 0, 2, 82);
    pixel(&im, 190, 126, 255, 0, 82);
    pixel(&im, 191, 127, 255, 0, 82);
    pixel(&im, 189, 127, 174, 34, 82);
    pixel(&im, 191, 125, 174, 34, 82);
    pcd_free(&im);

    /* Each level is read from its own sectors. */
    *luma(1, 5, 3) = 160;
    *luma(2, 767, 511) = 160;
    assert(decode(size, 1, &im) == CODEC_OK);
    grey(&im, 5, 3, 217);
    grey(&im, 4, 3, 82);
    pcd_free(&im);
    assert(decode(size, 2, &im) == CODEC_OK);
    grey(&im, 767, 511, 217);
    grey(&im, 766, 511, 82);
    pcd_free(&im);
}

static void test_rotation(void)
{
    static const unsigned where[4][2] = { { 0, 0 }, { 0, 191 }, { 191, 127 }, { 127, 0 } };
    struct pcd_image im;
    unsigned r;

    for (r = 0; r < 4; r++) {
        header(0, r);
        base(60, 156, 137);
        *luma(0, 0, 0) = 160;
        *luma(0, 1, 0) = 65;
        assert(decode(size, 0, &im) == CODEC_OK);
        assert(im.width == (r & 1u ? 128u : 192u) && im.height == (r & 1u ? 192u : 128u));
        grey(&im, where[r][0], where[r][1], 217);
        /* The next pixel along the stored row, turned the same way. */
        if (r == 0) grey(&im, 1, 0, 88);
        if (r == 1) grey(&im, 0, 190, 88);
        if (r == 2) grey(&im, 190, 127, 88);
        if (r == 3) grey(&im, 127, 1, 88);
        pcd_free(&im);
    }
}

static void check4(const struct pcd_image *im)
{
    assert(im->width == W4 && im->height == H4);
    grey(im, 0, 0, 217);
    /* Interpolated from the predictor, not changed by the residuals. */
    grey(im, 1, 0, 82);
    grey(im, 7, 5, 88);
    grey(im, 6, 5, 82);
    grey(im, 8, 5, 82);
    grey(im, W4 - 1, H4 - 1, 77);
    grey(im, W4 - 2, H4 - 1, 82);
}

static void test_4base(void)
{
    struct pcd_image im;

    pack4(60, 1);
    assert(count(size) == 4);
    assert(decode(size, PCD_DEFAULT, &im) == CODEC_OK);
    check4(&im);
    pcd_free(&im);
    assert(decode(size, 2, &im) == CODEC_OK);
    assert(im.width == 768);
    pcd_free(&im);

    /* ImageMagick expects three tables here when it goes on to 16Base. */
    pack4(60, 3);
    assert(decode(size, 3, &im) == CODEC_OK);
    check4(&im);
    pcd_free(&im);

    /* Residuals clamp. */
    pack4(2, 1);
    assert(decode(size, 3, &im) == CODEC_OK);
    grey(&im, 0, 0, 139);
    grey(&im, 7, 5, 10);
    grey(&im, W4 - 1, H4 - 1, 0);
    pcd_free(&im);

    /* Stray bits past a code's length never match, and the first of two
       equal codes wins. */
    quirky_table = 1;
    pack4(60, 1);
    quirky_table = 0;
    assert(decode(size, 3, &im) == CODEC_OK);
    check4(&im);
    pcd_free(&im);

    /* An unassigned code drops the rest of its row. */
    garbage_row = 9;
    pack4(60, 1);
    garbage_row = ~0u;
    assert(decode(size, 3, &im) == CODEC_OK);
    check4(&im);
    grey(&im, 1, 9, 82);
    grey(&im, W4 - 1, 9, 82);
    pcd_free(&im);

    /* A row with extra codes runs into the next, as in ImageMagick. */
    long_row = 9;
    pack4(60, 1);
    assert(decode(size, 3, &im) == CODEC_OK);
    grey(&im, 0, 10, 88);
    grey(&im, 1, 10, 88);
    grey(&im, 2, 10, 82);
    pcd_free(&im);
    /* ...but not past the end of the plane. */
    long_row = H4 - 1;
    pack4(60, 1);
    long_row = ~0u;
    assert(decode(size, 3, &im) == CODEC_OK);
    check4(&im);
    pcd_free(&im);
}

static void test_16base(void)
{
    struct pcd_image im;

    pack16();
    assert(count(size) == 5);
    assert(decode(size, PCD_DEFAULT, &im) == CODEC_OK);
    assert(im.width == W16 && im.height == H16);
    grey(&im, 0, 0, 217);
    grey(&im, 1000, 1000, 77);
    grey(&im, 1001, 1000, 82);
    grey(&im, W16 - 1, H16 - 1, 84);
    grey(&im, W16 - 2, H16 - 2, 77);
    pixel(&im, 800, 600, 91, 77, 82);
    pixel(&im, 801, 600, 87, 79, 82);
    grey(&im, 802, 600, 82);
    pixel(&im, 200, 200, 82, 83, 75);
    pcd_free(&im);
    assert(decode(size, 3, &im) == CODEC_OK);
    check4(&im);
    pcd_free(&im);

    /* The highest resolution comes from the header, 64Base meaning 16Base. */
    file[0xe02] = 3u << 2;
    assert(count(size) == 5);
    file[0xe02] = 0;
    assert(count(size) == 3);
    assert(decode(size, PCD_DEFAULT, &im) == CODEC_OK);
    assert(im.width == 768);
    pcd_free(&im);
    assert(decode(size, 3, &im) == CODEC_INVALID);
}

static void test_overview(void)
{
    struct pcd_image im;
    unsigned i;

    memset(file, 0, sizeof file);
    memcpy(file, "PCD_OPA", 7);
    file[11] = 3;
    for (i = 0; i < 3; i++) {
        uint8_t *t = file + 5u * SECTOR + i * 36864u;
        unsigned y;
        for (y = 0; y < 64; y++) {
            memset(t + y * 576u, 60 + 5 * i, 384);
            memset(t + y * 576u + 384, 156, 96);
            memset(t + y * 576u + 480, 137, 96);
        }
    }
    size = 5u * SECTOR + 3u * 36864u;
    assert(count(size) == 3);
    assert(decode(size, PCD_DEFAULT, &im) == CODEC_OK);
    assert(im.width == 192 && im.height == 128);
    grey(&im, 0, 0, 82);
    pcd_free(&im);
    assert(decode(size, 1, &im) == CODEC_OK);
    grey(&im, 191, 127, 88);
    pcd_free(&im);
    assert(decode(size, 3, &im) == CODEC_INVALID);
    /* Only whole thumbnails count. */
    assert(count(size - 1) == 2);
    assert(decode(size - 1, 2, &im) == CODEC_INVALID);
    assert(pcd_count(file, 5u * SECTOR + 36863u, &(unsigned long){ 0 }) == CODEC_TRUNCATED);
    assert(pcd_count(file, 11, &(unsigned long){ 0 }) == CODEC_TRUNCATED);
    file[11] = 0;
    assert(pcd_count(file, size, &(unsigned long){ 0 }) == CODEC_INVALID);
    assert(decode(size, 0, &im) == CODEC_INVALID);
    /* Rotation doesn't apply to thumbnails. */
    file[11] = 1;
    file[0xe02] = 1;
    assert(decode(size, 0, &im) == CODEC_OK);
    assert(im.width == 192);
    pcd_free(&im);
}

static void test_malformed(void)
{
    struct pcd_image im;
    unsigned long n;
    size_t full, len;

    assert(pcd_count(NULL, 0, &n) == CODEC_INVALID);
    assert(pcd_count(file, 0, NULL) == CODEC_INVALID);
    assert(pcd_decode(file, 0, 0, NULL) == CODEC_INVALID);
    header(0, 0);
    base(60, 156, 137);
    /* Header boundaries. */
    assert(pcd_count(file, 0, &n) == CODEC_TRUNCATED);
    assert(pcd_count(file, 0x806, &n) == CODEC_TRUNCATED);
    assert(pcd_count(file, 0xe02, &n) == CODEC_TRUNCATED);
    assert(pcd_count(file, 0xe03, &n) == CODEC_OK && n == 3);
    assert(decode(0xe03, 0, &im) == CODEC_TRUNCATED);
    file[0x806] = 'X';
    assert(pcd_count(file, size, &n) == CODEC_INVALID);
    assert(decode(size, 0, &im) == CODEC_INVALID);
    file[0x806] = 'I';
    /* Each plain level must be whole. */
    len = 4u * SECTOR + 36864u;
    assert(decode(len - 1, 0, &im) == CODEC_TRUNCATED);
    assert(decode(len, 0, &im) == CODEC_OK);
    pcd_free(&im);
    assert(decode(len, 1, &im) == CODEC_TRUNCATED);
    assert(decode(BASE_END - 1, 2, &im) == CODEC_TRUNCATED);

    /* Every cut in the residuals is truncation, and nothing is shown. */
    pack4(60, 1);
    full = size;
    assert(decode(BASE_END, 3, &im) == CODEC_TRUNCATED);
    assert(decode(388u * SECTOR, 3, &im) == CODEC_TRUNCATED);
    for (len = 388u * SECTOR + 1u; len < full; len += len < 388u * SECTOR + 64u ? 1u : 4099u) {
        assert(decode(len, 3, &im) == CODEC_TRUNCATED);
        assert(im.rgba == NULL);
    }
    for (len = full - 8u; len < full; len++)
        assert(decode(len, 3, &im) == CODEC_TRUNCATED);
    assert(decode(full, 3, &im) == CODEC_OK);
    pcd_free(&im);
    /* The lower levels don't need the residuals. */
    assert(decode(BASE_END, 2, &im) == CODEC_OK);
    pcd_free(&im);

    /* Codes longer than 16 bits. */
    file[388u * SECTOR + 1u + 12u] = 16;
    assert(decode(full, 3, &im) == CODEC_INVALID);
    file[388u * SECTOR + 1u + 12u] = 3;

    /* 16Base must be wholly there. */
    pack16();
    full = size;
    assert(decode(full - 1u, 4, &im) == CODEC_TRUNCATED);
    assert(decode(full - 300000u, 4, &im) == CODEC_TRUNCATED);
    assert(decode(full - 300000u, 3, &im) == CODEC_OK);
    pcd_free(&im);
}

/* Rows for plane 1, rows past the height and chroma rows in 4Base are
   ignored; rows that aren't there keep the prediction. */
static void test_skipped_rows(void)
{
    struct pcd_image im;
    unsigned x;

    header(1, 0);
    base(60, 156, 137);
    bit = 388u * SECTOR * 8u;
    table();
    sync_row(1, 0);
    for (x = 0; x < W4; x++)
        emit(100);
    sync_row(0, 5000);
    for (x = 0; x < W4; x++)
        emit(100);
    sync_row(2, 0);
    for (x = 0; x < W4 / 2u; x++)
        emit(100);
    /* Row 3 only; the rest keep the prediction. */
    sync_row(0, 3);
    emit(5);
    sync_row(0, H4);
    size = (bit + 7u) / 8u;
    assert(decode(size, 3, &im) == CODEC_OK);
    grey(&im, 0, 0, 82);
    grey(&im, 0, 3, 88);
    grey(&im, 1, 3, 82);
    grey(&im, W4 - 1, H4 - 1, 82);
    pcd_free(&im);
    /* No end marker at all. */
    assert(decode(size - 5u, 3, &im) == CODEC_TRUNCATED);
}

/* 16Base starts 12 sectors after the last sector ImageMagick's reader has
   touched, and it reads 25 to 32 bits past the start of the end marker's
   row number. Put 4Base's end marker at the last position that stays inside
   its first sector, and at the first that doesn't. */
static void test_16base_position(void)
{
    static const size_t end_bit[2] = { 2047u * 8u - 40u, 2047u * 8u - 32u };
    struct pcd_image im;
    unsigned i, t;

    for (i = 0; i < 2; i++) {
        size_t next = 388u + 1u + i + 12u;

        header(2, 0);
        base(60, 156, 137);
        bit = 388u * SECTOR * 8u;
        table();
        bit = 388u * SECTOR * 8u + end_bit[i];
        sync_row(0, H4);
        bit = next * SECTOR * 8u;
        for (t = 0; t < 3; t++)
            table();
        sync_row(0, 7);
        emit(5);
        sync_row(0, H16);
        size = (bit + 7u) / 8u;
        assert(decode(size, 4, &im) == CODEC_OK);
        grey(&im, 0, 7, 88);
        grey(&im, 1, 7, 82);
        pcd_free(&im);
    }
}

int main(void)
{
    test_colour();
    test_plain();
    test_rotation();
    test_4base();
    test_16base();
    test_16base_position();
    test_overview();
    test_skipped_rows();
    test_malformed();
    puts("pcd: all tests passed");
    return 0;
}
