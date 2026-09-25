#include "encode.h"
#include <stddef.h>

int gd_make_header(unsigned width, unsigned height, uint8_t header[GD_HEADER_SIZE])
{
    if (header == NULL || width == 0 || height == 0 ||
        width > 65535u || height > 65535u)
        return 0;
    header[0] = 0xff; header[1] = 0xfe;
    header[2] = (uint8_t)(width >> 8); header[3] = (uint8_t)width;
    header[4] = (uint8_t)(height >> 8); header[5] = (uint8_t)height;
    header[6] = 1;
    header[7] = header[8] = header[9] = header[10] = 0xff;
    return 1;
}

void gd_encode_row(const uint8_t *rgba, unsigned width, uint8_t *output)
{
    unsigned x;

    for (x = 0; x < width; x++, rgba += 4, output += 4) {
        /* Inverts the decoder's 255 - (2a + a / 64) for every gd alpha. */
        output[0] = (uint8_t)((255u - rgba[3]) >> 1);
        output[1] = rgba[0];
        output[2] = rgba[1];
        output[3] = rgba[2];
    }
}
