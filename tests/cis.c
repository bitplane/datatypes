#include "../formats/cis/decode.h"
#include "../formats/cis/encode.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HIGH_COUNT (CIS_HIGH_WIDTH * CIS_HIGH_HEIGHT)
#define MEDIUM_COUNT (CIS_MEDIUM_WIDTH * CIS_MEDIUM_HEIGHT)

static uint8_t file[4 * HIGH_COUNT];
static size_t file_size;

static void add(const char *bytes, size_t count)
{
    assert(file_size + count <= sizeof file);
    memcpy(file + file_size, bytes, count);
    file_size += count;
}

static void adds(const char *text)
{
    add(text, strlen(text));
}

static void addc(unsigned c)
{
    char b = (char)c;
    add(&b, 1);
}

static void start(char mode)
{
    file_size = 0;
    addc(CIS_ESC);
    addc('G');
    addc((unsigned)mode);
}

/* Runs of any length, split with empty runs past 94. */
static void add_run(size_t run)
{
    while (run > 94) {
        addc(0x20 + 94);
        addc(0x20);
        run -= 94;
    }
    addc(0x20 + (unsigned)run);
}

static enum codec_result decode(struct cis_image *image)
{
    return cis_decode(file, file_size, image);
}

/* Every shorter copy is truncated: no copy reaches the ESC, and none fills
   the image. */
static void expect_truncated_prefixes(size_t full)
{
    struct cis_image image;
    size_t keep = file_size;
    for (file_size = 0; file_size < full; file_size++) {
        assert(decode(&image) == CODEC_TRUNCATED);
        assert(image.pixels == NULL && image.width == 0);
    }
    file_size = keep;
}

/* Black where (x + 2y) % 3 == 0, which gives runs of 1 and 2. */
static int pattern(unsigned x, unsigned y)
{
    return (x + 2 * y) % 3 == 0;
}

static void add_pattern(unsigned width, unsigned height)
{
    size_t i, count = (size_t)width * height, run = 0;
    int colour = 1;
    for (i = 0; i < count; i++) {
        int black = pattern((unsigned)(i % width), (unsigned)(i / width));
        if (black == colour) {
            run++;
            continue;
        }
        add_run(run);
        colour ^= 1;
        run = 1;
    }
    add_run(run);
}

static void expect_pattern(unsigned width, unsigned height)
{
    struct cis_image image;
    unsigned x, y;
    assert(decode(&image) == CODEC_OK);
    assert(image.width == width && image.height == height);
    for (y = 0; y < height; y++)
        for (x = 0; x < width; x++)
            assert(image.pixels[y * width + x] == pattern(x, y));
    cis_free(&image);
    assert(image.pixels == NULL);
}

static void expect(enum codec_result result)
{
    struct cis_image image;
    assert(decode(&image) == result);
    assert(image.pixels == NULL && image.width == 0 && image.height == 0);
}

static void set_pixel(uint8_t *p, unsigned r, unsigned g, unsigned b, unsigned a)
{
    p[0] = (uint8_t)r; p[1] = (uint8_t)g; p[2] = (uint8_t)b; p[3] = (uint8_t)a;
}

static void roundtrip(const uint8_t *pixels, unsigned width, unsigned height)
{
    static uint8_t out[HIGH_COUNT * 2];
    struct cis_image image;
    size_t size, i;
    size = cis_encode(pixels, width, height, out, sizeof out);
    assert(size >= 6 && size <= cis_encode_bound(width, height));
    assert(out[0] == CIS_ESC && out[1] == 'G');
    assert(out[2] == (width == CIS_HIGH_WIDTH ? 'H' : 'M'));
    assert(memcmp(out + size - 3, "\033GN", 3) == 0);
    for (i = 3; i < size - 3; i++)
        assert(out[i] >= 0x20 && out[i] <= 0x20 + 94);
    assert(cis_decode(out, size, &image) == CODEC_OK);
    assert(image.width == width && image.height == height);
    assert(memcmp(image.pixels, pixels, (size_t)width * height) == 0);
    cis_free(&image);
    /* No capacity to spare. */
    assert(cis_encode(pixels, width, height, out, cis_encode_bound(width, height) - 1) == 0);
    assert(cis_encode(pixels, width, height, out, cis_encode_bound(width, height)) == size);
}

int main(void)
{
    static uint8_t pixels[HIGH_COUNT];
    struct cis_image image;
    uint8_t rgba[4 * 4], row[4];
    unsigned fw, fh, i;
    size_t full;

    /* Both resolutions. */
    start('M');
    add_pattern(CIS_MEDIUM_WIDTH, CIS_MEDIUM_HEIGHT);
    full = file_size;
    adds("\033GN");
    expect_pattern(CIS_MEDIUM_WIDTH, CIS_MEDIUM_HEIGHT);
    expect_truncated_prefixes(full);
    start('H');
    add_pattern(CIS_HIGH_WIDTH, CIS_HIGH_HEIGHT);
    full = file_size;
    adds("\033GN\r\n\032\032");
    expect_pattern(CIS_HIGH_WIDTH, CIS_HIGH_HEIGHT);
    expect_truncated_prefixes(full);
    /* Without a terminator, a full image is complete. */
    file_size = full;
    expect_pattern(CIS_HIGH_WIDTH, CIS_HIGH_HEIGHT);

    /* The first run is black, empty runs flip colour, and DEL is 95. */
    start('M');
    adds(" \"!\x7f!");
    adds("\033GN");
    assert(decode(&image) == CODEC_OK);
    assert(image.pixels[0] == 0 && image.pixels[1] == 0 && image.pixels[2] == 1);
    for (i = 3; i < 98; i++)
        assert(image.pixels[i] == 0);
    assert(image.pixels[98] == 1);
    for (i = 99; i < MEDIUM_COUNT; i++)
        assert(image.pixels[i] == 0);
    cis_free(&image);
    start('M');
    adds("\" \"#");
    adds("\033GN");
    assert(decode(&image) == CODEC_OK);
    assert(image.pixels[0] == 1 && image.pixels[3] == 1 && image.pixels[4] == 0 &&
           image.pixels[6] == 0 && image.pixels[7] == 0);
    cis_free(&image);

    /* Control characters are skipped, and so are those with the parity bit
       set; other bytes lose their parity bit. 0x9b is not an ESC. */
    start('M');
    add("\r\n\a\0!\x80\x9b\xa2\xa1", 9);
    adds("\033GN");
    assert(decode(&image) == CODEC_OK);
    assert(image.pixels[0] == 1 && image.pixels[1] == 0 && image.pixels[2] == 0 &&
           image.pixels[3] == 1 && image.pixels[4] == 0);
    cis_free(&image);

    /* Any ESC ends the image and leaves the rest white, as in files one pixel
       short or with a mangled trailer. */
    start('H');
    add_run(HIGH_COUNT - 1);
    adds("\033G\n");
    assert(decode(&image) == CODEC_OK);
    assert(image.pixels[HIGH_COUNT - 2] == 1 && image.pixels[HIGH_COUNT - 1] == 0);
    cis_free(&image);
    start('M');
    adds("\033");
    assert(decode(&image) == CODEC_OK);
    for (i = 0; i < MEDIUM_COUNT; i++)
        assert(image.pixels[i] == 0);
    cis_free(&image);

    /* Runs past the end are clamped, and whatever follows a full image,
       binary junk included, is ignored. */
    start('M');
    add_run(MEDIUM_COUNT - 1);
    adds("~~~~\xff\xfe\x01");
    assert(decode(&image) == CODEC_OK);
    assert(image.pixels[MEDIUM_COUNT - 2] == 1 && image.pixels[MEDIUM_COUNT - 1] == 0);
    cis_free(&image);
    start('M');
    add_run(MEDIUM_COUNT + 500);
    assert(decode(&image) == CODEC_OK);
    assert(image.pixels[MEDIUM_COUNT - 1] == 1);
    cis_free(&image);

    /* Leading junk is skipped, including a stray ESC. */
    file_size = 0;
    adds("junk\033x\r\n\033GM!");
    add_run(MEDIUM_COUNT);
    assert(decode(&image) == CODEC_OK);
    assert(image.width == CIS_MEDIUM_WIDTH && image.pixels[0] == 1 && image.pixels[1] == 0);
    cis_free(&image);

    /* Bad headers. */
    file_size = 0;
    expect(CODEC_TRUNCATED);
    adds("\033");
    expect(CODEC_TRUNCATED);
    adds("G");
    expect(CODEC_TRUNCATED);
    adds("S");
    expect(CODEC_INVALID);
    file[2] = 'N';
    expect(CODEC_INVALID);
    file[2] = 'h';
    expect(CODEC_INVALID);
    file[1] = 'g';
    file[2] = 'H';
    expect(CODEC_INVALID);
    file_size = 0;
    adds("GH");
    expect(CODEC_INVALID);
    file_size = 0;
    adds("x");
    expect(CODEC_INVALID);
    assert(cis_decode(NULL, 5, &image) == CODEC_TRUNCATED);
    assert(cis_decode(file, 1, NULL) == CODEC_INVALID);
    /* Leading junk then a cut header. */
    file_size = 0;
    adds("abc\033G");
    expect(CODEC_TRUNCATED);

    /* Frame choice, as pbmtocis makes it. */
    assert(cis_frame(128, 96, &fw, &fh) && fw == 128 && fh == 96);
    assert(cis_frame(1, 1, &fw, &fh) && fw == 128 && fh == 96);
    assert(cis_frame(129, 1, &fw, &fh) && fw == 256 && fh == 192);
    assert(cis_frame(1, 97, &fw, &fh) && fw == 256 && fh == 192);
    assert(cis_frame(4000, 3000, &fw, &fh) && fw == 256 && fh == 192);
    assert(!cis_frame(0, 5, &fw, &fh) && !cis_frame(5, 0, &fw, &fh));

    /* Thresholding composites over white. */
    set_pixel(rgba, 0, 0, 0, 255);
    set_pixel(rgba + 4, 255, 255, 255, 255);
    set_pixel(rgba + 8, 0, 0, 0, 0);
    set_pixel(rgba + 12, 0, 0, 0, 200);
    cis_threshold_row(rgba, 4, row);
    assert(row[0] == 1 && row[1] == 0 && row[2] == 0 && row[3] == 1);

    /* Round trips: patterns, all black, all white and long mixed runs. */
    for (i = 0; i < MEDIUM_COUNT; i++)
        pixels[i] = pattern(i % CIS_MEDIUM_WIDTH, i / CIS_MEDIUM_WIDTH);
    roundtrip(pixels, CIS_MEDIUM_WIDTH, CIS_MEDIUM_HEIGHT);
    for (i = 0; i < HIGH_COUNT; i++)
        pixels[i] = pattern(i % CIS_HIGH_WIDTH, i / CIS_HIGH_WIDTH);
    roundtrip(pixels, CIS_HIGH_WIDTH, CIS_HIGH_HEIGHT);
    memset(pixels, 1, HIGH_COUNT);
    roundtrip(pixels, CIS_HIGH_WIDTH, CIS_HIGH_HEIGHT);
    memset(pixels, 0, HIGH_COUNT);
    roundtrip(pixels, CIS_MEDIUM_WIDTH, CIS_MEDIUM_HEIGHT);
    roundtrip(pixels, CIS_HIGH_WIDTH, CIS_HIGH_HEIGHT);
    srand(1);
    for (i = 0; i < HIGH_COUNT; i++)
        pixels[i] = (uint8_t)((i / (1 + (unsigned)rand() % 300)) & 1);
    roundtrip(pixels, CIS_HIGH_WIDTH, CIS_HIGH_HEIGHT);
    for (i = 0; i < HIGH_COUNT; i++)
        pixels[i] = (uint8_t)(rand() & 1);
    roundtrip(pixels, CIS_HIGH_WIDTH, CIS_HIGH_HEIGHT);
    /* Only the two frame sizes encode. */
    assert(cis_encode(pixels, 100, 50, (uint8_t *)file, sizeof file) == 0);
    assert(cis_encode(pixels, 256, 96, (uint8_t *)file, sizeof file) == 0);
    assert(cis_encode(NULL, 128, 96, (uint8_t *)file, sizeof file) == 0);

    puts("cis: ok");
    return 0;
}
