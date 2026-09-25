#include "encode.h"
#include "decode.h"
#include <string.h>

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

int ftex_make_header(unsigned width, unsigned height, uint8_t header[FTEX_HEADER_SIZE])
{
    if (width == 0 || height == 0 || width > 65535u || height > 65535u ||
        (unsigned long)width * height > 16ul * 1024ul * 1024ul)
        return 0;
    memcpy(header, "FTEX", 4);
    put32(header + 4, 1);   /* version, as in the game's files */
    put32(header + 8, width);
    put32(header + 12, height);
    put32(header + 16, 1);  /* mip levels */
    put32(header + 20, 1);  /* formats */
    put32(header + 24, FTEX_RGB);
    put32(header + 28, 32); /* where the level starts */
    put32(header + 32, (uint32_t)width * height * 3u);
    return 1;
}

void ftex_encode_row(const uint8_t *rgba, unsigned width, uint8_t *output)
{
    unsigned x, c, a;

    for (x = 0; x < width; x++, rgba += 4, output += 3) {
        a = rgba[3];
        for (c = 0; c < 3; c++)
            output[c] = (uint8_t)((rgba[c] * a + 255u * (255u - a) + 127u) / 255u);
    }
}
