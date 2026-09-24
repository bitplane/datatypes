#include "encode.h"
#include <stdint.h>
#include <string.h>

static unsigned hash(const uint8_t p[4])
{
    return (p[0] * 3u + p[1] * 5u + p[2] * 7u + p[3] * 11u) & 63u;
}

static int delta(uint8_t a, uint8_t b)
{
    int d = (int)a - (int)b;
    if (d > 127) d -= 256;
    if (d < -128) d += 256;
    return d;
}

int qoi_make_header(unsigned width, unsigned height, uint8_t header[14])
{
    if (header == NULL || width == 0 || height == 0 ||
        width > 65535u || height > 65535u)
        return 0;
    memcpy(header, "qoif", 4);
    header[4] = (uint8_t)(width >> 24); header[5] = (uint8_t)(width >> 16);
    header[6] = (uint8_t)(width >> 8); header[7] = (uint8_t)width;
    header[8] = (uint8_t)(height >> 24); header[9] = (uint8_t)(height >> 16);
    header[10] = (uint8_t)(height >> 8); header[11] = (uint8_t)height;
    header[12] = 4; header[13] = 0;
    return 1;
}

void qoi_encoder_init(struct qoi_encoder *encoder)
{
    memset(encoder, 0, sizeof *encoder);
    encoder->previous[3] = 255;
}

size_t qoi_encode_row(struct qoi_encoder *encoder, const uint8_t *rgba,
                      unsigned width, uint8_t *output, size_t capacity)
{
    size_t pos = 0;
    unsigned x;
    if (encoder == NULL || rgba == NULL || output == NULL)
        return SIZE_MAX;
    for (x = 0; x < width; x++) {
        const uint8_t *pixel = rgba + (size_t)x * 4u;
        unsigned slot;
        int dr, dg, db, drdg, dbdg;
        if (memcmp(pixel, encoder->previous, 4) == 0) {
            encoder->run++;
            if (encoder->run == 62) {
                if (capacity - pos < 1) return SIZE_MAX;
                output[pos++] = 0xfdu;
                encoder->run = 0;
            }
            continue;
        }
        if (encoder->run != 0) {
            if (capacity - pos < 1) return SIZE_MAX;
            output[pos++] = (uint8_t)(0xc0u | (encoder->run - 1u));
            encoder->run = 0;
        }
        slot = hash(pixel);
        if (memcmp(encoder->index[slot], pixel, 4) == 0) {
            if (capacity - pos < 1) return SIZE_MAX;
            output[pos++] = (uint8_t)slot;
        } else {
            memcpy(encoder->index[slot], pixel, 4);
            if (pixel[3] != encoder->previous[3]) {
                if (capacity - pos < 5) return SIZE_MAX;
                output[pos++] = 0xff;
                memcpy(output + pos, pixel, 4); pos += 4;
            } else {
                dr = delta(pixel[0], encoder->previous[0]);
                dg = delta(pixel[1], encoder->previous[1]);
                db = delta(pixel[2], encoder->previous[2]);
                drdg = dr - dg; dbdg = db - dg;
                if (dr >= -2 && dr <= 1 && dg >= -2 && dg <= 1 &&
                    db >= -2 && db <= 1) {
                    if (capacity - pos < 1) return SIZE_MAX;
                    output[pos++] = (uint8_t)(0x40u | ((dr + 2) << 4) |
                                               ((dg + 2) << 2) | (db + 2));
                } else if (dg >= -32 && dg <= 31 && drdg >= -8 && drdg <= 7 &&
                           dbdg >= -8 && dbdg <= 7) {
                    if (capacity - pos < 2) return SIZE_MAX;
                    output[pos++] = (uint8_t)(0x80u | (dg + 32));
                    output[pos++] = (uint8_t)(((drdg + 8) << 4) | (dbdg + 8));
                } else {
                    if (capacity - pos < 4) return SIZE_MAX;
                    output[pos++] = 0xfe;
                    memcpy(output + pos, pixel, 3); pos += 3;
                }
            }
        }
        memcpy(encoder->previous, pixel, 4);
    }
    return pos;
}

size_t qoi_encode_end(struct qoi_encoder *encoder, uint8_t *output, size_t capacity)
{
    static const uint8_t end[8] = {0,0,0,0,0,0,0,1};
    size_t pos = 0;
    if (encoder == NULL || output == NULL || capacity < 8u + (encoder->run != 0))
        return SIZE_MAX;
    if (encoder->run != 0) {
        output[pos++] = (uint8_t)(0xc0u | (encoder->run - 1u));
        encoder->run = 0;
    }
    memcpy(output + pos, end, 8);
    return pos + 8;
}
