#include "decode.h"
#include <stdlib.h>
#include <string.h>

#define ROW_BYTES (MACPAINT_WIDTH / 8)
#define PACKED_SIZE ((size_t)ROW_BYTES * MACPAINT_HEIGHT)

static void put_bits(uint8_t *pixels, size_t at, uint8_t byte, size_t count)
{
    uint8_t bits[8];
    unsigned bit;

    for (bit = 0; bit < 8; bit++)
        bits[bit] = (byte >> (7u - bit)) & 1u;
    for (pixels += at * 8u; count > 0; count--, pixels += 8)
        memcpy(pixels, bits, 8);
}

void macpaint_free(struct macpaint_image *image)
{
    free(image->pixels);
    image->pixels = NULL;
    image->width = image->height = 0;
}

enum codec_result macpaint_decode(const uint8_t *data, size_t length, struct macpaint_image *image)
{
    size_t pos, out = 0;

    if (image == NULL)
        return CODEC_INVALID;
    image->width = image->height = 0;
    image->pixels = NULL;
    if (data == NULL || length < 2)
        return CODEC_TRUNCATED;
    /* The version is a big-endian 0, 2 or 3, so the first byte is zero. A
       MacBinary header also starts with zero, then the name length. */
    if (data[0] != 0)
        return CODEC_INVALID;
    pos = MACPAINT_HEADER_SIZE + (data[1] != 0 ? MACPAINT_MACBINARY_SIZE : 0);
    if (length <= pos)
        return CODEC_TRUNCATED;
    image->pixels = malloc(PACKED_SIZE * 8u);
    if (image->pixels == NULL)
        return CODEC_NO_MEMORY;
    /* Runs may cross rows, and one that passes the last pixel is clipped.
       0x80 repeats the next byte 129 times, as ImageMagick and netpbm read it. */
    while (out < PACKED_SIZE) {
        size_t count, take, i;
        uint8_t flag;

        if (pos >= length)
            goto truncated;
        flag = data[pos++];
        count = flag < 128 ? flag + 1u : 257u - flag;
        take = count < PACKED_SIZE - out ? count : PACKED_SIZE - out;
        if (flag < 128) {
            if (length - pos < take)
                goto truncated;
            for (i = 0; i < take; i++)
                put_bits(image->pixels, out + i, data[pos + i], 1);
            pos += take;
        } else {
            if (pos >= length)
                goto truncated;
            put_bits(image->pixels, out, data[pos++], take);
        }
        out += take;
    }
    image->width = MACPAINT_WIDTH;
    image->height = MACPAINT_HEIGHT;
    return CODEC_OK;

truncated:
    macpaint_free(image);
    return CODEC_TRUNCATED;
}
