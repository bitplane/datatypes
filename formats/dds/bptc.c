#include <stddef.h>

#include "bcn.h"

/* BC6H and BC7, from the ARB_texture_compression_bptc specification. */

/* Subset of each pixel, two bits per pixel from bit 0. */
static const uint32_t partition2[64] = {
    0x50505050u, 0x40404040u, 0x54545454u, 0x54505040u, 0x50404000u, 0x55545450u,
    0x55545040u, 0x54504000u, 0x50400000u, 0x55555450u, 0x55544000u, 0x54400000u,
    0x55555440u, 0x55550000u, 0x55555500u, 0x55000000u, 0x55150100u, 0x00004054u,
    0x15010000u, 0x00405054u, 0x00004050u, 0x15050100u, 0x05010000u, 0x40505054u,
    0x00404050u, 0x05010100u, 0x14141414u, 0x05141450u, 0x01155440u, 0x00555500u,
    0x15014054u, 0x05414150u, 0x44444444u, 0x55005500u, 0x11441144u, 0x05055050u,
    0x05500550u, 0x11114444u, 0x41144114u, 0x44111144u, 0x15055054u, 0x01055040u,
    0x05041050u, 0x05455150u, 0x14414114u, 0x50050550u, 0x41411414u, 0x00141400u,
    0x00041504u, 0x00105410u, 0x10541000u, 0x04150400u, 0x50410514u, 0x41051450u,
    0x05415014u, 0x14054150u, 0x41050514u, 0x41505014u, 0x40011554u, 0x54150140u,
    0x50505500u, 0x00555050u, 0x15151010u, 0x54540404u
};
static const uint32_t partition3[64] = {
    0xaa685050u, 0x6a5a5040u, 0x5a5a4200u, 0x5450a0a8u, 0xa5a50000u, 0xa0a05050u,
    0x5555a0a0u, 0x5a5a5050u, 0xaa550000u, 0xaa555500u, 0xaaaa5500u, 0x90909090u,
    0x94949494u, 0xa4a4a4a4u, 0xa9a59450u, 0x2a0a4250u, 0xa5945040u, 0x0a425054u,
    0xa5a5a500u, 0x55a0a0a0u, 0xa8a85454u, 0x6a6a4040u, 0xa4a45000u, 0x1a1a0500u,
    0x0050a4a4u, 0xaaa59090u, 0x14696914u, 0x69691400u, 0xa08585a0u, 0xaa821414u,
    0x50a4a450u, 0x6a5a0200u, 0xa9a58000u, 0x5090a0a8u, 0xa8a09050u, 0x24242424u,
    0x00aa5500u, 0x24924924u, 0x24499224u, 0x50a50a50u, 0x500aa550u, 0xaaaa4444u,
    0x66660000u, 0xa5a0a5a0u, 0x50a050a0u, 0x69286928u, 0x44aaaa44u, 0x66666600u,
    0xaa444444u, 0x54a854a8u, 0x95809580u, 0x96969600u, 0xa85454a8u, 0x80959580u,
    0xaa141414u, 0x96960000u, 0xaaaa1414u, 0xa05050a0u, 0xa0a5a5a0u, 0x96000000u,
    0x40804080u, 0xa9a8a9a8u, 0xaaaaaa44u, 0x2a4a5254u
};
/* Anchor pixel of the second subset, and of the second and third of three. */
static const uint8_t anchor2[64] = {
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
    15, 2, 8, 2, 2, 8, 8, 15, 2, 8, 2, 2, 8, 8, 2, 2,
    15, 15, 6, 8, 2, 8, 15, 15, 2, 8, 2, 2, 2, 15, 15, 6,
    6, 2, 6, 8, 15, 15, 2, 2, 15, 15, 15, 15, 15, 2, 2, 15
};
static const uint8_t anchor3a[64] = {
    3, 3, 15, 15, 8, 3, 15, 15, 8, 8, 6, 6, 6, 5, 3, 3,
    3, 3, 8, 15, 3, 3, 6, 10, 5, 8, 8, 6, 8, 5, 15, 15,
    8, 15, 3, 5, 6, 10, 8, 15, 15, 3, 15, 5, 15, 15, 15, 15,
    3, 15, 5, 5, 5, 8, 5, 10, 5, 10, 8, 13, 15, 12, 3, 3
};
static const uint8_t anchor3b[64] = {
    15, 8, 8, 3, 15, 15, 3, 8, 15, 15, 15, 15, 15, 15, 15, 8,
    15, 8, 15, 3, 15, 8, 15, 8, 3, 15, 6, 10, 15, 15, 10, 8,
    15, 3, 15, 10, 10, 8, 9, 10, 6, 15, 8, 15, 3, 6, 6, 8,
    15, 3, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 3, 15, 15, 8
};
/* Endpoint bit for each header bit after the mode: field * 16 + bit,
   fields r0 g0 b0 r1 g1 b1 r2 g2 b2 r3 g3 b3. */
/* mode 0: 75 bits */
static const uint8_t layout0[75] = {
    116, 132, 180, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 16, 17, 18,
    19, 20, 21, 22, 23, 24, 25, 32, 33, 34, 35, 36, 37, 38, 39, 40,
    41, 48, 49, 50, 51, 52, 164, 112, 113, 114, 115, 64, 65, 66, 67, 68,
    176, 160, 161, 162, 163, 80, 81, 82, 83, 84, 177, 128, 129, 130, 131, 96,
    97, 98, 99, 100, 178, 144, 145, 146, 147, 148, 179
};
/* mode 1: 75 bits */
static const uint8_t layout1[75] = {
    117, 164, 165, 0, 1, 2, 3, 4, 5, 6, 176, 177, 132, 16, 17, 18,
    19, 20, 21, 22, 133, 178, 116, 32, 33, 34, 35, 36, 37, 38, 179, 181,
    180, 48, 49, 50, 51, 52, 53, 112, 113, 114, 115, 64, 65, 66, 67, 68,
    69, 160, 161, 162, 163, 80, 81, 82, 83, 84, 85, 128, 129, 130, 131, 96,
    97, 98, 99, 100, 101, 144, 145, 146, 147, 148, 149
};
/* mode 2: 72 bits */
static const uint8_t layout2[72] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 16, 17, 18, 19, 20, 21,
    22, 23, 24, 25, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 48, 49,
    50, 51, 52, 10, 112, 113, 114, 115, 64, 65, 66, 67, 26, 176, 160, 161,
    162, 163, 80, 81, 82, 83, 42, 177, 128, 129, 130, 131, 96, 97, 98, 99,
    100, 178, 144, 145, 146, 147, 148, 179
};
/* mode 6: 72 bits */
static const uint8_t layout6[72] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 16, 17, 18, 19, 20, 21,
    22, 23, 24, 25, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 48, 49,
    50, 51, 10, 164, 112, 113, 114, 115, 64, 65, 66, 67, 68, 26, 160, 161,
    162, 163, 80, 81, 82, 83, 42, 177, 128, 129, 130, 131, 96, 97, 98, 99,
    176, 178, 144, 145, 146, 147, 116, 179
};
/* mode 10: 72 bits */
static const uint8_t layout10[72] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 16, 17, 18, 19, 20, 21,
    22, 23, 24, 25, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 48, 49,
    50, 51, 10, 132, 112, 113, 114, 115, 64, 65, 66, 67, 26, 176, 160, 161,
    162, 163, 80, 81, 82, 83, 84, 42, 128, 129, 130, 131, 96, 97, 98, 99,
    177, 178, 144, 145, 146, 147, 180, 179
};
/* mode 14: 72 bits */
static const uint8_t layout14[72] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 132, 16, 17, 18, 19, 20, 21,
    22, 23, 24, 116, 32, 33, 34, 35, 36, 37, 38, 39, 40, 180, 48, 49,
    50, 51, 52, 164, 112, 113, 114, 115, 64, 65, 66, 67, 68, 176, 160, 161,
    162, 163, 80, 81, 82, 83, 84, 177, 128, 129, 130, 131, 96, 97, 98, 99,
    100, 178, 144, 145, 146, 147, 148, 179
};
/* mode 18: 72 bits */
static const uint8_t layout18[72] = {
    0, 1, 2, 3, 4, 5, 6, 7, 164, 132, 16, 17, 18, 19, 20, 21,
    22, 23, 178, 116, 32, 33, 34, 35, 36, 37, 38, 39, 179, 180, 48, 49,
    50, 51, 52, 53, 112, 113, 114, 115, 64, 65, 66, 67, 68, 176, 160, 161,
    162, 163, 80, 81, 82, 83, 84, 177, 128, 129, 130, 131, 96, 97, 98, 99,
    100, 101, 144, 145, 146, 147, 148, 149
};
/* mode 22: 72 bits */
static const uint8_t layout22[72] = {
    0, 1, 2, 3, 4, 5, 6, 7, 176, 132, 16, 17, 18, 19, 20, 21,
    22, 23, 117, 116, 32, 33, 34, 35, 36, 37, 38, 39, 165, 180, 48, 49,
    50, 51, 52, 164, 112, 113, 114, 115, 64, 65, 66, 67, 68, 69, 160, 161,
    162, 163, 80, 81, 82, 83, 84, 177, 128, 129, 130, 131, 96, 97, 98, 99,
    100, 178, 144, 145, 146, 147, 148, 179
};
/* mode 26: 72 bits */
static const uint8_t layout26[72] = {
    0, 1, 2, 3, 4, 5, 6, 7, 177, 132, 16, 17, 18, 19, 20, 21,
    22, 23, 133, 116, 32, 33, 34, 35, 36, 37, 38, 39, 181, 180, 48, 49,
    50, 51, 52, 164, 112, 113, 114, 115, 64, 65, 66, 67, 68, 176, 160, 161,
    162, 163, 80, 81, 82, 83, 84, 85, 128, 129, 130, 131, 96, 97, 98, 99,
    100, 178, 144, 145, 146, 147, 148, 179
};
/* mode 30: 72 bits */
static const uint8_t layout30[72] = {
    0, 1, 2, 3, 4, 5, 164, 176, 177, 132, 16, 17, 18, 19, 20, 21,
    117, 133, 178, 116, 32, 33, 34, 35, 36, 37, 165, 179, 181, 180, 48, 49,
    50, 51, 52, 53, 112, 113, 114, 115, 64, 65, 66, 67, 68, 69, 160, 161,
    162, 163, 80, 81, 82, 83, 84, 85, 128, 129, 130, 131, 96, 97, 98, 99,
    100, 101, 144, 145, 146, 147, 148, 149
};
/* mode 3: 60 bits */
static const uint8_t layout3[60] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 16, 17, 18, 19, 20, 21,
    22, 23, 24, 25, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 48, 49,
    50, 51, 52, 53, 54, 55, 56, 57, 64, 65, 66, 67, 68, 69, 70, 71,
    72, 73, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89
};
/* mode 7: 60 bits */
static const uint8_t layout7[60] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 16, 17, 18, 19, 20, 21,
    22, 23, 24, 25, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 48, 49,
    50, 51, 52, 53, 54, 55, 56, 10, 64, 65, 66, 67, 68, 69, 70, 71,
    72, 26, 80, 81, 82, 83, 84, 85, 86, 87, 88, 42
};
/* mode 11: 60 bits */
static const uint8_t layout11[60] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 16, 17, 18, 19, 20, 21,
    22, 23, 24, 25, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 48, 49,
    50, 51, 52, 53, 54, 55, 11, 10, 64, 65, 66, 67, 68, 69, 70, 71,
    27, 26, 80, 81, 82, 83, 84, 85, 86, 87, 43, 42
};
/* mode 15: 60 bits */
static const uint8_t layout15[60] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 16, 17, 18, 19, 20, 21,
    22, 23, 24, 25, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 48, 49,
    50, 51, 15, 14, 13, 12, 11, 10, 64, 65, 66, 67, 31, 30, 29, 28,
    27, 26, 80, 81, 82, 83, 47, 46, 45, 44, 43, 42
};

/* Reads a 128-bit block from bit 0 of byte 0 upwards. */
struct bits {
    uint64_t low, high;
    unsigned position;
};

static void load_bits(struct bits *b, const uint8_t *in)
{
    unsigned i;
    b->low = b->high = 0;
    for (i = 0; i < 8; i++) {
        b->low |= (uint64_t)in[i] << (8u * i);
        b->high |= (uint64_t)in[8 + i] << (8u * i);
    }
    b->position = 0;
}

/* Takes count bits, 0 to 16. Reading past the end gives zeros. */
static unsigned take(struct bits *b, unsigned count)
{
    unsigned p = b->position;
    uint64_t v;

    if (count == 0)
        return 0;
    if (p >= 128)
        v = 0;
    else if (p >= 64)
        v = b->high >> (p - 64u);
    else if (p == 0)
        v = b->low;
    else
        v = (b->low >> p) | (b->high << (64u - p));
    b->position = p + count;
    return (unsigned)(v & ((1u << count) - 1u));
}

static const uint8_t weights2[4] = { 0, 21, 43, 64 };
static const uint8_t weights3[8] = { 0, 9, 18, 27, 37, 46, 55, 64 };
static const uint8_t weights4[16] = {
    0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64
};

static unsigned weight(unsigned bits, unsigned index)
{
    return bits == 2 ? weights2[index] : bits == 3 ? weights3[index] : weights4[index];
}

static unsigned subset_of(unsigned subsets, unsigned partition, unsigned pixel)
{
    if (subsets == 2)
        return (partition2[partition] >> (2u * pixel)) & 3u;
    if (subsets == 3)
        return (partition3[partition] >> (2u * pixel)) & 3u;
    return 0;
}

/* An anchor pixel's index is stored with one bit fewer. */
static int is_anchor(unsigned subsets, unsigned partition, unsigned pixel)
{
    if (pixel == 0)
        return 1;
    if (subsets == 2)
        return pixel == anchor2[partition];
    if (subsets == 3)
        return pixel == anchor3a[partition] || pixel == anchor3b[partition];
    return 0;
}

static const struct bc7_mode {
    uint8_t subsets, partition_bits, rotation_bits, selection_bits;
    uint8_t colour_bits, alpha_bits, endpoint_pbits, shared_pbits;
    uint8_t index_bits, index2_bits;
} bc7_modes[8] = {
    { 3, 4, 0, 0, 4, 0, 1, 0, 3, 0 },
    { 2, 6, 0, 0, 6, 0, 0, 1, 3, 0 },
    { 3, 6, 0, 0, 5, 0, 0, 0, 2, 0 },
    { 2, 6, 0, 0, 7, 0, 1, 0, 2, 0 },
    { 1, 0, 2, 1, 5, 6, 0, 0, 2, 3 },
    { 1, 0, 2, 0, 7, 8, 0, 0, 2, 2 },
    { 1, 0, 0, 0, 7, 7, 1, 0, 4, 0 },
    { 2, 6, 0, 0, 5, 5, 1, 0, 2, 0 }
};

/* Widen a precision-bit value to 8 bits by replicating its top bits. */
static unsigned widen(unsigned v, unsigned precision)
{
    v <<= 8u - precision;
    return v | (v >> precision);
}

void bc7_block(const uint8_t *in, uint8_t *out)
{
    const struct bc7_mode *m;
    struct bits b;
    unsigned mode = 0, partition, rotation, selection, endpoints, e, c, i;
    unsigned ep[6][4], pbit[6] = { 0 }, index1[16], index2[16];
    unsigned has_pbit;

    while (mode < 8 && !((in[0] >> mode) & 1u))
        mode++;
    if (mode == 8) {
        for (i = 0; i < 64; i++)
            out[i] = (i % 4u) == 3 ? 255 : 0;
        return;
    }
    m = &bc7_modes[mode];
    load_bits(&b, in);
    b.position = mode + 1u;
    partition = take(&b, m->partition_bits);
    rotation = take(&b, m->rotation_bits);
    selection = take(&b, m->selection_bits);
    endpoints = 2u * m->subsets;
    for (c = 0; c < 3; c++)
        for (e = 0; e < endpoints; e++)
            ep[e][c] = take(&b, m->colour_bits);
    for (e = 0; e < endpoints; e++)
        ep[e][3] = take(&b, m->alpha_bits);
    if (m->endpoint_pbits)
        for (e = 0; e < endpoints; e++)
            pbit[e] = take(&b, 1);
    if (m->shared_pbits)
        for (e = 0; e < endpoints; e += 2)
            pbit[e] = pbit[e + 1u] = take(&b, 1);
    has_pbit = m->endpoint_pbits | m->shared_pbits;
    for (e = 0; e < endpoints; e++) {
        for (c = 0; c < 4; c++) {
            unsigned bits = c == 3 ? m->alpha_bits : m->colour_bits;
            if (bits == 0) {
                ep[e][c] = 255;
                continue;
            }
            if (has_pbit) {
                ep[e][c] = (ep[e][c] << 1) | pbit[e];
                bits++;
            }
            ep[e][c] = widen(ep[e][c], bits);
        }
    }
    for (i = 0; i < 16; i++)
        index1[i] = take(&b, m->index_bits - (unsigned)is_anchor(m->subsets, partition, i));
    for (i = 0; i < 16; i++)
        index2[i] = m->index2_bits ? take(&b, m->index2_bits - (i == 0)) : index1[i];
    for (i = 0; i < 16; i++) {
        unsigned s = subset_of(m->subsets, partition, i), t;
        unsigned colour_index = index1[i], colour_bits = m->index_bits;
        unsigned alpha_index = index2[i];
        unsigned alpha_bits = m->index2_bits ? m->index2_bits : m->index_bits;
        uint8_t *p = out + i * 4u;

        if (selection) {
            colour_index = index2[i];
            colour_bits = m->index2_bits;
            alpha_index = index1[i];
            alpha_bits = m->index_bits;
        }
        for (c = 0; c < 4; c++) {
            unsigned w = c == 3 ? weight(alpha_bits, alpha_index)
                                : weight(colour_bits, colour_index);
            p[c] = (uint8_t)(((64u - w) * ep[2u * s][c] + w * ep[2u * s + 1u][c] + 32u) >> 6);
        }
        if (rotation) {
            t = p[3];
            p[3] = p[rotation - 1u];
            p[rotation - 1u] = (uint8_t)t;
        }
    }
}

static const struct bc6h_mode {
    uint8_t code, subsets, transformed, endpoint_bits, delta_bits[3];
    const uint8_t *layout;
    uint8_t layout_bits;
} bc6h_modes[14] = {
    { 0, 2, 1, 10, { 5, 5, 5 }, layout0, sizeof layout0 },
    { 1, 2, 1, 7, { 6, 6, 6 }, layout1, sizeof layout1 },
    { 2, 2, 1, 11, { 5, 4, 4 }, layout2, sizeof layout2 },
    { 6, 2, 1, 11, { 4, 5, 4 }, layout6, sizeof layout6 },
    { 10, 2, 1, 11, { 4, 4, 5 }, layout10, sizeof layout10 },
    { 14, 2, 1, 9, { 5, 5, 5 }, layout14, sizeof layout14 },
    { 18, 2, 1, 8, { 6, 5, 5 }, layout18, sizeof layout18 },
    { 22, 2, 1, 8, { 5, 6, 5 }, layout22, sizeof layout22 },
    { 26, 2, 1, 8, { 5, 5, 6 }, layout26, sizeof layout26 },
    { 30, 2, 0, 6, { 6, 6, 6 }, layout30, sizeof layout30 },
    { 3, 1, 0, 10, { 10, 10, 10 }, layout3, sizeof layout3 },
    { 7, 1, 1, 11, { 9, 9, 9 }, layout7, sizeof layout7 },
    { 11, 1, 1, 12, { 8, 8, 8 }, layout11, sizeof layout11 },
    { 15, 1, 1, 16, { 4, 4, 4 }, layout15, sizeof layout15 }
};

static int32_t sign_extend(uint32_t v, unsigned bits)
{
    uint32_t sign = 1u << (bits - 1u);
    v &= (sign << 1) - 1u;
    return (int32_t)(v ^ sign) - (int32_t)sign;
}

static int32_t unquantize(int32_t x, unsigned bits, int is_signed)
{
    int negative = 0;

    if (!is_signed) {
        if (bits >= 15 || x == 0)
            return x;
        if (x == (int32_t)((1u << bits) - 1u))
            return 0xffff;
        return (int32_t)((((uint32_t)x << 15) + 0x4000u) >> (bits - 1u));
    }
    if (bits >= 16)
        return x;
    if (x < 0) {
        negative = 1;
        x = -x;
    }
    if (x != 0) {
        if (x >= (int32_t)((1u << (bits - 1u)) - 1u))
            x = 0x7fff;
        else
            x = (int32_t)((((uint32_t)x << 15) + 0x4000u) >> (bits - 1u));
    }
    return negative ? -x : x;
}

/* Floor of v / 64, for either sign. */
static int32_t floor64(int32_t v)
{
    return v >= 0 ? v / 64 : -((-v + 63) / 64);
}

void bc6h_block(const uint8_t *in, uint16_t *half, int is_signed)
{
    const struct bc6h_mode *m = NULL;
    struct bits b;
    uint32_t raw[12] = { 0 };
    int32_t ep[12];
    unsigned code, i, c, partition = 0, index_bits, endpoints;

    load_bits(&b, in);
    code = take(&b, 2);
    if (code >= 2)
        code |= take(&b, 3) << 2;
    for (i = 0; i < 14; i++)
        if (bc6h_modes[i].code == code)
            m = &bc6h_modes[i];
    if (m == NULL) {
        for (i = 0; i < 48; i++)
            half[i] = 0;
        return;
    }
    for (i = 0; i < m->layout_bits; i++)
        raw[m->layout[i] >> 4] |= (uint32_t)take(&b, 1) << (m->layout[i] & 15u);
    if (m->subsets == 2)
        partition = take(&b, 5);
    endpoints = 2u * m->subsets;
    for (c = 0; c < 3; c++) {
        uint32_t mask = (uint32_t)((1ul << m->endpoint_bits) - 1u);
        ep[c] = is_signed ? sign_extend(raw[c], m->endpoint_bits) : (int32_t)raw[c];
        for (i = 1; i < endpoints; i++) {
            unsigned k = i * 3u + c;
            if (m->transformed) {
                int32_t delta = sign_extend(raw[k], m->delta_bits[c]);
                uint32_t sum = ((uint32_t)raw[c] + (uint32_t)delta) & mask;
                ep[k] = is_signed ? sign_extend(sum, m->endpoint_bits) : (int32_t)sum;
            } else {
                ep[k] = is_signed ? sign_extend(raw[k], m->endpoint_bits) : (int32_t)raw[k];
            }
        }
    }
    for (i = 0; i < endpoints * 3u; i++)
        ep[i] = unquantize(ep[i], m->endpoint_bits, is_signed);
    index_bits = m->subsets == 2 ? 3u : 4u;
    for (i = 0; i < 16; i++) {
        unsigned s = subset_of(m->subsets, partition, i);
        unsigned w = weight(index_bits,
                            take(&b, index_bits - (unsigned)is_anchor(m->subsets, partition, i)));
        for (c = 0; c < 3; c++) {
            int32_t e0 = ep[s * 6u + c], e1 = ep[s * 6u + 3u + c];
            int32_t v = floor64(e0 * (int32_t)(64u - w) + e1 * (int32_t)w + 32);
            uint16_t h;
            if (!is_signed)
                h = (uint16_t)(((uint32_t)v * 31u) >> 6);
            else if (v < 0)
                h = (uint16_t)(0x8000u | (((uint32_t)-v * 31u) >> 5));
            else
                h = (uint16_t)(((uint32_t)v * 31u) >> 5);
            half[i * 3u + c] = h;
        }
    }
}
