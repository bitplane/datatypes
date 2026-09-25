#include "encode.h"

/* The longest run written in one character. 95 is DEL, which the format
   allows but terminals may drop. */
#define CIS_MAX_RUN 94u

int cis_frame(unsigned width, unsigned height, unsigned *frame_width, unsigned *frame_height)
{
    if (width == 0 || height == 0)
        return 0;
    if (width <= CIS_MEDIUM_WIDTH && height <= CIS_MEDIUM_HEIGHT) {
        *frame_width = CIS_MEDIUM_WIDTH;
        *frame_height = CIS_MEDIUM_HEIGHT;
    } else {
        *frame_width = CIS_HIGH_WIDTH;
        *frame_height = CIS_HIGH_HEIGHT;
    }
    return 1;
}

static unsigned over_white(const uint8_t *pixel, unsigned channel)
{
    unsigned a = pixel[3];
    return (pixel[channel] * a + 255u * (255u - a) + 127u) / 255u;
}

void cis_threshold_row(const uint8_t *rgba, unsigned width, uint8_t *pixels)
{
    unsigned x;
    for (x = 0; x < width; x++, rgba += 4) {
        unsigned luma = 77u * over_white(rgba, 0) + 150u * over_white(rgba, 1) +
                        29u * over_white(rgba, 2);
        pixels[x] = luma < 128u * 256u;
    }
}

size_t cis_encode_bound(unsigned width, unsigned height)
{
    size_t count = (size_t)width * height;
    /* Header, a leading empty run, one character per run, two more for each
       94 pixels a run continues past, and the trailer. */
    return 3 + 1 + count + 2 * (count / CIS_MAX_RUN) + 3;
}

static uint8_t *put_run(uint8_t *out, size_t run)
{
    while (run > CIS_MAX_RUN) {
        *out++ = 0x20 + CIS_MAX_RUN;
        *out++ = 0x20;
        run -= CIS_MAX_RUN;
    }
    *out++ = (uint8_t)(0x20 + run);
    return out;
}

size_t cis_encode(const uint8_t *pixels, unsigned width, unsigned height,
                  uint8_t *output, size_t capacity)
{
    size_t count = (size_t)width * height, i, run = 0;
    uint8_t *out = output, colour = 1;
    char mode;

    if (width == CIS_MEDIUM_WIDTH && height == CIS_MEDIUM_HEIGHT)
        mode = 'M';
    else if (width == CIS_HIGH_WIDTH && height == CIS_HIGH_HEIGHT)
        mode = 'H';
    else
        return 0;
    if (pixels == NULL || output == NULL || capacity < cis_encode_bound(width, height))
        return 0;
    *out++ = CIS_ESC;
    *out++ = 'G';
    *out++ = (uint8_t)mode;
    for (i = 0; i < count; i++) {
        if ((pixels[i] != 0) == colour) {
            run++;
            continue;
        }
        out = put_run(out, run);
        colour ^= 1u;
        run = 1;
    }
    out = put_run(out, run);
    *out++ = CIS_ESC;
    *out++ = 'G';
    *out++ = 'N';
    return (size_t)(out - output);
}
