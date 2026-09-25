#include "decode.h"
#include "format.h"
#include <stdlib.h>
#include <string.h>

#define XCURSOR_MAX_PIXELS (16u * 1024u * 1024u)
#define XCURSOR_MAX_TOC 0x10000u

struct entry { unsigned width, height; size_t pixels; };

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

void xcursor_free(struct xcursor_image *image)
{
    free(image->rgba);
    image->rgba = NULL;
    image->width = image->height = 0;
}

/* Check the image chunk a table entry points to. */
static enum codec_result read_entry(const uint8_t *data, size_t length,
                                    const uint8_t *toc, struct entry *entry)
{
    uint64_t pos = le32(toc + 8), header, end;
    const uint8_t *chunk;

    if (pos > length || length - pos < XCURSOR_IMAGE_HEADER)
        return CODEC_TRUNCATED;
    chunk = data + pos;
    header = le32(chunk);
    if (le32(chunk + 4) != XCURSOR_IMAGE_TYPE || le32(chunk + 8) != le32(toc + 4) ||
        header < XCURSOR_IMAGE_HEADER)
        return CODEC_INVALID;
    entry->width = le32(chunk + 16);
    entry->height = le32(chunk + 20);
    /* The hotspot and delay don't affect the picture, so they aren't checked. */
    if (entry->width == 0 || entry->height == 0 ||
        entry->width > XCURSOR_MAX_SIDE || entry->height > XCURSOR_MAX_SIDE)
        return CODEC_INVALID;
    end = pos + header + (uint64_t)entry->width * entry->height * 4u;
    if (end > length)
        return CODEC_TRUNCATED;
    entry->pixels = (size_t)(pos + header);
    return CODEC_OK;
}

/* Straight alpha from premultiplied, as GIMP converts it. */
static void convert(const uint8_t *in, uint8_t *out, size_t pixels)
{
    size_t i;
    for (i = 0; i < pixels; i++, in += 4, out += 4) {
        unsigned a = in[3], c;
        if (a == 0) {
            out[0] = out[1] = out[2] = out[3] = 0;
            continue;
        }
        for (c = 0; c < 3; c++) {
            unsigned v = in[2 - c] * 255u / a;
            out[c] = (uint8_t)(v > 255u ? 255u : v);
        }
        out[3] = (uint8_t)a;
    }
}

enum codec_result xcursor_decode(const uint8_t *data, size_t length, long index,
                                 struct xcursor_image *image, unsigned *count)
{
    struct entry chosen = {0, 0, 0}, entry;
    uint64_t header, ntoc;
    unsigned images = 0;
    size_t i;

    if (image == NULL || count == NULL)
        return CODEC_INVALID;
    image->width = image->height = 0;
    image->rgba = NULL;
    *count = 0;
    if (data == NULL || length < 4)
        return CODEC_TRUNCATED;
    if (memcmp(data, "Xcur", 4) != 0)
        return CODEC_INVALID;
    if (length < XCURSOR_FILE_HEADER)
        return CODEC_TRUNCATED;
    header = le32(data + 4);
    ntoc = le32(data + 12);
    if (header < XCURSOR_FILE_HEADER || ntoc > XCURSOR_MAX_TOC)
        return CODEC_INVALID;
    if (header + ntoc * XCURSOR_TOC_ENTRY > length)
        return CODEC_TRUNCATED;
    for (i = 0; i < ntoc; i++) {
        const uint8_t *toc = data + header + i * XCURSOR_TOC_ENTRY;
        enum codec_result result;
        /* Comments and unknown chunk types are skipped. */
        if (le32(toc) != XCURSOR_IMAGE_TYPE)
            continue;
        result = read_entry(data, length, toc, &entry);
        if (result != CODEC_OK)
            return result;
        if (index == XCURSOR_BEST ?
                (uint64_t)entry.width * entry.height >
                    (uint64_t)chosen.width * chosen.height :
                (long)images == index)
            chosen = entry;
        images++;
    }
    if (images == 0)
        return CODEC_INVALID;
    *count = images;
    if (chosen.width == 0)
        return CODEC_INVALID;
    if ((uint64_t)chosen.width * chosen.height > XCURSOR_MAX_PIXELS)
        return CODEC_TOO_LARGE;
    image->rgba = malloc((size_t)chosen.width * chosen.height * 4u);
    if (image->rgba == NULL)
        return CODEC_NO_MEMORY;
    image->width = chosen.width;
    image->height = chosen.height;
    convert(data + chosen.pixels, image->rgba, (size_t)chosen.width * chosen.height);
    return CODEC_OK;
}
