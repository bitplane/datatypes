#include "encode.h"
#include <stddef.h>

static void put32(uint8_t *p, uint32_t v, int big_endian)
{
    unsigned i;

    for (i = 0; i < 4; i++)
        p[big_endian ? 3u - i : i] = (uint8_t)(v >> (i * 8u));
}

int avs_make_header(enum avs_variant variant, unsigned width, unsigned height,
                    uint8_t header[8])
{
    if (header == NULL || width == 0 || height == 0 ||
        width > 65535u || height > 65535u)
        return 0;
    put32(header, width, variant == AVS_VARIANT_AVS);
    put32(header + 4, height, variant == AVS_VARIANT_AVS);
    return 1;
}

void avs_encode_row(enum avs_variant variant, const uint8_t *rgba,
                    unsigned width, uint8_t *output)
{
    size_t i;

    for (i = 0; i < width; i++, rgba += 4, output += 4) {
        if (variant == AVS_VARIANT_AVS) {
            output[0] = rgba[3]; output[1] = rgba[0];
            output[2] = rgba[1]; output[3] = rgba[2];
        } else {
            output[0] = rgba[2]; output[1] = rgba[1];
            output[2] = rgba[0]; output[3] = rgba[3];
        }
    }
}
