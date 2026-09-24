#include "encode.h"
#include <stdio.h>

static const char *const tuple_types[] = {
    "GRAYSCALE", "GRAYSCALE_ALPHA", "RGB", "RGB_ALPHA"
};

unsigned pam_row_needs(const uint8_t *rgba, unsigned width)
{
    unsigned needs = 0, x;
    for (x = 0; x < width; x++, rgba += 4) {
        if (rgba[0] != rgba[1] || rgba[1] != rgba[2])
            needs |= PAM_NEEDS_COLOR;
        if (rgba[3] != 255)
            needs |= PAM_NEEDS_ALPHA;
    }
    return needs;
}

unsigned pam_channels(unsigned needs)
{
    return ((needs & PAM_NEEDS_COLOR) ? 3u : 1u) +
           ((needs & PAM_NEEDS_ALPHA) ? 1u : 0u);
}

size_t pam_make_header(unsigned width, unsigned height, unsigned channels,
                       uint8_t *output, size_t capacity)
{
    int length;
    if (width == 0 || height == 0 || channels == 0 || channels > 4)
        return 0;
    length = snprintf((char *)output, capacity,
                      "P7\nWIDTH %u\nHEIGHT %u\nDEPTH %u\nMAXVAL 255\n"
                      "TUPLTYPE %s\nENDHDR\n",
                      width, height, channels, tuple_types[channels - 1]);
    if (length <= 0 || (size_t)length >= capacity)
        return 0;
    return (size_t)length;
}

size_t pam_encode_row(const uint8_t *rgba, unsigned width, unsigned channels,
                      uint8_t *output, size_t capacity)
{
    unsigned x;
    uint8_t *out = output;
    if (channels == 0 || channels > 4 || (size_t)width * channels > capacity)
        return 0;
    for (x = 0; x < width; x++, rgba += 4) {
        *out++ = rgba[0];
        if (channels >= 3) {
            *out++ = rgba[1];
            *out++ = rgba[2];
        }
        if (channels == 2 || channels == 4)
            *out++ = rgba[3];
    }
    return (size_t)(out - output);
}
