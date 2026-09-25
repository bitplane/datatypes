#include "../formats/ora/ora.h"
#include "../formats/ora/zip.h"
#include "common/png.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

static uint8_t buffer[1u << 20];

struct member {
    const char *name;
    const uint8_t *data;
    size_t length;
};

static size_t archive(uint8_t *out, size_t capacity, const struct member *m, unsigned count)
{
    struct zip_writer w;
    unsigned i;
    zip_writer_init(&w, out, capacity);
    for (i = 0; i < count; i++)
        assert(zip_add(&w, m[i].name, m[i].data, m[i].length) == CODEC_OK);
    assert(zip_finish(&w) == CODEC_OK);
    return w.length;
}

static unsigned le16(const uint8_t *p) { return (unsigned)p[0] | (unsigned)p[1] << 8; }
static uint32_t le32(const uint8_t *p) { return le16(p) | (uint32_t)le16(p + 2) << 16; }
static void put16(uint8_t *p, unsigned v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { put16(p, v & 0xffffu); put16(p + 2, v >> 16); }

/* Central directory record of a member. */
static uint8_t *central(uint8_t *zip, size_t length, const char *name)
{
    size_t i, n = strlen(name);
    for (i = 0; i + 46 + n <= length; i++)
        if (memcmp(zip + i, "PK\1\2", 4) == 0 && le16(zip + i + 28) == n &&
            memcmp(zip + i + 46, name, n) == 0)
            return zip + i;
    assert(0);
    return NULL;
}

static void rgba_png(const uint8_t *rgba, unsigned w, unsigned h, uint8_t **png, size_t *n)
{
    assert(png_encode(rgba, w, h, png, n) == CODEC_OK);
}

static void pattern(uint8_t *rgba, unsigned w, unsigned h, int alpha)
{
    unsigned i;
    for (i = 0; i < w * h; i++) {
        rgba[i * 4] = (uint8_t)(i * 7);
        rgba[i * 4 + 1] = (uint8_t)(i * 13 + 5);
        rgba[i * 4 + 2] = (uint8_t)(255 - i);
        rgba[i * 4 + 3] = alpha ? (uint8_t)(i * 31) : 255;
    }
}

/* An archive with an optional mimetype, an optional document member (the
   OpenRaster stack by default) and the composite. */
static size_t with_document(uint8_t *out, size_t capacity, const char *mimetype,
                            const char *document, const uint8_t *png, size_t png_length)
{
    static const char stack[] = "<image w=\"3\" h=\"2\"><stack/></image>";
    struct member m[3];
    unsigned n = 0;
    if (mimetype != NULL) {
        m[n].name = "mimetype";
        m[n].data = (const uint8_t *)mimetype;
        m[n++].length = strlen(mimetype);
    }
    if (document != NULL) {
        m[n].name = document;
        m[n].data = (const uint8_t *)stack;
        m[n++].length = sizeof stack - 1;
    }
    m[n].name = "mergedimage.png";
    m[n].data = png;
    m[n++].length = png_length;
    return archive(out, capacity, m, n);
}

static size_t simple(uint8_t *out, size_t capacity, const char *mimetype,
                     const uint8_t *png, size_t png_length)
{
    return with_document(out, capacity, mimetype, "stack.xml", png, png_length);
}

static void test_round_trip(void)
{
    static const unsigned sizes[][2] = {{1, 1}, {3, 2}, {300, 20}, {20, 600}, {257, 256}};
    unsigned s;
    int alpha;
    for (s = 0; s < sizeof sizes / sizeof sizes[0]; s++)
        for (alpha = 0; alpha < 2; alpha++) {
            unsigned w = sizes[s][0], h = sizes[s][1];
            uint8_t *rgba = malloc((size_t)w * h * 4), *out, *thumb;
            size_t length;
            struct ora_image image;
            struct zip_entry e;
            unsigned tw, th;
            pattern(rgba, w, h, alpha);
            assert(ora_encode(rgba, w, h, &out, &length) == CODEC_OK);
            /* Fixed magic: mimetype first, stored. */
            assert(memcmp(out, "PK\3\4", 4) == 0 && le16(out + 8) == 0);
            assert(memcmp(out + 30, "mimetypeimage/openraster", 24) == 0);
            assert(ora_decode(out, length, &image) == CODEC_OK);
            assert(image.width == w && image.height == h);
            assert(memcmp(image.rgba, rgba, (size_t)w * h * 4) == 0);
            ora_free(&image);
            /* The layer and a thumbnail no larger than 256 pixels. */
            assert(zip_find(out, length, "data/layer0.png", &e) == CODEC_OK);
            assert(zip_find(out, length, "Thumbnails/thumbnail.png", &e) == CODEC_OK);
            assert(zip_extract(&e, 1u << 24, &thumb) == CODEC_OK);
            assert(png_info(thumb, e.size, &tw, &th) == CODEC_OK);
            assert(tw <= 256 && th <= 256 && (tw == 256 || th == 256 || (tw == w && th == h)));
            free(thumb);
            free(out);
            free(rgba);
        }
}

static void test_types(void)
{
    uint8_t rgba[3 * 2 * 4], *png;
    size_t n, length;
    struct ora_image image;
    struct member m[1];

    pattern(rgba, 3, 2, 1);
    rgba_png(rgba, 3, 2, &png, &n);
    length = simple(buffer, sizeof buffer, "application/x-krita", png, n);
    assert(ora_decode(buffer, length, &image) == CODEC_OK);
    assert(memcmp(image.rgba, rgba, sizeof rgba) == 0);
    ora_free(&image);
    /* A trailing newline in the mimetype is tolerated. */
    length = simple(buffer, sizeof buffer, "image/openraster\n", png, n);
    assert(ora_decode(buffer, length, &image) == CODEC_OK);
    ora_free(&image);
    /* No mimetype member, but an OpenRaster stack. */
    length = simple(buffer, sizeof buffer, NULL, png, n);
    assert(ora_decode(buffer, length, &image) == CODEC_OK);
    ora_free(&image);
    /* Krita templates with an empty or generic mimetype, and a Krita
       document with none. */
    length = with_document(buffer, sizeof buffer, "", "maindoc.xml", png, n);
    assert(ora_decode(buffer, length, &image) == CODEC_OK);
    ora_free(&image);
    length = with_document(buffer, sizeof buffer, "application/zip", "maindoc.xml", png, n);
    assert(ora_decode(buffer, length, &image) == CODEC_OK);
    ora_free(&image);
    length = with_document(buffer, sizeof buffer, NULL, "maindoc.xml", png, n);
    assert(ora_decode(buffer, length, &image) == CODEC_OK);
    ora_free(&image);
    /* The mimetype alone identifies the file. */
    length = with_document(buffer, sizeof buffer, "image/openraster", NULL, png, n);
    assert(ora_decode(buffer, length, &image) == CODEC_OK);
    ora_free(&image);
    /* Other zip-based documents. */
    length = with_document(buffer, sizeof buffer, "application/vnd.oasis.opendocument.graphics",
                           "content.xml", png, n);
    assert(ora_decode(buffer, length, &image) == CODEC_INVALID);
    length = with_document(buffer, sizeof buffer, "", "content.xml", png, n);
    assert(ora_decode(buffer, length, &image) == CODEC_INVALID);
    length = with_document(buffer, sizeof buffer, "image/openrasterx", NULL, png, n);
    assert(ora_decode(buffer, length, &image) == CODEC_INVALID);
    m[0].name = "mergedimage.png";
    m[0].data = png;
    m[0].length = n;
    length = archive(buffer, sizeof buffer, m, 1);
    assert(ora_decode(buffer, length, &image) == CODEC_INVALID);
    /* No composite. */
    m[0].name = "mimetype";
    m[0].data = (const uint8_t *)"image/openraster";
    m[0].length = 16;
    length = archive(buffer, sizeof buffer, m, 1);
    assert(ora_decode(buffer, length, &image) == CODEC_INVALID);
    /* A composite that isn't a PNG. */
    length = simple(buffer, sizeof buffer, "image/openraster", (const uint8_t *)"GIF89a", 6);
    assert(ora_decode(buffer, length, &image) == CODEC_INVALID);
    /* Not a zip at all. */
    assert(ora_decode((const uint8_t *)"GIF89a", 6, &image) == CODEC_INVALID);
    assert(ora_decode(buffer, 0, &image) == CODEC_INVALID);
    free(png);
}

/* Rebuild the archive with mergedimage.png deflated, and a data descriptor. */
static size_t deflated(uint8_t *out, const uint8_t *png, size_t png_length, int descriptor)
{
    static const char mime[] = "image/openraster";
    uint8_t packed[1u << 16];
    z_stream z;
    size_t pos = 0, packed_length, merged, directory;
    uint32_t crc = (uint32_t)crc32(0, png, (uInt)png_length);

    memset(&z, 0, sizeof z);
    assert(deflateInit2(&z, 9, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY) == Z_OK);
    z.next_in = (Bytef *)png;
    z.avail_in = (uInt)png_length;
    z.next_out = packed;
    z.avail_out = sizeof packed;
    assert(deflate(&z, Z_FINISH) == Z_STREAM_END);
    packed_length = z.total_out;
    deflateEnd(&z);

    /* mimetype, stored. */
    memset(out, 0, 30);
    memcpy(out, "PK\3\4", 4);
    put32(out + 14, (uint32_t)crc32(0, (const Bytef *)mime, 16));
    put32(out + 18, 16);
    put32(out + 22, 16);
    put16(out + 26, 8);
    memcpy(out + 30, "mimetype", 8);
    memcpy(out + 38, mime, 16);
    pos = merged = 54;
    memset(out + pos, 0, 30);
    memcpy(out + pos, "PK\3\4", 4);
    put16(out + pos + 6, descriptor ? 8 : 0);
    put16(out + pos + 8, 8);
    if (!descriptor) {
        put32(out + pos + 14, crc);
        put32(out + pos + 18, (uint32_t)packed_length);
        put32(out + pos + 22, (uint32_t)png_length);
    }
    put16(out + pos + 26, 15);
    /* An extra field in the local header only. */
    put16(out + pos + 28, 8);
    memcpy(out + pos + 30, "mergedimage.png", 15);
    memcpy(out + pos + 45, "UT\4\0abcd", 8);
    pos += 53;
    memcpy(out + pos, packed, packed_length);
    pos += packed_length;
    if (descriptor) {
        memcpy(out + pos, "PK\7\10", 4);
        put32(out + pos + 4, crc);
        put32(out + pos + 8, (uint32_t)packed_length);
        put32(out + pos + 12, (uint32_t)png_length);
        pos += 16;
    }
    directory = pos;
    memset(out + pos, 0, 46);
    memcpy(out + pos, "PK\1\2", 4);
    put32(out + pos + 16, (uint32_t)crc32(0, (const Bytef *)mime, 16));
    put32(out + pos + 20, 16);
    put32(out + pos + 24, 16);
    put16(out + pos + 28, 8);
    memcpy(out + pos + 46, "mimetype", 8);
    pos += 54;
    memset(out + pos, 0, 46);
    memcpy(out + pos, "PK\1\2", 4);
    put16(out + pos + 8, descriptor ? 8 : 0);
    put16(out + pos + 10, 8);
    put32(out + pos + 16, crc);
    put32(out + pos + 20, (uint32_t)packed_length);
    put32(out + pos + 24, (uint32_t)png_length);
    put16(out + pos + 28, 15);
    put32(out + pos + 42, (uint32_t)merged);
    memcpy(out + pos + 46, "mergedimage.png", 15);
    pos += 61;
    /* An archive comment. */
    memset(out + pos, 0, 22);
    memcpy(out + pos, "PK\5\6", 4);
    put16(out + pos + 8, 2);
    put16(out + pos + 10, 2);
    put32(out + pos + 12, (uint32_t)(pos - directory));
    put32(out + pos + 16, (uint32_t)directory);
    put16(out + pos + 20, 7);
    memcpy(out + pos + 22, "comment", 7);
    return pos + 29;
}

static void test_deflate(void)
{
    uint8_t rgba[40 * 30 * 4], *png, *copy;
    size_t n, length, i;
    struct ora_image image;
    int descriptor;

    pattern(rgba, 40, 30, 1);
    rgba_png(rgba, 40, 30, &png, &n);
    for (descriptor = 0; descriptor < 2; descriptor++) {
        length = deflated(buffer, png, n, descriptor);
        assert(ora_decode(buffer, length, &image) == CODEC_OK);
        assert(image.width == 40 && image.height == 30);
        assert(memcmp(image.rgba, rgba, sizeof rgba) == 0);
        ora_free(&image);
    }

    length = deflated(buffer, png, n, 0);
    copy = malloc(length);
    /* Every truncation fails, without reading past the end, except in the
       archive comment, which is only metadata. */
    for (i = 0; i < length; i++) {
        enum codec_result r;
        memcpy(copy, buffer, i);
        r = ora_decode(copy, i, &image);
        assert(i >= length - 7 ? r == CODEC_OK : r != CODEC_OK);
        ora_free(&image);
    }
    /* Corrupt compressed data, CRC, sizes and method. */
    memcpy(copy, buffer, length);
    copy[54 + 53 + 5] ^= 0x55;
    assert(ora_decode(copy, length, &image) != CODEC_OK);
    memcpy(copy, buffer, length);
    put32(central(copy, length, "mergedimage.png") + 16, 1234);
    assert(ora_decode(copy, length, &image) == CODEC_INVALID);
    memcpy(copy, buffer, length);
    put32(central(copy, length, "mergedimage.png") + 24, (uint32_t)n - 1);
    assert(ora_decode(copy, length, &image) == CODEC_INVALID);
    memcpy(copy, buffer, length);
    put32(central(copy, length, "mergedimage.png") + 24, (uint32_t)n + 1);
    assert(ora_decode(copy, length, &image) == CODEC_TRUNCATED);
    memcpy(copy, buffer, length);
    put32(central(copy, length, "mergedimage.png") + 24, 0xfffffff0u);
    assert(ora_decode(copy, length, &image) == CODEC_TOO_LARGE);
    memcpy(copy, buffer, length);
    put32(central(copy, length, "mergedimage.png") + 20, 0x7ffffff0u);
    assert(ora_decode(copy, length, &image) == CODEC_TRUNCATED);
    memcpy(copy, buffer, length);
    put16(central(copy, length, "mergedimage.png") + 10, 12);
    assert(ora_decode(copy, length, &image) == CODEC_INVALID);
    memcpy(copy, buffer, length);
    put16(central(copy, length, "mergedimage.png") + 8, 1);
    assert(ora_decode(copy, length, &image) == CODEC_INVALID);
    memcpy(copy, buffer, length);
    put32(central(copy, length, "mergedimage.png") + 42, (uint32_t)length);
    assert(ora_decode(copy, length, &image) == CODEC_TRUNCATED);
    memcpy(copy, buffer, length);
    put32(central(copy, length, "mergedimage.png") + 42, 1);
    assert(ora_decode(copy, length, &image) == CODEC_INVALID);
    memcpy(copy, buffer, length);
    put16(central(copy, length, "mergedimage.png") + 28, 0xffff);
    assert(ora_decode(copy, length, &image) == CODEC_INVALID);
    free(copy);
    free(png);
}

static void test_directory(void)
{
    uint8_t rgba[2 * 2 * 4], *png, *copy, *eocd;
    size_t n, length, extended;
    struct ora_image image;

    pattern(rgba, 2, 2, 0);
    rgba_png(rgba, 2, 2, &png, &n);
    length = simple(buffer, sizeof buffer, "image/openraster", png, n);
    eocd = buffer + length - 22;
    copy = malloc(length + 200);

    /* Directory offset or size past the end, and an entry count too high. */
    memcpy(copy, buffer, length);
    put32(copy + length - 22 + 16, (uint32_t)length + 5);
    assert(ora_decode(copy, length, &image) == CODEC_TRUNCATED);
    memcpy(copy, buffer, length);
    put32(copy + length - 22 + 12, 0xffff0000u);
    assert(ora_decode(copy, length, &image) != CODEC_OK);
    memcpy(copy, buffer, length);
    put16(copy + length - 22 + 10, 40);
    assert(ora_decode(copy, length, &image) == CODEC_OK);
    ora_free(&image);
    put16(copy + length - 22 + 10, 2);
    assert(ora_decode(copy, length, &image) == CODEC_INVALID);

    /* A zip64 end record and locator, pointed to by a saturated EOCD. */
    memcpy(copy, buffer, length - 22);
    extended = length - 22;
    memset(copy + extended, 0, 56 + 20);
    memcpy(copy + extended, "PK\6\6", 4);
    put32(copy + extended + 4, 44);
    put32(copy + extended + 32, le16(eocd + 10));
    put32(copy + extended + 40, le32(eocd + 12));
    put32(copy + extended + 48, le32(eocd + 16));
    memcpy(copy + extended + 56, "PK\6\7", 4);
    put32(copy + extended + 64, (uint32_t)extended);
    memcpy(copy + extended + 76, eocd, 22);
    put16(copy + extended + 76 + 10, 0xffff);
    put16(copy + extended + 76 + 8, 0xffff);
    put32(copy + extended + 76 + 12, 0xffffffffu);
    put32(copy + extended + 76 + 16, 0xffffffffu);
    assert(ora_decode(copy, extended + 98, &image) == CODEC_OK);
    assert(memcmp(image.rgba, rgba, sizeof rgba) == 0);
    ora_free(&image);
    /* A locator pointing nowhere. */
    put32(copy + extended + 64, (uint32_t)extended + 1000);
    assert(ora_decode(copy, extended + 98, &image) == CODEC_INVALID);

    free(copy);
    free(png);
}

static void test_zip64_extra(void)
{
    /* A central record whose sizes and offset live in a zip64 extra field. */
    static uint8_t zip[512];
    static const char mime[] = "image/openraster";
    size_t pos = 0, directory;
    struct zip_entry e;
    uint8_t *data;
    int i;

    memset(zip, 0, sizeof zip);
    memcpy(zip, "PK\3\4", 4);
    put16(zip + 26, 8);
    memcpy(zip + 30, "mimetype", 8);
    memcpy(zip + 38, mime, 16);
    pos = directory = 54;
    memcpy(zip + pos, "PK\1\2", 4);
    put32(zip + pos + 16, (uint32_t)crc32(0, (const Bytef *)mime, 16));
    put32(zip + pos + 20, 0xffffffffu);
    put32(zip + pos + 24, 0xffffffffu);
    put16(zip + pos + 28, 8);
    put16(zip + pos + 30, 36);
    put32(zip + pos + 42, 0xffffffffu);
    memcpy(zip + pos + 46, "mimetype", 8);
    put16(zip + pos + 54, 0x5455);
    put16(zip + pos + 56, 4);
    put16(zip + pos + 62, 1);
    put16(zip + pos + 64, 24);
    for (i = 0; i < 2; i++)
        put32(zip + pos + 66 + i * 8, 16);
    put32(zip + pos + 82, 0);
    pos += 46 + 8 + 36;
    memcpy(zip + pos, "PK\5\6", 4);
    put16(zip + pos + 8, 1);
    put16(zip + pos + 10, 1);
    put32(zip + pos + 12, (uint32_t)(pos - directory));
    put32(zip + pos + 16, (uint32_t)directory);
    pos += 22;
    assert(zip_find(zip, pos, "mimetype", &e) == CODEC_OK);
    assert(e.size == 16 && e.compressed == 16 && e.data == zip + 38);
    assert(zip_extract(&e, 64, &data) == CODEC_OK);
    assert(memcmp(data, mime, 16) == 0);
    free(data);
    assert(zip_extract(&e, 15, &data) == CODEC_TOO_LARGE);
    /* The zip64 field is too short for the saturated values. */
    put16(zip + directory + 64, 16);
    assert(zip_find(zip, pos, "mimetype", &e) == CODEC_INVALID);
    assert(zip_find(zip, pos, "stack.xml", &e) == CODEC_INVALID);
}

static void test_crc(void)
{
    assert(zip_crc32(0, (const uint8_t *)"123456789", 9) == 0xcbf43926u);
    assert(zip_crc32(zip_crc32(0, (const uint8_t *)"1234", 4), (const uint8_t *)"56789", 5) ==
           0xcbf43926u);
}

int main(void)
{
    test_crc();
    test_round_trip();
    test_types();
    test_deflate();
    test_directory();
    test_zip64_extra();
    return 0;
}
