#include "encode.h"
#include <string.h>

static void put_le16(uint8_t *p, unsigned value)
{
    p[0] = (uint8_t)value; p[1] = (uint8_t)(value >> 8);
}

static void put_le32(uint8_t *p, unsigned long value)
{
    put_le16(p, (unsigned)(value & 0xffffu));
    put_le16(p + 2, (unsigned)(value >> 16));
}

int pixar_make_header(unsigned width, unsigned height, int alpha,
                      uint8_t header[PIXAR_ENCODE_HEADER])
{
    if (header == NULL || width == 0 || height == 0 || width > 65535u ||
        height > 65535u || (unsigned long)width * height > 16ul * 1024ul * 1024ul)
        return 0;
    memset(header, 0, PIXAR_ENCODE_HEADER);
    header[0] = 0x80; header[1] = 0xe8;
    put_le16(header + 4, 1);                  /* Version, as Photoshop writes. */
    put_le16(header + 416, height);
    put_le16(header + 418, width);
    put_le16(header + 420, height);           /* One tile covers the picture. */
    put_le16(header + 422, width);
    put_le16(header + 424, alpha ? 15 : 14);  /* RGBA or RGB. */
    put_le16(header + 426, 2);                /* 8-bit dumped. */
    put_le16(header + 428, 1024);             /* Blocking factor. */
    put_le16(header + 430, alpha ? 1 : 0);    /* Unassociated alpha. */
    put_le32(header + 512, PIXAR_ENCODE_HEADER);
    put_le32(header + 516, (unsigned long)width * height * (alpha ? 4u : 3u));
    return 1;
}

static uint8_t over_white(const uint8_t *pixel, unsigned plane)
{
    unsigned a = pixel[3];
    return (uint8_t)((pixel[plane] * a + 255u * (255u - a) + 127u) / 255u);
}

size_t pixar_encode_row(const uint8_t *rgba, unsigned width, int alpha,
                        uint8_t *output, size_t capacity)
{
    size_t size, pos = 0;
    unsigned x;
    if (rgba == NULL || output == NULL || width == 0 || width > 65535u)
        return 0;
    size = (size_t)width * (alpha ? 4u : 3u);
    if (capacity < size)
        return 0;
    if (alpha) {
        memcpy(output, rgba, size);
        return size;
    }
    for (x = 0; x < width; x++) {
        output[pos++] = over_white(rgba + x * 4u, 0);
        output[pos++] = over_white(rgba + x * 4u, 1);
        output[pos++] = over_white(rgba + x * 4u, 2);
    }
    return pos;
}
