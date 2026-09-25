#include "gimp.h"
#include <stdlib.h>
#include <string.h>

/* A version 1 brush has no magic, so its header has to add up exactly. */
static int is_old_brush(const uint8_t *data, size_t length)
{
    uint32_t header, width, height, bytes;

    if (length < 20 || gimp_be32(data + 4) != 1)
        return 0;
    header = gimp_be32(data);
    width = gimp_be32(data + 8);
    height = gimp_be32(data + 12);
    bytes = gimp_be32(data + 16);
    return header >= 20 && width != 0 && height != 0 &&
           (bytes == 1 || bytes == 4) &&
           header + (uint64_t)width * height * bytes <= length;
}

enum gimp_kind gimp_sniff(const uint8_t *data, size_t length)
{
    size_t header;
    unsigned long cells;

    if (data == NULL)
        return GIMP_UNKNOWN;
    if (length >= 9 && memcmp(data, "gimp xcf ", 9) == 0)
        return GIMP_XCF;
    if (length >= 24 && memcmp(data + 20, "GIMP", 4) == 0 &&
        (gimp_be32(data + 4) == 2 || gimp_be32(data + 4) == 3))
        return GIMP_GBR;
    if (length >= 24 && memcmp(data + 20, "GPAT", 4) == 0)
        return GIMP_PAT;
    if (is_old_brush(data, length))
        return GIMP_GBR;
    if (gih_parse_header(data, length, &header, &cells) == CODEC_OK)
        return GIMP_GIH;
    return GIMP_UNKNOWN;
}

void gimp_free(struct gimp_image *image)
{
    free(image->rgba);
    image->rgba = NULL;
    image->width = image->height = 0;
}

enum codec_result gimp_decode(const uint8_t *data, size_t length, long index,
                              struct gimp_image *image, unsigned *count)
{
    enum codec_result result;

    if (image == NULL || count == NULL)
        return CODEC_INVALID;
    image->width = image->height = 0;
    image->rgba = NULL;
    *count = 0;
    if (data == NULL || length == 0)
        return CODEC_TRUNCATED;
    switch (gimp_sniff(data, length)) {
    case GIMP_GIH:
        return gih_decode(data, length, index, image, count);
    case GIMP_GBR:
        result = gbr_decode(data, length, image);
        break;
    case GIMP_PAT:
        result = pat_decode(data, length, image);
        break;
    case GIMP_XCF:
        result = xcf_decode(data, length, image);
        break;
    default:
        /* Too short to tell what it is. */
        return length < 24 ? CODEC_TRUNCATED : CODEC_INVALID;
    }
    if (result == CODEC_OK) {
        *count = 1;
        if (index != 0) {
            gimp_free(image);
            result = CODEC_INVALID;
        }
    }
    return result;
}
