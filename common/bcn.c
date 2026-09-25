#include "common/bcn.h"

/* Integer arithmetic follows the common decoders: 5:6:5 endpoints widened by
   bit replication, and interpolated values rounded down. */

static void expand565(unsigned c, unsigned rgb[3])
{
    unsigned r = (c >> 11) & 31u, g = (c >> 5) & 63u, b = c & 31u;
    rgb[0] = (r << 3) | (r >> 2);
    rgb[1] = (g << 2) | (g >> 4);
    rgb[2] = (b << 3) | (b >> 2);
}

/* four_colour forces the four-colour mode that BC2 and BC3 always use. */
static void colour_block(const uint8_t *in, uint8_t *out, int four_colour,
                         int punch_through)
{
    unsigned c0 = in[0] | (unsigned)in[1] << 8, c1 = in[2] | (unsigned)in[3] << 8;
    unsigned e0[3], e1[3], i, k;
    uint8_t palette[4][4];

    expand565(c0, e0);
    expand565(c1, e1);
    for (k = 0; k < 3; k++) {
        palette[0][k] = (uint8_t)e0[k];
        palette[1][k] = (uint8_t)e1[k];
        if (four_colour || c0 > c1) {
            palette[2][k] = (uint8_t)((2u * e0[k] + e1[k]) / 3u);
            palette[3][k] = (uint8_t)((e0[k] + 2u * e1[k]) / 3u);
        } else {
            palette[2][k] = (uint8_t)((e0[k] + e1[k]) / 2u);
            palette[3][k] = 0;
        }
    }
    palette[0][3] = palette[1][3] = palette[2][3] = palette[3][3] = 255;
    if (!four_colour && c0 <= c1 && punch_through)
        palette[3][3] = 0;
    for (i = 0; i < 16; i++) {
        unsigned index = (in[4 + i / 4u] >> (2u * (i % 4u))) & 3u;
        for (k = 0; k < 4; k++)
            out[i * 4u + k] = palette[index][k];
    }
}

/* The BC3 alpha block, also BC4's and BC5's channels. Each value is written to
   out[i * 4], so point out at the channel. */
static void ramp_block(const uint8_t *in, uint8_t *out, int is_signed)
{
    unsigned a0 = in[0], a1 = in[1], ramp[8], i;
    uint64_t bits = 0;

    if (is_signed) {
        a0 = (a0 ^ 0x80u);
        a1 = (a1 ^ 0x80u);
    }
    ramp[0] = a0;
    ramp[1] = a1;
    if (a0 > a1) {
        for (i = 1; i < 7; i++)
            ramp[i + 1] = ((7u - i) * a0 + i * a1) / 7u;
    } else {
        for (i = 1; i < 5; i++)
            ramp[i + 1] = ((5u - i) * a0 + i * a1) / 5u;
        ramp[6] = 0;
        ramp[7] = 255;
    }
    for (i = 0; i < 6; i++)
        bits |= (uint64_t)in[2 + i] << (8u * i);
    for (i = 0; i < 16; i++)
        out[i * 4u] = (uint8_t)ramp[(bits >> (3u * i)) & 7u];
}

void bc1_block(const uint8_t *in, uint8_t *out, int punch_through)
{
    colour_block(in, out, 0, punch_through);
}

void bc2_block(const uint8_t *in, uint8_t *out)
{
    unsigned i;
    colour_block(in + 8, out, 1, 0);
    for (i = 0; i < 16; i++) {
        unsigned a = (in[i / 2u] >> (4u * (i % 2u))) & 15u;
        out[i * 4u + 3u] = (uint8_t)(a * 17u);
    }
}

void bc3_block(const uint8_t *in, uint8_t *out)
{
    colour_block(in + 8, out, 1, 0);
    ramp_block(in, out + 3, 0);
}

void bc4_block(const uint8_t *in, uint8_t *out)
{
    unsigned i;
    ramp_block(in, out, 0);
    for (i = 0; i < 16; i++) {
        out[i * 4u + 1u] = out[i * 4u + 2u] = out[i * 4u];
        out[i * 4u + 3u] = 255;
    }
}

void bc5_block(const uint8_t *in, uint8_t *out, int is_signed)
{
    unsigned i;
    ramp_block(in, out, is_signed);
    ramp_block(in + 8, out + 1, is_signed);
    for (i = 0; i < 16; i++) {
        out[i * 4u + 2u] = is_signed ? 128 : 0;
        out[i * 4u + 3u] = 255;
    }
}
