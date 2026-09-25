#include "decode.h"
#include <stdlib.h>
#include <string.h>

void cis_free(struct cis_image *image)
{
    free(image->pixels);
    image->pixels = NULL;
    image->width = image->height = 0;
}

enum codec_result cis_decode(const uint8_t *data, size_t length, struct cis_image *image)
{
    size_t start, pos, count, filled, run;
    unsigned width, height;
    uint8_t colour = 1;
    int ended = 0;

    if (image == NULL)
        return CODEC_INVALID;
    image->width = image->height = 0;
    image->pixels = NULL;
    if (data == NULL)
        return CODEC_TRUNCATED;
    /* Like cistopbm, skip anything before the first ESC G, such as the tail
       of a terminal capture. */
    for (start = 0; start < length; start++)
        if (data[start] == CIS_ESC && (start + 1 == length || data[start + 1] == 'G'))
            break;
    if (start == length)
        return length == 0 ? CODEC_TRUNCATED : CODEC_INVALID;
    if (length - start < 3)
        return CODEC_TRUNCATED;
    switch (data[start + 2]) {
    case 'M': width = CIS_MEDIUM_WIDTH; height = CIS_MEDIUM_HEIGHT; break;
    case 'H': width = CIS_HIGH_WIDTH; height = CIS_HIGH_HEIGHT; break;
    default: return CODEC_INVALID;
    }
    count = (size_t)width * height;
    image->pixels = malloc(count);
    if (image->pixels == NULL)
        return CODEC_NO_MEMORY;
    memset(image->pixels, 0, count);
    filled = 0;
    for (pos = start + 3; pos < length && filled < count; pos++) {
        /* Drop the parity bit, as a 7-bit terminal would. */
        uint8_t c = data[pos] & 0x7fu;
        if (data[pos] == CIS_ESC) {
            ended = 1;
            break;
        }
        if (c < 0x20)
            continue;
        run = c - 0x20u;
        if (run > count - filled)
            run = count - filled;
        memset(image->pixels + filled, colour, run);
        filled += run;
        colour ^= 1u;
    }
    if (filled < count && !ended) {
        cis_free(image);
        return CODEC_TRUNCATED;
    }
    image->width = width;
    image->height = height;
    return CODEC_OK;
}
