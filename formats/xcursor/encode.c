#include "encode.h"
#include "format.h"
#include <string.h>

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

int xcursor_make_header(unsigned width, unsigned height,
                        uint8_t header[XCURSOR_ENCODED_HEADER])
{
    /* libXcursor names an image by its larger side. */
    unsigned nominal = width > height ? width : height;
    uint8_t *chunk = header + XCURSOR_FILE_HEADER + XCURSOR_TOC_ENTRY;

    if (width == 0 || height == 0 ||
        width > XCURSOR_MAX_SIDE || height > XCURSOR_MAX_SIDE)
        return 0;
    memset(header, 0, XCURSOR_ENCODED_HEADER);
    memcpy(header, "Xcur", 4);
    put32(header + 4, XCURSOR_FILE_HEADER);
    put32(header + 8, 0x10000u);
    put32(header + 12, 1);
    put32(header + 16, XCURSOR_IMAGE_TYPE);
    put32(header + 20, nominal);
    put32(header + 24, XCURSOR_FILE_HEADER + XCURSOR_TOC_ENTRY);
    put32(chunk, XCURSOR_IMAGE_HEADER);
    put32(chunk + 4, XCURSOR_IMAGE_TYPE);
    put32(chunk + 8, nominal);
    put32(chunk + 12, 1);
    put32(chunk + 16, width);
    put32(chunk + 20, height);
    /* Hotspot and delay stay zero. */
    return 1;
}

/* x * a / 255, rounded, as xcursorgen premultiplies. */
static uint8_t scale(unsigned x, unsigned a)
{
    unsigned t = x * a + 0x80u;
    return (uint8_t)((t + (t >> 8)) >> 8);
}

void xcursor_encode_row(const uint8_t *rgba, unsigned width, uint8_t *output)
{
    unsigned x;
    for (x = 0; x < width; x++, rgba += 4, output += 4) {
        unsigned a = rgba[3];
        output[0] = scale(rgba[2], a);
        output[1] = scale(rgba[1], a);
        output[2] = scale(rgba[0], a);
        output[3] = (uint8_t)a;
    }
}
