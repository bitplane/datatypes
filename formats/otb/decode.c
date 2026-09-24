#include "decode.h"
#include <stdlib.h>

#define OTB_MAX_PIXELS (16u * 1024u * 1024u)
#define OTB_MORE 0x80u
#define OTB_COMPRESSED 0x40u
#define OTB_WIDE 0x10u
#define OTB_FRAMES 0x0fu
/* The extension fields are numbered 0 to 15. */
#define OTB_MAX_EXTFIELDS 16u

void otb_free(struct otb_image *image)
{
    free(image->rgba);
    image->rgba = NULL;
    image->width = image->height = 0;
}

static enum codec_result read_size(const uint8_t *data, size_t length, size_t *pos,
                                   int wide, uint32_t *value)
{
    if (wide) {
        if (length - *pos < 2)
            return CODEC_TRUNCATED;
        *value = (uint32_t)data[*pos] << 8 | data[*pos + 1];
        *pos += 2;
    } else {
        if (*pos >= length)
            return CODEC_TRUNCATED;
        *value = data[(*pos)++];
    }
    return CODEC_OK;
}

enum codec_result otb_decode(const uint8_t *data, size_t length, unsigned index,
                             unsigned *count, struct otb_image *image)
{
    uint32_t width, height;
    size_t pos = 1, padded, packed, frame_bytes, stride, bit, x, y;
    unsigned info, frames, extfields = 0;
    uint8_t *out;
    enum codec_result result;

    if (image == NULL)
        return CODEC_INVALID;
    image->width = image->height = 0;
    image->rgba = NULL;
    if (data == NULL || length == 0)
        return CODEC_TRUNCATED;
    info = data[0];
    /* Each extension field's top bit says whether another follows. */
    if (info & OTB_MORE) {
        do {
            if (pos >= length)
                return CODEC_TRUNCATED;
            if (++extfields > OTB_MAX_EXTFIELDS)
                return CODEC_INVALID;
        } while (data[pos++] & OTB_MORE);
    }
    if ((result = read_size(data, length, &pos, info & OTB_WIDE, &width)) != CODEC_OK ||
        (result = read_size(data, length, &pos, info & OTB_WIDE, &height)) != CODEC_OK)
        return result;
    if (pos >= length)
        return CODEC_TRUNCATED;
    /* Depth counts the planes. Only one is defined; the external palette
       (info bit 5) only colours the others, so it is ignored. */
    if (data[pos++] != 1 || width == 0 || height == 0)
        return CODEC_INVALID;
    if ((size_t)width * height > OTB_MAX_PIXELS)
        return CODEC_TOO_LARGE;
    frames = (info & OTB_FRAMES) + 1u;
    if (count != NULL)
        *count = frames;
    /* No compression scheme was ever defined. */
    if ((info & OTB_COMPRESSED) || index >= frames)
        return CODEC_INVALID;

    /* The specification packs rows without padding and pads only the end of
       each bitmap, but ImageMagick pads every row to a byte. Use the padded
       layout whenever the file is long enough for it, and the packed one
       when it is only long enough for that. */
    padded = (size_t)height * ((width + 7u) / 8u);
    packed = ((size_t)width * height + 7u) / 8u;
    if ((length - pos) / frames >= padded) {
        frame_bytes = padded;
        stride = ((width + 7u) / 8u) * 8u;
    } else if ((length - pos) / frames >= packed) {
        frame_bytes = packed;
        stride = width;
    } else {
        return CODEC_TRUNCATED;
    }
    pos += frame_bytes * index;

    image->rgba = malloc((size_t)width * height * 4u);
    if (image->rgba == NULL)
        return CODEC_NO_MEMORY;
    image->width = width;
    image->height = height;
    out = image->rgba;
    for (y = 0; y < height; y++) {
        for (x = 0, bit = y * stride; x < width; x++, bit++) {
            uint8_t value = (data[pos + bit / 8u] & (0x80u >> (bit % 8u))) ? 0 : 255;
            *out++ = value; *out++ = value; *out++ = value; *out++ = 255;
        }
    }
    return CODEC_OK;
}
