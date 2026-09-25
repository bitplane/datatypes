#include "../formats/pdb/decode.h"
#include "../formats/pdb/encode.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t data[8192];

static void put16(uint8_t *p, unsigned value)
{
    p[0] = (uint8_t)(value >> 8);
    p[1] = (uint8_t)value;
}

static void put32(uint8_t *p, unsigned long value)
{
    put16(p, (unsigned)(value >> 16));
    put16(p + 2, (unsigned)value);
}

/* A database with records records, the image at offset, and its header.
   Returns the offset of the pixel data. */
static size_t image(unsigned records, size_t offset, unsigned version, unsigned type,
                    unsigned width, unsigned height)
{
    memset(data, 0, sizeof data);
    memcpy(data, "test", 4);
    memcpy(data + 60, "vIMGView", 8);
    put16(data + 76, records);
    put32(data + 78, offset);
    memcpy(data + 82, "\x40\x6f\x80\x00", 4);
    memcpy(data + offset, "test", 4);
    data[offset + 32] = (uint8_t)version;
    data[offset + 33] = (uint8_t)type;
    put16(data + offset + 50, 0xffff);
    put16(data + offset + 52, 0xffff);
    put16(data + offset + 54, width);
    put16(data + offset + 56, height);
    return offset + 58;
}

/* Pixels as digits, one string per row. */
static void check_row(const struct pdb_image *img, unsigned y, const char *expect)
{
    unsigned x;
    assert(strlen(expect) == img->width);
    for (x = 0; x < img->width; x++) {
        unsigned want = expect[x] <= '9' ? (unsigned)(expect[x] - '0') : (unsigned)(expect[x] - 'a' + 10);
        assert(img->pixels[y * img->width + x] == want);
    }
}

static void check_empty(const struct pdb_image *img)
{
    assert(img->pixels == NULL && img->width == 0 && img->height == 0 && img->depth == 0);
}

int main(void)
{
    struct pdb_image img;
    uint8_t header[PDB_HEADER_SIZE], rgba[40 * 4], row[32], file[256];
    size_t pos, n, size;
    unsigned x, i;

    /* Shades: 0 is white, the top value black, evenly spaced. */
    assert(pdb_shade(1, 0) == 255 && pdb_shade(1, 1) == 0);
    assert(pdb_shade(2, 1) == 170 && pdb_shade(2, 2) == 85 && pdb_shade(2, 3) == 0);
    assert(pdb_shade(4, 1) == 238 && pdb_shade(4, 15) == 0);

    /* 1-bit, uncompressed, rows padded to a byte, most significant bit first. */
    pos = image(1, 86, 0, 0xff, 10, 2);
    memcpy(data + pos, "\xa5\xc0\xff\x40", 4);
    assert(pdb_decode(data, pos + 4, &img) == CODEC_OK);
    assert(img.width == 10 && img.height == 2 && img.depth == 1);
    check_row(&img, 0, "1010010111");
    check_row(&img, 1, "1111111101");
    pdb_free(&img);
    check_empty(&img);

    /* Every truncation, in the header, the record list, the image header or the pixels. */
    for (n = 0; n < pos + 4; n++) {
        enum codec_result r = pdb_decode(data, n, &img);
        assert(r == CODEC_TRUNCATED);
        check_empty(&img);
    }
    /* Trailing bytes, like Image Compression Manager 1.0's extra zero, are ignored. */
    assert(pdb_decode(data, pos + 5, &img) == CODEC_OK);
    pdb_free(&img);

    /* 2-bit gray, 4 pixels to a byte. */
    pos = image(1, 86, 0, 0x00, 6, 1);
    memcpy(data + pos, "\x1b\xe0", 2);
    assert(pdb_decode(data, pos + 2, &img) == CODEC_OK);
    assert(img.depth == 2);
    check_row(&img, 0, "012332");
    pdb_free(&img);

    /* 4-bit gray, 2 pixels to a byte. */
    pos = image(1, 86, 0, 0x02, 3, 2);
    memcpy(data + pos, "\x01\x20\xef\xa0", 4);
    assert(pdb_decode(data, pos + 4, &img) == CODEC_OK);
    assert(img.depth == 4);
    check_row(&img, 0, "012");
    check_row(&img, 1, "efa");
    pdb_free(&img);

    /* RLE: a literal, then a run that crosses into the next row. */
    pos = image(1, 86, 1, 0xff, 16, 2);
    memcpy(data + pos, "\x00\xf0\x81\x0f\x00\x33", 6);
    assert(pdb_decode(data, pos + 6, &img) == CODEC_OK);
    check_row(&img, 0, "1111000000001111");
    check_row(&img, 1, "0000111100110011");
    pdb_free(&img);
    /* The longest literal (0x80: 129 bytes) and run (0xff: 128 bytes). */
    pos = image(1, 86, 1, 0x02, 2, 257);
    data[pos] = 0x80;
    memset(data + pos + 1, 0x12, 129);
    data[pos + 130] = 0xff;
    data[pos + 131] = 0x34;
    assert(pdb_decode(data, pos + 132, &img) == CODEC_OK);
    check_row(&img, 128, "12");
    check_row(&img, 129, "34");
    check_row(&img, 256, "34");
    pdb_free(&img);
    /* Every truncation of the RLE data. */
    for (n = pos; n < pos + 132; n++)
        assert(pdb_decode(data, n, &img) == CODEC_TRUNCATED);
    /* A run past the last pixel is cut off; so is a literal, even one the file cuts short. */
    pos = image(1, 86, 1, 0xff, 8, 2);
    memcpy(data + pos, "\x00\x81\x85\x3c", 4);
    assert(pdb_decode(data, pos + 4, &img) == CODEC_OK);
    check_row(&img, 1, "00111100");
    pdb_free(&img);
    memcpy(data + pos, "\x00\x81\x05\x3c", 4);
    assert(pdb_decode(data, pos + 4, &img) == CODEC_OK);
    check_row(&img, 1, "00111100");
    pdb_free(&img);
    /* The low three bits of the version pick compression; the rest are ignored. */
    data[86 + 32] = 0x09;
    assert(pdb_decode(data, pos + 4, &img) == CODEC_OK);
    pdb_free(&img);

    /* A note record ends the image. The RLE data may not run into it. */
    pos = image(2, 94, 1, 0xff, 8, 3);
    put32(data + 86, pos + 3);
    memcpy(data + 90, "\x40\x6f\x80\x01", 4);
    memcpy(data + pos, "\x81\x55\x00\xaahello", 9);
    assert(pdb_decode(data, pos + 9, &img) == CODEC_TRUNCATED);
    put32(data + 86, pos + 4);
    assert(pdb_decode(data, pos + 9, &img) == CODEC_OK);
    check_row(&img, 2, "10101010");
    pdb_free(&img);
    /* A note offset that is garbage (inside the image header, or past the end) is ignored. */
    put32(data + 86, 100);
    assert(pdb_decode(data, pos + 9, &img) == CODEC_OK);
    pdb_free(&img);
    put32(data + 86, 0xfffffff0ul);
    assert(pdb_decode(data, pos + 9, &img) == CODEC_OK);
    pdb_free(&img);
    /* An uncompressed image cut short by the note is truncated. */
    pos = image(2, 94, 0, 0xff, 8, 4);
    put32(data + 86, pos + 3);
    assert(pdb_decode(data, pos + 9, &img) == CODEC_TRUNCATED);
    /* A gap after the record list, as Palm OS itself writes, is fine. */
    pos = image(1, 88, 0, 0xff, 8, 1);
    data[pos] = 0x80;
    assert(pdb_decode(data, pos + 1, &img) == CODEC_OK);
    check_row(&img, 0, "10000000");
    pdb_free(&img);
    /* Other record attributes and unique IDs are accepted. */
    memset(data + 82, 0, 4);
    assert(pdb_decode(data, pos + 1, &img) == CODEC_OK);
    pdb_free(&img);

    /* Invalid headers. */
    pos = image(1, 86, 0, 0xff, 8, 1);
    data[63] = 'g';
    assert(pdb_decode(data, pos + 1, &img) == CODEC_INVALID);
    check_empty(&img);
    pos = image(0, 86, 0, 0xff, 8, 1);
    assert(pdb_decode(data, pos + 1, &img) == CODEC_INVALID);
    pos = image(1, 86, 0, 0xff, 8, 1);
    put32(data + 78, 40);
    assert(pdb_decode(data, pos + 1, &img) == CODEC_INVALID);
    put32(data + 78, 0xffffffful);
    assert(pdb_decode(data, pos + 1, &img) == CODEC_TRUNCATED);
    /* Reserved types and compression values. */
    for (i = 0; i < 256; i++) {
        pos = image(1, 86, 0, i, 8, 1);
        assert(pdb_decode(data, pos + 4, &img) ==
               (i == 0 || i == 2 || i == 0xff ? CODEC_OK : CODEC_INVALID));
        pdb_free(&img);
    }
    for (i = 2; i < 8; i++) {
        pos = image(1, 86, i, 0xff, 8, 1);
        assert(pdb_decode(data, pos + 1, &img) == CODEC_INVALID);
    }
    /* Empty and oversized images. */
    pos = image(1, 86, 0, 0xff, 0, 1);
    assert(pdb_decode(data, pos + 1, &img) == CODEC_INVALID);
    pos = image(1, 86, 0, 0xff, 8, 0);
    assert(pdb_decode(data, pos + 1, &img) == CODEC_INVALID);
    pos = image(1, 86, 0, 0xff, 65535, 65535);
    assert(pdb_decode(data, sizeof data, &img) == CODEC_TOO_LARGE);
    pos = image(1, 86, 1, 0x02, 4096, 4096);
    assert(pdb_decode(data, sizeof data, &img) == CODEC_TRUNCATED);
    assert(pdb_decode(NULL, 100, &img) == CODEC_TRUNCATED);
    assert(pdb_decode(data, 100, NULL) == CODEC_INVALID);

    /* Writer: widths pad to 16; unsupported sizes and depths. */
    assert(pdb_padded_width(1) == 16 && pdb_padded_width(16) == 16 && pdb_padded_width(17) == 32);
    assert(!pdb_make_header(NULL, 0, 1, 1, header));
    assert(!pdb_make_header(NULL, 1, 0, 1, header));
    assert(!pdb_make_header(NULL, 65521, 1, 1, header));
    assert(pdb_make_header(NULL, 65520, 1, 1, header));
    assert(!pdb_make_header(NULL, 4096, 4097, 1, header));
    assert(!pdb_make_header(NULL, 8, 8, 3, header));
    assert(pdb_row_bytes(8, 8) == 0 && pdb_row_bytes(10, 2) == 4);
    assert(pdb_encode_row(rgba, 10, 2, row, 3) == 0);

    /* Depth: black and white is 1 bit, the four 2-bit grays are 2, anything else 4.
       Transparent pixels are white. */
    memset(rgba, 0, sizeof rgba);
    for (x = 0; x < 10; x++)
        rgba[x * 4 + 3] = 255;
    assert(pdb_row_depth(rgba, 10) == 1);
    rgba[9 * 4 + 3] = 0;
    assert(pdb_row_depth(rgba, 10) == 1);
    memset(rgba + 4, 170, 3);
    assert(pdb_row_depth(rgba, 10) == 2);
    memset(rgba + 8, 85, 3);
    assert(pdb_row_depth(rgba, 10) == 2);
    rgba[12 + 3] = 128;
    assert(pdb_row_depth(rgba, 10) == 4);
    rgba[12 + 3] = 255;
    rgba[12] = 255;
    assert(pdb_row_depth(rgba, 10) == 4);

    /* Round trips at each depth: the header and rows decode back to the same grays. */
    for (i = 1; i <= 4; i *= 2) {
        unsigned top = (1u << i) - 1u;
        for (x = 0; x < 20; x++) {
            unsigned v = x % (top + 1u);
            memset(rgba + x * 4, (int)(255u - v * 255u / top), 3);
            rgba[x * 4 + 3] = 255;
        }
        assert(pdb_row_depth(rgba, 20) == i);
        assert(pdb_make_header("a name longer than thirty-one bytes", 20, 2, i, header));
        assert(header[31] == 0 && header[86 + 31] == 0 && memcmp(header, "a name longer", 13) == 0);
        memcpy(file, header, sizeof header);
        size = sizeof header;
        for (n = 0; n < 2; n++) {
            size_t bytes = pdb_encode_row(rgba, 20, i, row, sizeof row);
            assert(bytes == 32u * i / 8u);
            memcpy(file + size, row, bytes);
            size += bytes;
        }
        assert(pdb_decode(file, size, &img) == CODEC_OK);
        assert(img.width == 32 && img.height == 2 && img.depth == i);
        for (x = 0; x < 32; x++) {
            unsigned want = x < 20 ? x % (top + 1u) : 0;
            assert(img.pixels[x] == want && img.pixels[32 + x] == want);
        }
        pdb_free(&img);
        assert(pdb_decode(file, size - 1, &img) == CODEC_TRUNCATED);
    }
    /* 4-bit rounds other grays to the nearest of 16; 1-bit thresholds at half. */
    memset(rgba, 0, sizeof rgba);
    rgba[0] = rgba[1] = rgba[2] = 25; rgba[3] = 255;
    rgba[4] = rgba[5] = rgba[6] = 0; rgba[7] = 0;
    assert(pdb_encode_row(rgba, 2, 4, row, sizeof row) == 8);
    assert(row[0] == 0xe0);
    rgba[0] = rgba[1] = rgba[2] = 127;
    assert(pdb_encode_row(rgba, 2, 1, row, sizeof row) == 2);
    assert(row[0] == 0x80 && row[1] == 0);
    assert(pdb_make_header("", 2, 1, 4, header) && memcmp(header, "Image", 6) == 0);

    printf("pdb: ok\n");
    return 0;
}
