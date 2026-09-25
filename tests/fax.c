#include "../formats/fax/codes.h"
#include "../formats/fax/decode.h"
#include "../formats/fax/encode.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EOL "000000000001"
#define RTC EOL EOL EOL EOL EOL EOL
/* White 2, black 4, white 14: "..XXXX.............." */
#define LINE20 "0111" "011" "110100"

static uint8_t buffer[1 << 16];

/* Pack a string of '0' and '1' (other characters are ignored), most
   significant bit first, zero-filling the last byte. */
static size_t pack(const char *bits, uint8_t *out)
{
    size_t count = 0;
    for (; *bits != '\0'; bits++) {
        if (*bits != '0' && *bits != '1')
            continue;
        if (count % 8u == 0)
            out[count / 8u] = 0;
        if (*bits == '1')
            out[count / 8u] |= (uint8_t)(0x80u >> (count % 8u));
        count++;
    }
    return (count + 7u) / 8u;
}

static enum codec_result decode_bits(const char *bits, struct fax_image *image)
{
    return fax_decode(buffer, pack(bits, buffer), image);
}

/* Rows written as '.' for white and 'X' for black. */
static void expect_rows(const struct fax_image *image, const char *const *rows, unsigned count)
{
    unsigned x, y;
    assert(image->height == count);
    for (y = 0; y < count; y++) {
        assert(strlen(rows[y]) == image->width);
        for (x = 0; x < image->width; x++)
            assert(image->pixels[(size_t)y * image->width + x] == (rows[y][x] == 'X'));
    }
}

static void expect_g3(const char *bits, const char *const *rows, unsigned count)
{
    struct fax_image image;
    assert(decode_bits(bits, &image) == CODEC_OK);
    expect_rows(&image, rows, count);
    fax_free(&image);
}

static void expect_error(const char *bits, enum codec_result expected)
{
    struct fax_image image;
    assert(decode_bits(bits, &image) == expected);
    assert(image.pixels == NULL && image.width == 0 && image.height == 0);
}

static void test_tables(void)
{
    static struct fax_lookup lookup;
    unsigned colour, i, bits, j;

    for (colour = 0; colour < 2; colour++) {
        fax_build_lookup(&lookup, (int)colour);
        /* Each code must survive every other code being added: the set is prefix-free. */
        for (i = 0; i < 64 + FAX_MAKEUP_CODES + FAX_EXTENDED_CODES; i++) {
            const char *code = i < 64 ? fax_terminating[colour][i]
                             : i < 64 + FAX_MAKEUP_CODES ? fax_makeup[colour][i - 64]
                             : fax_extended[i - 64 - FAX_MAKEUP_CODES];
            unsigned run = i < 64 ? i : 64u * (i - 63u);
            size_t length = strlen(code);
            assert(length >= 2 && length <= FAX_LOOKUP_BITS);
            for (bits = 0, j = 0; j < length; j++)
                bits = bits << 1 | (unsigned)(code[j] == '1');
            bits <<= FAX_LOOKUP_BITS - length;
            assert(lookup.length[bits] == length && lookup.run[bits] == run);
        }
        /* EOL's zeros start no code. */
        assert(lookup.length[0] == 0 && lookup.length[1] == 0);
    }
}

static void test_g3(void)
{
    static const char *const one[] = {"..XXXX.............."};
    static const char *const two[] = {"..XXXX..............", "..XXXX.............."};
    struct fax_image image;
    size_t size;

    expect_g3(EOL LINE20 EOL LINE20 RTC, two, 2);
    /* No RTC, with or without a final EOL. */
    expect_g3(EOL LINE20 EOL LINE20, two, 2);
    expect_g3(EOL LINE20 EOL LINE20 EOL, two, 2);
    /* Fill bits before EOLs, and a byte-aligned layout. */
    expect_g3("0000" EOL LINE20 "00000" EOL LINE20 "000" RTC, two, 2);
    /* Junk before the first EOL is skipped. */
    expect_g3("1011" EOL LINE20 RTC, one, 1);
    /* An incomplete RTC at the end adds no rows. */
    expect_g3(EOL LINE20 EOL EOL EOL, one, 1);
    /* An empty line mid-page is a white row; short lines are padded white. */
    {
        static const char *const rows[] = {
            "..XXXX..........................................................",
            "..XXXX..........................................................",
            "..XXXX..........................................................",
            "................................................................",
            "..XXXX..........................................................",
        };
        expect_g3(EOL LINE20 EOL "0111" "011" "01000" "0000000001" LINE20
                  EOL "11011" EOL LINE20 RTC, rows, 5);
    }
    /* Long runs use make-up codes, including the extended ones. */
    {
        struct fax_image wide;
        /* White 64+1 = 65, black 1728+2 = 1730, white 2560+2560+64+0 = 5184. */
        assert(decode_bits(EOL "11011" "000111" "0000001100101" "11"
                           "000000011111" "000000011111" "11011" "00110101" RTC, &wide) == CODEC_OK);
        assert(wide.width == 65 + 1730 + 5184 && wide.height == 1);
        assert(wide.pixels[64] == 0 && wide.pixels[65] == 1 && wide.pixels[65 + 1729] == 1);
        assert(wide.pixels[65 + 1730] == 0 && wide.pixels[wide.width - 1] == 0);
        fax_free(&wide);
    }

    /* A bad code keeps the start of its line and the page carries on. */
    {
        static const char *const rows[] = {
            "..XXXX..............", "..XX................", "..XXXX..............",
        };
        expect_g3(EOL LINE20 EOL "0111" "11" "000000001" "1" EOL LINE20 RTC, rows, 3);
    }

    /* Errors. */
    expect_error("", CODEC_TRUNCATED);
    expect_error(LINE20 LINE20, CODEC_INVALID);          /* no EOL */
    expect_error(RTC, CODEC_INVALID);                    /* no lines */
    expect_error(EOL "00110101" RTC, CODEC_INVALID);     /* only a zero-width line */
    expect_error(EOL LINE20 EOL "0111" "011", CODEC_TRUNCATED);    /* last line short */
    expect_error(EOL LINE20 EOL "0111" "011" "1101", CODEC_TRUNCATED);  /* code cut */
    expect_error(EOL LINE20 EOL "11011", CODEC_TRUNCATED);  /* make-up without its run */
    assert(fax_decode(NULL, 0, &image) == CODEC_TRUNCATED);
    assert(fax_decode(buffer, 1, NULL) == CODEC_INVALID);
    /* Lines over 65535 pixels are bad; so a page of nothing else is invalid. */
    {
        char bits[12 + 26 * 12 + 8 + 72 + 1];
        strcpy(bits, EOL);
        for (size = 0; size < 26; size++)
            strcat(bits, "000000011111");
        strcat(bits, "00110101" RTC);
        expect_error(bits, CODEC_INVALID);
    }
    /* More than 65535 rows, and more than 16M pixels. */
    {
        size_t i, bytes = 70000u * 4u + 16u;
        uint8_t *data = malloc(bytes);
        assert(data != NULL);
        /* Byte-aligned EOLs with a 1-pixel white line ("000111" padded) between. */
        for (i = 0; i + 1 < bytes; i += 2) {
            data[i] = 0x00;
            data[i + 1] = 0x01;
        }
        assert(fax_decode(data, bytes, &image) == CODEC_INVALID);  /* all empty lines */
        for (i = 2; i + 3 < bytes; i += 4) {
            data[i] = 0x1c;  /* 000111 00: white 1 */
            data[i + 1] = 0x00;
        }
        assert(fax_decode(data, bytes, &image) == CODEC_TOO_LARGE);
        free(data);
    }
    /* 300 lines of white 65535 (25 x 2560 + 1472 + 63) is 19.6M pixels. */
    {
        static const char *const parts[] = {EOL, "000000011111", "010011000" "00110100"};
        char *bits = malloc(300u * (12u + 25u * 12u + 17u) + 80u), *end;
        size_t i, j;
        uint8_t *data;
        assert(bits != NULL);
        end = bits;
        for (i = 0; i < 300; i++) {
            end += sprintf(end, "%s", parts[0]);
            for (j = 0; j < 25; j++)
                end += sprintf(end, "%s", parts[1]);
            end += sprintf(end, "%s", parts[2]);
        }
        sprintf(end, "%s", RTC);
        data = malloc(strlen(bits) / 8u + 2u);
        assert(data != NULL);
        assert(fax_decode(data, pack(bits, data), &image) == CODEC_TOO_LARGE);
        free(data);
        free(bits);
    }
}

/* Width 8. Codes: P 0001, H 001, V0 1, VR1 011, VL1 010, VR2 000011,
   VL2 000010, VR3 0000011, VL3 0000010. */
#define G4_ROWS \
    "001 0111 011 1" \
    "1 1 1" \
    "011 011 1" \
    "0001 1" \
    "001 00110101 000101" \
    "011 1" \
    "010 000010 1" \
    "0000011 1 1" \
    "0000010 0000010 1" \
    "011 000011 1"
#define EOFB EOL EOL

static const char *const g4_rows[] = {
    "..XXXX..", "..XXXX..", "...XXXX.", "........", "XXXXXXXX",
    ".XXXXXXX", "XXXXXX..", "...XXX..", "XXX.....", ".XXXX...",
};

static void test_g4(void)
{
    struct fax_image image;
    size_t size = pack(G4_ROWS EOFB, buffer), cut;

    assert(fax_decode_g4(buffer, size, 8, 10, &image) == CODEC_OK);
    assert(image.width == 8);
    expect_rows(&image, g4_rows, 10);
    fax_free(&image);
    /* EOFB is optional; data after the last row is ignored. */
    size = pack(G4_ROWS, buffer);
    assert(fax_decode_g4(buffer, size, 8, 10, &image) == CODEC_OK);
    fax_free(&image);
    assert(fax_decode_g4(buffer, size, 8, 3, &image) == CODEC_OK);
    expect_rows(&image, g4_rows, 3);
    fax_free(&image);
    /* Fewer rows than declared, or a cut anywhere. */
    size = pack(G4_ROWS EOFB, buffer);
    assert(fax_decode_g4(buffer, size, 8, 11, &image) == CODEC_TRUNCATED);
    size = pack(G4_ROWS, buffer);
    for (cut = 0; cut < size; cut++)
        assert(fax_decode_g4(buffer, cut, 8, 10, &image) == CODEC_TRUNCATED);
    assert(fax_decode_g4(NULL, 0, 8, 1, &image) == CODEC_TRUNCATED);
    assert(image.pixels == NULL);

    /* Changes past the right edge or left of a0, uncompressed mode, zeros. */
    size = pack("011 111111", buffer);
    assert(fax_decode_g4(buffer, size, 8, 1, &image) == CODEC_INVALID);
    size = pack("001 10011 010 111111", buffer);
    assert(fax_decode_g4(buffer, size, 8, 1, &image) == CODEC_INVALID);
    size = pack("001 0111 010 1" "1 0000010 11111111", buffer);
    assert(fax_decode_g4(buffer, size, 8, 2, &image) == CODEC_INVALID);
    size = pack("0000001111 11111111", buffer);
    assert(fax_decode_g4(buffer, size, 8, 1, &image) == CODEC_INVALID);
    size = pack("00000000 00001111 11111111", buffer);
    assert(fax_decode_g4(buffer, size, 8, 1, &image) == CODEC_INVALID);
    /* An EOL (the start of EOFB) where a row should be. */
    size = pack("1" EOL "1111", buffer);
    assert(fax_decode_g4(buffer, size, 8, 2, &image) == CODEC_TRUNCATED);
    /* Zero-length vertical changes can't overflow the change list: under
       ".X.X.X.X", VL1 and VL2 keep putting a1 back at 0. */
    size = pack("001 000111 010 001 000111 010 001 000111 010 001 000111 010"
                "010 000010 010 000010 010 000010 010 000010 010 000010 010 000010"
                "010 000010 010 000010 11111111", buffer);
    assert(fax_decode_g4(buffer, size, 8, 2, &image) == CODEC_INVALID);
    /* Sizes. */
    assert(fax_decode_g4(buffer, size, 0, 1, &image) == CODEC_INVALID);
    assert(fax_decode_g4(buffer, size, 1, 0, &image) == CODEC_INVALID);
    assert(fax_decode_g4(buffer, size, 65536, 1, &image) == CODEC_TOO_LARGE);
    assert(fax_decode_g4(buffer, size, 4097, 4097, &image) == CODEC_TOO_LARGE);
    assert(fax_decode_g4(buffer, size, 8, 1, NULL) == CODEC_INVALID);
}

static size_t cals(uint8_t *out, const char *const *records, const char *bits)
{
    size_t i;
    memset(out, ' ', CALS_HEADER_SIZE);
    for (i = 0; records[i] != NULL; i++)
        memcpy(out + i * 128u, records[i], strlen(records[i]));
    return CALS_HEADER_SIZE + pack(bits, out + CALS_HEADER_SIZE);
}

static void expect_orientation(const char *rorient, unsigned width, unsigned height,
                               int transpose, int flip_x, int flip_y)
{
    const char *records[] = {"srcdocid: NONE", "rtype: 1", NULL, "rpelcnt: 000008,000010", NULL};
    struct fax_image image;
    unsigned x, y, sx, sy;
    char text[64];
    size_t size;

    sprintf(text, "rorient: %s", rorient);
    records[2] = text;
    size = cals(buffer, records, G4_ROWS EOFB);
    assert(fax_decode(buffer, size, &image) == CODEC_OK);
    assert(image.width == width && image.height == height);
    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            sx = flip_x ? width - 1u - x : x;
            sy = flip_y ? height - 1u - y : y;
            if (transpose) { unsigned t = sx; sx = sy; sy = t; }
            assert(image.pixels[y * width + x] == (g4_rows[sy][sx] == 'X'));
        }
    }
    fax_free(&image);
}

static void test_cals(void)
{
    struct fax_image image;
    size_t size;

    /* Pel path and line progression, counter-clockwise from rightwards. */
    expect_orientation("000,270", 8, 10, 0, 0, 0);
    expect_orientation("180,270", 8, 10, 0, 1, 0);
    expect_orientation("000,090", 8, 10, 0, 0, 1);
    expect_orientation("180,090", 8, 10, 0, 1, 1);
    expect_orientation("270,000", 10, 8, 1, 0, 0);
    expect_orientation("270,180", 10, 8, 1, 1, 0);
    expect_orientation("090,000", 10, 8, 1, 0, 1);
    expect_orientation("090,180", 10, 8, 1, 1, 1);
    /* Nonsense orientations are ignored. */
    expect_orientation("000,000", 8, 10, 0, 0, 0);
    expect_orientation("045,315", 8, 10, 0, 0, 0);
    expect_orientation("sideways", 8, 10, 0, 0, 0);

    /* Other first records, any case, spaces, and no rtype. */
    {
        const char *records[] = {"version: MIL-STD-1840", "RPELCNT:  8 , 10", NULL};
        size = cals(buffer, records, G4_ROWS);
        assert(fax_decode(buffer, size, &image) == CODEC_OK);
        expect_rows(&image, g4_rows, 10);
        fax_free(&image);
    }
    {
        const char *records[] = {"rorient: 000,270", "rpelcnt: 8,10", NULL};
        size = cals(buffer, records, G4_ROWS);
        assert(fax_decode(buffer, size, &image) == CODEC_OK);
        fax_free(&image);
    }
    /* Errors. */
    {
        const char *type2[] = {"srcdocid: NONE", "rtype: 2", "rpelcnt: 8,10", NULL};
        const char *no_size[] = {"srcdocid: NONE", "rtype: 1", NULL};
        const char *bad_size[] = {"srcdocid: NONE", "rpelcnt: 8", NULL};
        const char *zero[] = {"srcdocid: NONE", "rpelcnt: 0,10", NULL};
        const char *huge[] = {"srcdocid: NONE", "rpelcnt: 99999999999,10", NULL};
        const char *wide[] = {"srcdocid: NONE", "rpelcnt: 65536,1", NULL};
        const char *big[] = {"srcdocid: NONE", "rpelcnt: 4097,4097", NULL};
        const char *tall[] = {"srcdocid: NONE", "rpelcnt: 8,11", NULL};
        assert(fax_decode(buffer, cals(buffer, type2, G4_ROWS), &image) == CODEC_INVALID);
        assert(fax_decode(buffer, cals(buffer, no_size, G4_ROWS), &image) == CODEC_INVALID);
        assert(fax_decode(buffer, cals(buffer, bad_size, G4_ROWS), &image) == CODEC_INVALID);
        assert(fax_decode(buffer, cals(buffer, zero, G4_ROWS), &image) == CODEC_INVALID);
        assert(fax_decode(buffer, cals(buffer, huge, G4_ROWS), &image) == CODEC_TOO_LARGE);
        assert(fax_decode(buffer, cals(buffer, wide, G4_ROWS), &image) == CODEC_TOO_LARGE);
        assert(fax_decode(buffer, cals(buffer, big, G4_ROWS), &image) == CODEC_TOO_LARGE);
        assert(fax_decode(buffer, cals(buffer, tall, G4_ROWS EOFB), &image) == CODEC_TRUNCATED);
        size = cals(buffer, tall, "");
        assert(fax_decode(buffer, size - 1, &image) == CODEC_TRUNCATED);
        assert(fax_decode(buffer, 128, &image) == CODEC_TRUNCATED);
        assert(image.pixels == NULL);
    }
    assert(fax_is_cals((const uint8_t *)"srcdocid:", 9) == 0);
}

static void set(uint8_t *p, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    p[0] = r; p[1] = g; p[2] = b; p[3] = a;
}

static void reverse_bytes(uint8_t *data, size_t size)
{
    size_t i;
    unsigned bit;
    for (i = 0; i < size; i++) {
        unsigned value = data[i], reversed = 0;
        for (bit = 0; bit < 8; bit++)
            reversed |= ((value >> bit) & 1u) << (7u - bit);
        data[i] = (uint8_t)reversed;
    }
}

/* A page with bars, diagonals and speckle, like a scanned form. */
static size_t encode_page(uint8_t *out, unsigned width, unsigned height, unsigned seed)
{
    static uint8_t row[400 * 4];
    struct fax_encoder encoder;
    size_t total = 0;
    unsigned x, y, black;

    assert(width <= 400);
    fax_encoder_init(&encoder);
    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            seed = seed * 1103515245u + 12345u;
            black = (x / 8u + y / 5u) % 3u == 0 || (seed >> 16) % 13u == 0;
            set(row + x * 4u, black ? 0 : 255, black ? 0 : 255, black ? 0 : 255, 255);
        }
        total += fax_encode_row(&encoder, row, width, out + total, fax_row_capacity(width));
    }
    return total + fax_encode_end(&encoder, out + total);
}

static void test_encode(void)
{
    static uint8_t rgba[7000 * 4], out[8000];
    struct fax_encoder encoder;
    struct fax_image image;
    uint8_t expected[64], file[64];
    size_t size, total, i;
    unsigned x, y, width;

    /* Same bits as "pbmtog3 -nofixedwidth": an EOL before each row, RTC after. */
    for (x = 0; x < 20; x++)
        set(rgba + x * 4u, 255, 255, 255, 255);
    for (x = 2; x < 6; x++)
        set(rgba + x * 4u, 0, 0, 0, 255);
    fax_encoder_init(&encoder);
    total = 0;
    for (y = 0; y < 3; y++) {
        size = fax_encode_row(&encoder, rgba, 20, file + total, fax_row_capacity(20));
        assert(size != SIZE_MAX);
        total += size;
    }
    total += fax_encode_end(&encoder, file + total);
    size = pack(EOL LINE20 EOL LINE20 EOL LINE20 RTC, expected);
    assert(total == size && memcmp(file, expected, size) == 0);

    /* Threshold and compositing over white. */
    {
        static const struct { uint8_t r, g, b, a; int black; } cases[] = {
            {255, 255, 255, 255, 0}, {0, 0, 0, 255, 1}, {128, 128, 128, 255, 0},
            {127, 127, 127, 255, 1}, {0, 0, 0, 0, 0}, {0, 0, 0, 128, 1},
            {0, 0, 0, 127, 0}, {255, 0, 0, 255, 1}, {0, 255, 0, 255, 0},
        };
        for (i = 0; i < sizeof cases / sizeof cases[0]; i++) {
            set(rgba, cases[i].r, cases[i].g, cases[i].b, cases[i].a);
            fax_encoder_init(&encoder);
            size = fax_encode_row(&encoder, rgba, 1, file, fax_row_capacity(1));
            size += fax_encode_end(&encoder, file + size);
            assert(fax_decode(file, size, &image) == CODEC_OK);
            assert(image.width == 1 && image.pixels[0] == cases[i].black);
            fax_free(&image);
        }
    }
    fax_encoder_init(&encoder);
    assert(fax_encode_row(&encoder, rgba, 0, out, sizeof out) == SIZE_MAX);
    assert(fax_encode_row(&encoder, rgba, 65536, out, sizeof out) == SIZE_MAX);
    assert(fax_encode_row(&encoder, rgba, 20, out, fax_row_capacity(20) - 1) == SIZE_MAX);
    assert(fax_encode_row(&encoder, NULL, 20, out, sizeof out) == SIZE_MAX);

    /* Least significant bit first is detected on a real page, though a tiny
       repetitive one could read either way. A second page after RTC is
       ignored, in either order. */
    {
        static uint8_t stream[1 << 15];
        struct fax_image forward;
        size_t pass;
        for (pass = 0; pass < 2; pass++) {
            total = encode_page(stream, 200, 40, 7);
            assert(fax_decode(stream, total, &forward) == CODEC_OK);
            assert(forward.width == 200 && forward.height == 40);
            if (pass == 1)
                total += encode_page(stream + total, 120, 30, 9);
            reverse_bytes(stream, total);
            assert(fax_decode(stream, total, &image) == CODEC_OK);
            assert(image.width == 200 && image.height == 40);
            assert(memcmp(image.pixels, forward.pixels, 200u * 40u) == 0);
            fax_free(&image);
            reverse_bytes(stream, total);
            assert(fax_decode(stream, total, &image) == CODEC_OK);
            assert(image.width == 200 && image.height == 40);
            assert(memcmp(image.pixels, forward.pixels, 200u * 40u) == 0);
            fax_free(&image);
            fax_free(&forward);
        }
    }

    /* Round trips: widths across the make-up boundaries, worst-case
       alternation, and random rows. */
    {
        static const unsigned widths[] = {1, 2, 7, 63, 64, 65, 127, 1728, 1791, 1792,
                                          2559, 2560, 2561, 2623, 2624, 5185, 7000};
        static uint8_t stream[1 << 16];
        unsigned seed = 1, w, height = 5;
        for (w = 0; w < sizeof widths / sizeof widths[0]; w++) {
            width = widths[w];
            fax_encoder_init(&encoder);
            total = 0;
            for (y = 0; y < height; y++) {
                for (x = 0; x < width; x++) {
                    int black;
                    seed = seed * 1103515245u + 12345u;
                    black = y == 0 ? (x == width - 1u) : y == 1 ? (x % 2u == 0)
                          : y == 2 ? (x % 2u == 1) : y == 3 ? 1 : (int)((seed >> 16) % 7u == 0);
                    set(rgba + x * 4u, black ? 0 : 255, black ? 0 : 255, black ? 0 : 255, 255);
                }
                size = fax_encode_row(&encoder, rgba, width, stream + total, fax_row_capacity(width));
                assert(size != SIZE_MAX && total + size <= sizeof stream - FAX_END_MAX);
                total += size;
            }
            total += fax_encode_end(&encoder, stream + total);
            assert(fax_decode(stream, total, &image) == CODEC_OK);
            assert(image.width == width && image.height == height);
            assert(image.pixels[width - 1] == 1 && image.pixels[width] == 1);
            assert(image.pixels[3u * width + width / 2u] == 1);
            fax_free(&image);
            /* Every cut short of the last row's end is caught or loses rows. */
            for (size = total - 13u; size > total - 40u && size > 2u; size -= 3u) {
                if (fax_decode(stream, size, &image) == CODEC_OK) {
                    assert(image.height < height || image.width == width);
                    fax_free(&image);
                }
            }
        }
    }
}

int main(void)
{
    test_tables();
    test_g3();
    test_g4();
    test_cals();
    test_encode();
    puts("fax codec tests passed");
    return 0;
}
