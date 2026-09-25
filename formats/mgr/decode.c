#include "decode.h"
#include <stdlib.h>

void mgr_free(struct mgr_image *image)
{
    free(image->pixels);
    image->pixels = NULL;
    image->width = image->height = 0;
}

/* Like netpbm, each character only has to be at least ' '; MGR itself never
   writes a low character above ' ' + 63. */
static unsigned side(const uint8_t *chars)
{
    if (chars[0] < ' ' || chars[1] < ' ')
        return 0;
    return ((unsigned)(chars[0] - ' ') << 6) + (unsigned)(chars[1] - ' ');
}

enum codec_result mgr_decode(const uint8_t *data, size_t length, struct mgr_image *image)
{
    size_t header, pad, row_bytes, x, y;
    unsigned width, height;

    if (image == NULL)
        return CODEC_INVALID;
    image->width = image->height = 0;
    image->pixels = NULL;
    if (data == NULL || length < 2)
        return CODEC_TRUNCATED;
    if (data[1] != 'z')
        return CODEC_INVALID;
    switch (data[0]) {
    case 'y': header = MGR_HEADER_SIZE; pad = 8; break;
    case 'z': header = MGR_OLD_HEADER_SIZE; pad = 16; break;
    case 'x': header = MGR_OLD_HEADER_SIZE; pad = 32; break;
    default: return CODEC_INVALID;
    }
    if (length < header)
        return CODEC_TRUNCATED;
    /* Only "yz" has a depth. Colour MGR pixmaps index the server's palette,
       which the file doesn't carry. */
    if (header == MGR_HEADER_SIZE && data[6] != ' ' + 1)
        return CODEC_INVALID;
    width = side(data + 2);
    height = side(data + 4);
    /* 4095 x 4095 is within the 16M pixel limit. */
    if (width == 0 || height == 0 || width > MGR_MAX_SIDE || height > MGR_MAX_SIDE)
        return CODEC_INVALID;
    row_bytes = (width + pad - 1) / pad * (pad / 8);
    if ((length - header) / row_bytes < height)
        return CODEC_TRUNCATED;
    image->pixels = malloc((size_t)width * height);
    if (image->pixels == NULL)
        return CODEC_NO_MEMORY;
    data += header;
    for (y = 0; y < height; y++, data += row_bytes)
        for (x = 0; x < width; x++)
            image->pixels[y * width + x] = (data[x / 8] >> (7 - x % 8)) & 1u;
    image->width = width;
    image->height = height;
    return CODEC_OK;
}
