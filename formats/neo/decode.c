#include "decode.h"
#include "common/atarist.h"
#include <stdlib.h>

static unsigned be16(const uint8_t *p)
{
    return ((unsigned)p[0] << 8) | p[1];
}

void neo_free(struct neo_image *image)
{
    free(image->rgba);
    image->rgba = NULL;
    image->width = image->height = 0;
}

enum codec_result neo_decode(const uint8_t *data, size_t length,
                             struct neo_image *image)
{
    static const uint8_t mono[2][3] = { { 255, 255, 255 }, { 0, 0, 0 } };
    uint8_t palette[16][3];
    unsigned resolution, planes, colours, width, height, x, y;

    if (image == NULL)
        return CODEC_INVALID;
    image->width = image->height = 0;
    image->rgba = NULL;
    if (data == NULL || length < 4)
        return CODEC_TRUNCATED;
    resolution = be16(data + 2);
    if (be16(data) != 0 || resolution > 2)
        return CODEC_INVALID;
    /* Bytes after the image are ignored. */
    if (length < NEO_FILE_SIZE)
        return CODEC_TRUNCATED;
    planes = 4u >> resolution;
    colours = 1u << planes;
    width = resolution == 0 ? 320u : 640u;
    height = resolution == 2 ? 400u : 200u;

    /* Colours the resolution doesn't use often hold garbage, so they
       don't decide whether the palette is STE. */
    st_palette(data + 4, colours, palette);

    image->rgba = malloc((size_t)width * height * 4u);
    if (image->rgba == NULL)
        return CODEC_NO_MEMORY;
    image->width = width;
    image->height = height;
    for (y = 0; y < height; y++) {
        /* Each line is groups of 16 pixels, one word per plane. */
        const uint8_t *line = data + 128 + (size_t)y * width * planes / 8u;
        uint8_t *dst = image->rgba + (size_t)y * width * 4u;
        for (x = 0; x < width; x++) {
            unsigned index = st_pixel(line, planes, x);
            const uint8_t *rgb = resolution == 2 ? mono[index] : palette[index];
            dst[0] = rgb[0];
            dst[1] = rgb[1];
            dst[2] = rgb[2];
            dst[3] = 255;
            dst += 4;
        }
    }
    return CODEC_OK;
}
