#include "decode.h"

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

enum codec_result dcx_count(const uint8_t *data, size_t length, unsigned *count)
{
    unsigned pages, i;
    size_t end;

    if (count != NULL)
        *count = 0;
    if (data == NULL || length < 4)
        return CODEC_TRUNCATED;
    if (le32(data) != DCX_MAGIC)
        return CODEC_INVALID;
    /* A zero ends the directory; a full directory of 1024 has no room for it. */
    for (pages = 0; pages < DCX_MAX_PAGES; pages++) {
        if ((length - 4u) / 4u <= pages)
            return CODEC_TRUNCATED;
        if (le32(data + 4u + pages * 4u) == 0)
            break;
    }
    if (pages == 0)
        return CODEC_INVALID;
    end = 4u + (size_t)(pages < DCX_MAX_PAGES ? pages + 1u : pages) * 4u;
    for (i = 0; i < pages; i++) {
        uint32_t offset = le32(data + 4u + i * 4u);
        if (offset < end)
            return CODEC_INVALID;
        if (length < 128u || offset > length - 128u)
            return CODEC_TRUNCATED;
    }
    if (count != NULL)
        *count = pages;
    return CODEC_OK;
}

enum codec_result dcx_decode(const uint8_t *data, size_t length, unsigned index,
                             struct pcx_image *image)
{
    unsigned pages, i;
    size_t start, stop = length;
    enum codec_result result;

    if (image == NULL)
        return CODEC_INVALID;
    image->width = image->height = 0;
    image->rgba = NULL;
    result = dcx_count(data, length, &pages);
    if (result != CODEC_OK)
        return result;
    if (index >= pages)
        return CODEC_INVALID;
    /* A page runs to the next page in the file, which the 8-bit palette needs.
       Directory order needn't be file order. */
    start = le32(data + 4u + index * 4u);
    for (i = 0; i < pages; i++) {
        size_t offset = le32(data + 4u + i * 4u);
        if (offset > start && offset < stop)
            stop = offset;
    }
    return pcx_decode(data + start, stop - start, image);
}
