#include "decode.h"
#include <stdlib.h>

void zxscr_colour(unsigned colour, int bright, uint8_t rgb[3])
{
    /* ImageMagick's levels; emulators vary between 0xC0 and 0xD7. */
    uint8_t on = bright ? 255 : 192;

    rgb[0] = (colour & 2u) ? on : 0;
    rgb[1] = (colour & 4u) ? on : 0;
    rgb[2] = (colour & 1u) ? on : 0;
}

size_t zxscr_offset(unsigned x, unsigned y)
{
    /* Thirds of the screen, then pixel row within a cell, then cell row. */
    return ((size_t)(y & 0xC0u) << 5) | ((y & 7u) << 8) | ((y & 0x38u) << 2) | (x >> 3);
}

void zxscr_free(struct zxscr_image *image)
{
    free(image->rgba);
    image->rgba = NULL;
    image->width = image->height = 0;
}

enum codec_result zxscr_decode(const uint8_t *data, size_t length,
                               struct zxscr_image *image)
{
    unsigned x, y;

    if (image == NULL)
        return CODEC_INVALID;
    image->width = image->height = 0;
    image->rgba = NULL;
    if (data == NULL || length < ZXSCR_FILE_SIZE)
        return CODEC_TRUNCATED;
    if (length > ZXSCR_FILE_SIZE)
        return CODEC_INVALID;

    image->rgba = malloc((size_t)ZXSCR_WIDTH * ZXSCR_HEIGHT * 4u);
    if (image->rgba == NULL)
        return CODEC_NO_MEMORY;
    image->width = ZXSCR_WIDTH;
    image->height = ZXSCR_HEIGHT;
    for (y = 0; y < ZXSCR_HEIGHT; y++) {
        uint8_t *dst = image->rgba + (size_t)y * ZXSCR_WIDTH * 4u;
        for (x = 0; x < ZXSCR_WIDTH; x++) {
            unsigned bits = data[zxscr_offset(x, y)];
            unsigned attr = data[6144u + (y / 8u) * 32u + x / 8u];
            unsigned colour = (bits >> (7u - x % 8u) & 1u) ? attr & 7u : attr >> 3 & 7u;
            zxscr_colour(colour, (attr & 0x40u) != 0, dst);
            dst[3] = 255;
            dst += 4;
        }
    }
    return CODEC_OK;
}
