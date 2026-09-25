#include "decode.h"
#include <stdlib.h>

#define AVS_MAX_PIXELS (16u * 1024u * 1024u)
#define AVS_MAX_SIDE 65535u

enum step { STEP_IMAGE, STEP_END, STEP_ERROR };

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static uint32_t le32(const uint8_t *p)
{
    return ((uint32_t)p[3] << 24) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[1] << 8) | p[0];
}

static int fits(uint32_t width, uint32_t height)
{
    return width != 0 && height != 0 &&
           width <= AVS_MAX_SIDE && height <= AVS_MAX_SIDE;
}

int avs_detect(const uint8_t *data, size_t length, enum avs_variant *variant)
{
    /* Sides of at most 65535 leave the high bytes zero, which is in the
       first two bytes of an AVS field and the last two of an AAI one, and a
       side is never zero, so no header fits both. */
    if (data == NULL || length < 8)
        return 0;
    if (fits(be32(data), be32(data + 4)))
        *variant = AVS_VARIANT_AVS;
    else if (fits(le32(data), le32(data + 4)))
        *variant = AVS_VARIANT_AAI;
    else
        return 0;
    return 1;
}

/* Reads the header at *offset. On STEP_IMAGE the image's pixels start at
   *offset + 8; on STEP_ERROR *result says why. */
static enum step step(const uint8_t *data, size_t length, size_t offset,
                      enum avs_variant variant, uint32_t *width,
                      uint32_t *height, enum codec_result *result)
{
    const uint8_t *p = data + offset;

    /* Fewer bytes than a header after an image are trailing padding. */
    if (length - offset < 8)
        return STEP_END;
    *width = variant == AVS_VARIANT_AVS ? be32(p) : le32(p);
    *height = variant == AVS_VARIANT_AVS ? be32(p + 4) : le32(p + 4);
    if (*width == 0 || *height == 0)
        return STEP_END;
    if (*width > AVS_MAX_SIDE || *height > AVS_MAX_SIDE ||
        (size_t)*width * *height > AVS_MAX_PIXELS) {
        *result = CODEC_TOO_LARGE;
        return STEP_ERROR;
    }
    if ((length - offset - 8) / 4 < (size_t)*width * *height) {
        *result = CODEC_TRUNCATED;
        return STEP_ERROR;
    }
    return STEP_IMAGE;
}

unsigned avs_count(const uint8_t *data, size_t length)
{
    enum avs_variant variant;
    enum codec_result result;
    uint32_t width, height;
    size_t offset = 0;
    unsigned count = 0;

    if (!avs_detect(data, length, &variant))
        return 0;
    while (step(data, length, offset, variant, &width, &height, &result) ==
           STEP_IMAGE) {
        offset += 8 + (size_t)width * height * 4u;
        count++;
    }
    return count;
}

void avs_free(struct avs_image *image)
{
    free(image->rgba);
    image->rgba = NULL;
    image->width = image->height = 0;
}

enum codec_result avs_decode(const uint8_t *data, size_t length, unsigned index,
                             struct avs_image *image)
{
    enum avs_variant variant;
    enum codec_result result = CODEC_INVALID;
    uint32_t width = 0, height = 0;
    size_t offset = 0, pixels, i;
    const uint8_t *src;
    uint8_t *dst;
    unsigned seen = 0;
    int any_alpha = 0;

    if (image == NULL)
        return CODEC_INVALID;
    image->width = image->height = 0;
    image->rgba = NULL;
    if (data == NULL || length < 8)
        return CODEC_TRUNCATED;
    if (!avs_detect(data, length, &variant))
        return CODEC_INVALID;
    for (;;) {
        switch (step(data, length, offset, variant, &width, &height, &result)) {
        case STEP_END:
            return CODEC_INVALID;
        case STEP_ERROR:
            return result;
        case STEP_IMAGE:
            break;
        }
        if (seen == index)
            break;
        offset += 8 + (size_t)width * height * 4u;
        seen++;
    }
    pixels = (size_t)width * height;
    image->rgba = malloc(pixels * 4u);
    if (image->rgba == NULL)
        return CODEC_NO_MEMORY;
    image->width = width;
    image->height = height;
    src = data + offset + 8;
    dst = image->rgba;
    for (i = 0; i < pixels; i++, src += 4, dst += 4) {
        if (variant == AVS_VARIANT_AVS) {
            dst[0] = src[1]; dst[1] = src[2]; dst[2] = src[3]; dst[3] = src[0];
            any_alpha |= src[0];
        } else {
            /* Dune's tools and ImageMagick write opaque as 254. */
            dst[0] = src[2]; dst[1] = src[1]; dst[2] = src[0];
            dst[3] = src[3] == 254 ? 255 : src[3];
        }
    }
    /* Original AVS files, such as the format's mandrill.x sample, leave
       alpha zero; later writers use 255 for opaque. */
    if (variant == AVS_VARIANT_AVS && !any_alpha)
        for (i = 0; i < pixels; i++)
            image->rgba[i * 4u + 3u] = 255;
    return CODEC_OK;
}
