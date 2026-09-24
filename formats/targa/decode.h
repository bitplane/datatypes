#ifndef BITPLANE_TARGA_DECODE_H
#define BITPLANE_TARGA_DECODE_H

#include <stddef.h>
#include <stdint.h>

enum tga_result {
    TGA_OK = 0,
    TGA_INVALID,
    TGA_TRUNCATED,
    TGA_TOO_LARGE,
    TGA_NO_MEMORY
};

struct tga_image {
    unsigned width;
    unsigned height;
    uint8_t *rgba;
};

enum tga_result tga_decode(const uint8_t *data, size_t length,
                           struct tga_image *image);
void tga_free(struct tga_image *image);

#endif
