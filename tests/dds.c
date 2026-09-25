#include "common/bcn.h"
#include "common/bptc.h"
#include "../formats/dds/decode.h"
#include "../formats/dds/encode.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define PF_ALPHAPIXELS 0x1u
#define PF_ALPHA 0x2u
#define PF_FOURCC 0x4u
#define PF_PALETTE8 0x20u
#define PF_RGB 0x40u
#define PF_LUMINANCE 0x20000u

static uint8_t data[1 << 20];

struct hdr {
    uint32_t width, height, depth, levels, flags, bits, mask[4], caps2;
    const char *fourcc;
    int dx10;
    uint32_t format, dimension, misc, array, misc2;
};

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

/* Write the header to data; returns the offset of the pixel data. */
static size_t header(struct hdr h)
{
    unsigned i;
    memset(data, 0, 148);
    memcpy(data, "DDS ", 4);
    put32(data + 4, 124);
    put32(data + 8, 0x1007);
    put32(data + 12, h.height);
    put32(data + 16, h.width);
    put32(data + 24, h.depth);
    put32(data + 28, h.levels);
    put32(data + 76, 32);
    if (h.dx10)
        h.fourcc = "DX10";
    if (h.fourcc) {
        h.flags |= PF_FOURCC;
        memcpy(data + 84, h.fourcc, 4);
    }
    put32(data + 80, h.flags);
    put32(data + 88, h.bits);
    for (i = 0; i < 4; i++)
        put32(data + 92 + i * 4, h.mask[i]);
    put32(data + 108, 0x1000);
    put32(data + 112, h.caps2);
    if (!h.dx10)
        return 128;
    put32(data + 128, h.format);
    put32(data + 132, h.dimension ? h.dimension : 3);
    put32(data + 136, h.misc);
    put32(data + 140, h.array);
    put32(data + 144, h.misc2);
    return 148;
}

static void pixel(const struct dds_image *im, unsigned x, unsigned y,
                  unsigned r, unsigned g, unsigned b, unsigned a)
{
    const uint8_t *p = im->rgba + (y * im->width + x) * 4u;
    if (p[0] != r || p[1] != g || p[2] != b || p[3] != a) {
        fprintf(stderr, "pixel %u,%u is %u %u %u %u, expected %u %u %u %u\n",
                x, y, p[0], p[1], p[2], p[3], r, g, b, a);
        assert(0);
    }
}

static enum codec_result decode(size_t length, unsigned long index, struct dds_image *im)
{
    return dds_decode(data, length, index, im);
}

static unsigned long count(size_t length)
{
    unsigned long n = 99;
    assert(dds_count(data, length, &n) == CODEC_OK);
    return n;
}

/* Little bit writer for building BC6H and BC7 blocks. */
struct writer { uint8_t *block; unsigned position; };

static void bits(struct writer *w, uint32_t value, unsigned n)
{
    unsigned i;
    for (i = 0; i < n; i++, w->position++)
        if ((value >> i) & 1u)
            w->block[w->position / 8u] |= (uint8_t)(1u << (w->position % 8u));
}

static void test_masks(void)
{
    struct dds_image im;
    size_t o;
    unsigned v;

    /* 24-bit BGR, the usual layout. */
    o = header((struct hdr){ .width = 2, .height = 1, .flags = PF_RGB, .bits = 24,
                             .mask = { 0xff0000, 0xff00, 0xff, 0 } });
    memcpy(data + o, "\x03\x02\x01\x06\x05\x04", 6);
    assert(count(o + 6) == 1);
    assert(decode(o + 6, 0, &im) == CODEC_OK);
    assert(im.width == 2 && im.height == 1);
    pixel(&im, 0, 0, 1, 2, 3, 255);
    pixel(&im, 1, 0, 4, 5, 6, 255);
    dds_free(&im);

    /* 32-bit ABGR order with straight alpha. */
    o = header((struct hdr){ .width = 1, .height = 1, .flags = PF_RGB | PF_ALPHAPIXELS,
                             .bits = 32, .mask = { 0xff, 0xff00, 0xff0000, 0xff000000 } });
    memcpy(data + o, "\x10\x20\x30\x40", 4);
    assert(decode(o + 4, 0, &im) == CODEC_OK);
    pixel(&im, 0, 0, 0x10, 0x20, 0x30, 0x40);
    dds_free(&im);

    /* Without DDPF_ALPHAPIXELS the alpha mask and the padding byte are ignored. */
    put32(data + 80, PF_RGB);
    assert(decode(o + 4, 0, &im) == CODEC_OK);
    pixel(&im, 0, 0, 0x10, 0x20, 0x30, 255);
    dds_free(&im);

    /* Declared alpha stays transparent even when every value is zero. */
    put32(data + 80, PF_RGB | PF_ALPHAPIXELS);
    data[o + 3] = 0;
    assert(decode(o + 4, 0, &im) == CODEC_OK);
    pixel(&im, 0, 0, 0x10, 0x20, 0x30, 0);
    dds_free(&im);

    /* 5:6:5 widens every value as floor(v * 255 / max). */
    o = header((struct hdr){ .width = 64, .height = 1, .flags = PF_RGB, .bits = 16,
                             .mask = { 0xf800, 0x7e0, 0x1f, 0 } });
    for (v = 0; v < 64; v++) {
        unsigned c = (v & 31u) << 11 | v << 5 | (31u - (v & 31u));
        data[o + v * 2] = (uint8_t)c;
        data[o + v * 2 + 1] = (uint8_t)(c >> 8);
    }
    assert(decode(o + 128, 0, &im) == CODEC_OK);
    for (v = 0; v < 64; v++)
        pixel(&im, v, 0, (v & 31u) * 255u / 31u, v * 255u / 63u,
              (31u - (v & 31u)) * 255u / 31u, 255);
    dds_free(&im);

    /* 1:5:5:5 and 4:4:4:4 alpha. */
    o = header((struct hdr){ .width = 2, .height = 1, .flags = PF_RGB | PF_ALPHAPIXELS,
                             .bits = 16, .mask = { 0x7c00, 0x3e0, 0x1f, 0x8000 } });
    memcpy(data + o, "\x1f\x80\x00\x7c", 4);
    assert(decode(o + 4, 0, &im) == CODEC_OK);
    pixel(&im, 0, 0, 0, 0, 255, 255);
    pixel(&im, 1, 0, 255, 0, 0, 0);
    dds_free(&im);
    o = header((struct hdr){ .width = 1, .height = 1, .flags = PF_RGB | PF_ALPHAPIXELS,
                             .bits = 16, .mask = { 0xf00, 0xf0, 0xf, 0xf000 } });
    memcpy(data + o, "\x21\x73", 2);
    assert(decode(o + 2, 0, &im) == CODEC_OK);
    pixel(&im, 0, 0, 0x33, 0x22, 0x11, 0x77);
    dds_free(&im);

    /* Masks with no bits give 0; alpha declared with no mask is opaque. */
    o = header((struct hdr){ .width = 1, .height = 1, .flags = PF_RGB | PF_ALPHAPIXELS,
                             .bits = 24 });
    memcpy(data + o, "\xff\xff\xff", 3);
    assert(decode(o + 3, 0, &im) == CODEC_OK);
    pixel(&im, 0, 0, 0, 0, 0, 255);
    dds_free(&im);

    /* DX10 R8G8B8A8, B8G8R8A8, B8G8R8X8 and R10G10B10A2. */
    o = header((struct hdr){ .width = 1, .height = 1, .dx10 = 1, .format = 28 });
    memcpy(data + o, "\x10\x20\x30\x40", 4);
    assert(decode(o + 4, 0, &im) == CODEC_OK);
    pixel(&im, 0, 0, 0x10, 0x20, 0x30, 0x40);
    dds_free(&im);
    put32(data + 128, 87);
    assert(decode(o + 4, 0, &im) == CODEC_OK);
    pixel(&im, 0, 0, 0x30, 0x20, 0x10, 0x40);
    dds_free(&im);
    put32(data + 128, 88);
    assert(decode(o + 4, 0, &im) == CODEC_OK);
    pixel(&im, 0, 0, 0x30, 0x20, 0x10, 255);
    dds_free(&im);
    put32(data + 128, 24);
    put32(data + o, 0x3ffu | 0x200u << 10 | 0u << 20 | 2u << 30);
    assert(decode(o + 4, 0, &im) == CODEC_OK);
    pixel(&im, 0, 0, 255, 0x200u * 255u / 1023u, 0, 170);
    dds_free(&im);

    /* DX10 alpha modes: premultiplied is divided out, opaque ignores alpha. */
    put32(data + 128, 28);
    memcpy(data + o, "\x40\x20\x00\x80", 4);
    put32(data + 144, 2);
    assert(decode(o + 4, 0, &im) == CODEC_OK);
    pixel(&im, 0, 0, 0x80, 0x40, 0, 0x80);
    dds_free(&im);
    put32(data + 144, 3);
    assert(decode(o + 4, 0, &im) == CODEC_OK);
    pixel(&im, 0, 0, 0x40, 0x20, 0, 255);
    dds_free(&im);
}

static void test_gray_and_palette(void)
{
    struct dds_image im;
    size_t o;
    unsigned i;

    /* Luminance, including the out-of-range mask some writers store. */
    o = header((struct hdr){ .width = 2, .height = 1, .flags = PF_LUMINANCE, .bits = 8,
                             .mask = { 0xff000000 } });
    memcpy(data + o, "\x00\x80", 2);
    assert(decode(o + 2, 0, &im) == CODEC_OK);
    pixel(&im, 0, 0, 0, 0, 0, 255);
    pixel(&im, 1, 0, 0x80, 0x80, 0x80, 255);
    dds_free(&im);

    /* Luminance with alpha, masks given or left out. */
    o = header((struct hdr){ .width = 1, .height = 1, .flags = PF_LUMINANCE | PF_ALPHAPIXELS,
                             .bits = 16, .mask = { 0xff, 0, 0, 0xff00 } });
    memcpy(data + o, "\x30\x90", 2);
    assert(decode(o + 2, 0, &im) == CODEC_OK);
    pixel(&im, 0, 0, 0x30, 0x30, 0x30, 0x90);
    dds_free(&im);
    put32(data + 92, 0);
    put32(data + 104, 0);
    assert(decode(o + 2, 0, &im) == CODEC_OK);
    pixel(&im, 0, 0, 0x30, 0x30, 0x30, 0x90);
    dds_free(&im);

    /* 4:4 luminance and alpha in a byte. */
    o = header((struct hdr){ .width = 1, .height = 1, .flags = PF_LUMINANCE | PF_ALPHAPIXELS,
                             .bits = 8, .mask = { 0xf, 0, 0, 0xf0 } });
    data[o] = 0x5a;
    assert(decode(o + 1, 0, &im) == CODEC_OK);
    pixel(&im, 0, 0, 0xaa, 0xaa, 0xaa, 0x55);
    dds_free(&im);

    /* Alpha only is white with the stored alpha. */
    o = header((struct hdr){ .width = 1, .height = 1, .flags = PF_ALPHA, .bits = 8,
                             .mask = { 0, 0, 0, 0xff } });
    data[o] = 0x42;
    assert(decode(o + 1, 0, &im) == CODEC_OK);
    pixel(&im, 0, 0, 255, 255, 255, 0x42);
    dds_free(&im);

    /* Palette: the bit count is ignored, and the fourth byte is alpha only
       with DDPF_ALPHAPIXELS. */
    o = header((struct hdr){ .width = 2, .height = 1, .flags = PF_PALETTE8, .bits = 24 });
    for (i = 0; i < 256; i++) {
        data[o + i * 4] = (uint8_t)i;
        data[o + i * 4 + 1] = (uint8_t)(255 - i);
        data[o + i * 4 + 2] = 7;
        data[o + i * 4 + 3] = (uint8_t)(i / 2);
    }
    data[o + 1024] = 3;
    data[o + 1025] = 200;
    assert(count(o + 1026) == 1);
    assert(decode(o + 1026, 0, &im) == CODEC_OK);
    pixel(&im, 0, 0, 3, 252, 7, 255);
    pixel(&im, 1, 0, 200, 55, 7, 255);
    dds_free(&im);
    put32(data + 80, PF_PALETTE8 | PF_ALPHAPIXELS);
    assert(decode(o + 1026, 0, &im) == CODEC_OK);
    pixel(&im, 0, 0, 3, 252, 7, 1);
    pixel(&im, 1, 0, 200, 55, 7, 100);
    dds_free(&im);
    assert(decode(o + 1025, 0, &im) == CODEC_TRUNCATED);
    assert(decode(o + 1023, 0, &im) == CODEC_TRUNCATED);
}

static void test_bc1_to_bc5(void)
{
    struct dds_image im;
    uint8_t out[64];
    size_t o;

    /* Four colours: white, black and two thirds between, rounded down. */
    bc1_block((const uint8_t *)"\xff\xff\x00\x00\xe4\xe4\xe4\xe4", out, 1);
    assert(out[0] == 255 && out[3] == 255);
    assert(out[4] == 0 && out[5] == 0);
    assert(out[8] == 170 && out[12] == 85 && out[15] == 255);

    /* Three colours: the midpoint, then index 3 is transparent black. */
    bc1_block((const uint8_t *)"\x00\x00\xff\xff\xe4\xe4\xe4\xe4", out, 1);
    assert(out[0] == 0 && out[4] == 255 && out[8] == 127);
    assert(out[12] == 0 && out[15] == 0);
    bc1_block((const uint8_t *)"\x00\x00\xff\xff\xe4\xe4\xe4\xe4", out, 0);
    assert(out[12] == 0 && out[15] == 255);

    /* BC2 and BC3 always use four colours. */
    bc2_block((const uint8_t *)"\x10\x32\x54\x76\x98\xba\xdc\xfe"
              "\x00\x00\xff\xff\xe4\xe4\xe4\xe4", out);
    assert(out[8] == 85 && out[12] == 170);
    assert(out[3] == 0 && out[7] == 17 && out[63] == 255);

    /* BC3's alpha ramp, eight values and then six plus 0 and 255. */
    bc3_block((const uint8_t *)"\xff\x00\x88\xc6\xfa\x88\xc6\xfa"
              "\xff\xff\x00\x00\x00\x00\x00\x00", out);
    assert(out[3] == 255 && out[7] == 0 && out[11] == 218 && out[15] == 182);
    assert(out[19] == 145 && out[23] == 109 && out[27] == 72 && out[31] == 36);
    bc3_block((const uint8_t *)"\x00\xff\x88\xc6\xfa\x88\xc6\xfa"
              "\xff\xff\x00\x00\x00\x00\x00\x00", out);
    assert(out[11] == 51 && out[23] == 204 && out[27] == 0 && out[31] == 255);

    /* BC4 is gray; BC5 is red and green, signed ones shifted by 128. */
    bc4_block((const uint8_t *)"\x80\x80\x00\x00\x00\x00\x00\x00", out);
    assert(out[0] == 128 && out[1] == 128 && out[2] == 128 && out[3] == 255);
    bc5_block((const uint8_t *)"\x10\x10\x00\x00\x00\x00\x00\x00"
              "\x20\x20\x00\x00\x00\x00\x00\x00", out, 0);
    assert(out[0] == 0x10 && out[1] == 0x20 && out[2] == 0 && out[3] == 255);
    bc5_block((const uint8_t *)"\x81\x81\x00\x00\x00\x00\x00\x00"
              "\x7f\x7f\x00\x00\x00\x00\x00\x00", out, 1);
    assert(out[0] == 1 && out[1] == 255 && out[2] == 128);

    /* A 5x3 DXT1 image uses two blocks and keeps only the visible pixels. */
    o = header((struct hdr){ .width = 5, .height = 3, .fourcc = "DXT1" });
    memcpy(data + o, "\xff\xff\x00\x00\x00\x00\x00\x00"
           "\x00\x00\xff\xff\xff\xff\xff\xff", 16);
    assert(decode(o + 16, 0, &im) == CODEC_OK);
    assert(im.width == 5 && im.height == 3);
    pixel(&im, 3, 2, 255, 255, 255, 255);
    pixel(&im, 4, 0, 0, 0, 0, 0);
    dds_free(&im);
    assert(decode(o + 15, 0, &im) == CODEC_TRUNCATED);

    /* A DXT1 image that is transparent everywhere stays transparent. */
    memcpy(data + o, "\x00\x00\xff\xff\xff\xff\xff\xff", 8);
    assert(decode(o + 16, 0, &im) == CODEC_OK);
    pixel(&im, 0, 0, 0, 0, 0, 0);
    dds_free(&im);

    /* DX10 BC1 whose alpha mode is opaque ignores the punch-through. */
    o = header((struct hdr){ .width = 5, .height = 3, .dx10 = 1, .format = 71,
                             .misc2 = 3 });
    memcpy(data + o, "\xff\xff\x00\x00\x00\x00\x00\x00"
           "\x00\x00\xff\xff\xff\xff\xff\xff", 16);
    assert(decode(o + 16, 0, &im) == CODEC_OK);
    pixel(&im, 4, 0, 0, 0, 0, 255);
    dds_free(&im);

    /* Each fourcc and DXGI code picks its block size. */
    o = header((struct hdr){ .width = 4, .height = 4, .fourcc = "DXT5" });
    assert(decode(o + 15, 0, &im) == CODEC_TRUNCATED);
    assert(decode(o + 16, 0, &im) == CODEC_OK);
    dds_free(&im);
    o = header((struct hdr){ .width = 4, .height = 4, .fourcc = "ATI1" });
    assert(decode(o + 8, 0, &im) == CODEC_OK);
    dds_free(&im);
    o = header((struct hdr){ .width = 4, .height = 4, .fourcc = "BC5S" });
    assert(decode(o + 15, 0, &im) == CODEC_TRUNCATED);
    o = header((struct hdr){ .width = 4, .height = 4, .dx10 = 1, .format = 80 });
    assert(decode(o + 8, 0, &im) == CODEC_OK);
    dds_free(&im);
    o = header((struct hdr){ .width = 4, .height = 4, .dx10 = 1, .format = 99 });
    assert(decode(o + 15, 0, &im) == CODEC_TRUNCATED);
}

static void test_bc7(void)
{
    uint8_t block[16], out[64];
    struct writer w;
    unsigned i, expect;
    static const unsigned weights4[16] = {
        0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64
    };

    /* Mode 6: endpoints 0 and 127 with P-bits 0 and 1 span 0 to 255, and
       pixel i has index i. */
    memset(block, 0, 16);
    w.block = block;
    w.position = 0;
    bits(&w, 1u << 6, 7);
    for (i = 0; i < 4; i++) {
        bits(&w, 0, 7);
        bits(&w, 127, 7);
    }
    bits(&w, 0, 1);
    bits(&w, 1, 1);
    bits(&w, 0, 3);
    for (i = 1; i < 16; i++)
        bits(&w, i, 4);
    assert(w.position == 128);
    bc7_block(block, out);
    for (i = 0; i < 16; i++) {
        expect = (weights4[i] * 255u + 32u) >> 6;
        assert(out[i * 4] == expect && out[i * 4 + 1] == expect);
        assert(out[i * 4 + 2] == expect && out[i * 4 + 3] == expect);
    }

    /* Mode 5 with rotation 1 swaps red and alpha. */
    memset(block, 0, 16);
    w.position = 0;
    bits(&w, 1u << 5, 6);
    bits(&w, 1, 2);
    for (i = 0; i < 3; i++) {
        bits(&w, 0, 7);
        bits(&w, 127, 7);
    }
    bits(&w, 200, 8);
    bits(&w, 200, 8);
    bits(&w, 0, 1);
    for (i = 1; i < 16; i++)
        bits(&w, 3, 2);
    bits(&w, 0, 1);
    for (i = 1; i < 16; i++)
        bits(&w, 0, 2);
    assert(w.position == 128);
    bc7_block(block, out);
    assert(out[0] == 200 && out[1] == 0 && out[2] == 0 && out[3] == 0);
    assert(out[4] == 200 && out[5] == 255 && out[6] == 255 && out[7] == 255);

    /* Mode 1, partition 0: the two right columns are subset 1, whose anchor
       is pixel 15. */
    memset(block, 0, 16);
    w.position = 0;
    bits(&w, 2, 2);
    bits(&w, 0, 6);
    for (i = 0; i < 3; i++) {
        bits(&w, 10, 6);
        bits(&w, 10, 6);
        bits(&w, 50, 6);
        bits(&w, 50, 6);
    }
    bits(&w, 0, 2);
    assert(w.position == 82);
    bc7_block(block, out);
    for (i = 0; i < 16; i++) {
        expect = (i % 4u) < 2 ? 10u << 2 | 10u >> 5 : 50u << 2 | 50u >> 5;
        assert(out[i * 4] == expect && out[i * 4 + 3] == 255);
    }

    /* A block with no mode bit set is opaque black. */
    memset(block, 0, 16);
    bc7_block(block, out);
    assert(out[0] == 0 && out[2] == 0 && out[3] == 255 && out[63] == 255);
}

/* One-subset BC6H block, mode 3 (10-bit endpoints) or mode 7 (11-bit base,
   9-bit deltas), with every index 15 so each pixel is endpoint 1. */
static void bc6h_mode3(uint8_t *block, const unsigned e0[3], const unsigned e1[3])
{
    struct writer w = { block, 0 };
    unsigned i;
    memset(block, 0, 16);
    bits(&w, 3, 5);
    for (i = 0; i < 3; i++)
        bits(&w, e0[i], 10);
    for (i = 0; i < 3; i++)
        bits(&w, e1[i], 10);
    bits(&w, 7, 3);
    for (i = 1; i < 16; i++)
        bits(&w, 15, 4);
}

static void test_bc6h(void)
{
    uint8_t block[16];
    uint16_t half[48];
    struct dds_image im;
    struct writer w;
    size_t o;
    unsigned i;
    static const unsigned zero[3] = { 0, 0, 0 };
    static const unsigned e[3] = { 100, 512, 1023 };
    static const unsigned negative[3] = { 0x200, 100, 0x3ff };

    /* Unsigned: 100 is dark, 512 is above 1.0 and 1023 is the largest half. */
    bc6h_mode3(block, zero, e);
    bc6h_block(block, half, 0);
    assert(half[3] == 0x0c2b && half[4] == 0x3e0f && half[5] == 0x7bff);
    assert(half[45] == 0x0c2b);

    o = header((struct hdr){ .width = 4, .height = 4, .dx10 = 1, .format = 95 });
    memcpy(data + o, block, 16);
    assert(decode(o + 16, 0, &im) == CODEC_OK);
    pixel(&im, 3, 3, 1, 255, 255, 255);
    dds_free(&im);

    /* Signed: -512 and -1 clamp to 0; 100 encodes as sRGB 7. */
    bc6h_mode3(block, zero, negative);
    bc6h_block(block, half, 1);
    assert(half[3] == (0x8000 | 0x7bff) && half[4] == 0x1857 && half[5] == 0x805d);
    put32(data + 128, 96);
    memcpy(data + o, block, 16);
    assert(decode(o + 16, 0, &im) == CODEC_OK);
    pixel(&im, 1, 0, 0, 7, 0, 255);
    dds_free(&im);

    /* Mode 7's deltas: 0 + (-1) wraps and is sign-extended to -1 when signed,
       but is 0x7ff (almost the largest value) when unsigned. */
    memset(block, 0, 16);
    w.block = block;
    w.position = 0;
    bits(&w, 7, 5);
    bits(&w, 0, 30);
    for (i = 0; i < 3; i++) {
        bits(&w, 0x1ff, 9);
        bits(&w, 0, 1);
    }
    bits(&w, 7, 3);
    for (i = 1; i < 16; i++)
        bits(&w, 15, 4);
    assert(w.position == 128);
    bc6h_block(block, half, 1);
    assert(half[3] == 0x802e);
    bc6h_block(block, half, 0);
    assert(half[3] == 0x7bff);

    /* Reserved modes decode to black. */
    memset(block, 0, 16);
    block[0] = 19;
    bc6h_block(block, half, 0);
    for (i = 0; i < 48; i++)
        assert(half[i] == 0);
}

static void test_images(void)
{
    struct dds_image im;
    size_t o, i;

    /* 8-bit gray mip chain for 4x2: levels 4x2, 2x1 and 1x1. */
    o = header((struct hdr){ .width = 4, .height = 2, .levels = 3, .flags = PF_LUMINANCE,
                             .bits = 8, .mask = { 0xff } });
    for (i = 0; i < 11; i++)
        data[o + i] = (uint8_t)(i * 10);
    assert(count(o + 11) == 3);
    assert(decode(o + 11, 1, &im) == CODEC_OK);
    assert(im.width == 2 && im.height == 1);
    pixel(&im, 1, 0, 90, 90, 90, 255);
    dds_free(&im);
    assert(decode(o + 11, 2, &im) == CODEC_OK);
    assert(im.width == 1 && im.height == 1);
    pixel(&im, 0, 0, 100, 100, 100, 255);
    dds_free(&im);
    assert(decode(o + 11, 3, &im) == CODEC_INVALID);
    assert(decode(o + 11, 0xffffffffUL, &im) == CODEC_INVALID);

    /* Missing trailing levels are left out of the count; asking for one is a
       truncation. */
    assert(count(o + 10) == 2);
    assert(count(o + 9) == 1);
    assert(count(o + 7) == 0);
    assert(decode(o + 10, 2, &im) == CODEC_TRUNCATED);
    assert(decode(o + 9, 1, &im) == CODEC_TRUNCATED);
    assert(decode(o + 9, 0, &im) == CODEC_OK);
    dds_free(&im);

    /* More levels than the size allows. */
    put32(data + 28, 4);
    assert(decode(o + 11, 0, &im) == CODEC_INVALID);
    put32(data + 28, 0);
    assert(count(o + 11) == 1);

    /* A legacy cube map with three faces of two levels: face-major order. */
    o = header((struct hdr){ .width = 2, .height = 2, .levels = 2, .flags = PF_LUMINANCE,
                             .bits = 8, .mask = { 0xff }, .caps2 = 0x200 | 0x400 | 0x1000 | 0x4000 });
    for (i = 0; i < 15; i++)
        data[o + i] = (uint8_t)(i * 10);
    assert(count(o + 15) == 6);
    assert(decode(o + 15, 3, &im) == CODEC_OK);
    assert(im.width == 1);
    pixel(&im, 0, 0, 90, 90, 90, 255);
    dds_free(&im);
    assert(decode(o + 15, 4, &im) == CODEC_OK);
    assert(im.width == 2);
    pixel(&im, 0, 0, 100, 100, 100, 255);
    dds_free(&im);
    assert(count(o + 14) == 5);

    /* A cube map that lists no faces has six. */
    put32(data + 112, 0x200);
    assert(count(o + 30) == 12);

    /* DX10 array of two cubes: twelve faces of one level. */
    o = header((struct hdr){ .width = 1, .height = 1, .dx10 = 1, .format = 28,
                             .misc = 4, .array = 2 });
    for (i = 0; i < 48; i++)
        data[o + i] = (uint8_t)i;
    assert(count(o + 48) == 12);
    assert(decode(o + 48, 11, &im) == CODEC_OK);
    pixel(&im, 0, 0, 44, 45, 46, 47);
    dds_free(&im);
    put32(data + 140, 0xffffffffu);
    assert(decode(o + 48, 0, &im) == CODEC_INVALID);
    put32(data + 140, 0);
    put32(data + 136, 0);
    assert(count(o + 48) == 1);

    /* A volume of depth 3 with two levels: slices 0-2, then one 1x1 slice. */
    o = header((struct hdr){ .width = 2, .height = 1, .depth = 3, .levels = 2,
                             .flags = PF_LUMINANCE, .bits = 8, .mask = { 0xff },
                             .caps2 = 0x200000 });
    for (i = 0; i < 7; i++)
        data[o + i] = (uint8_t)(i * 10);
    assert(count(o + 7) == 4);
    assert(decode(o + 7, 2, &im) == CODEC_OK);
    assert(im.width == 2);
    pixel(&im, 1, 0, 50, 50, 50, 255);
    dds_free(&im);
    assert(decode(o + 7, 3, &im) == CODEC_OK);
    assert(im.width == 1);
    pixel(&im, 0, 0, 60, 60, 60, 255);
    dds_free(&im);

    /* DX10 volumes can't be arrays. */
    o = header((struct hdr){ .width = 1, .height = 1, .depth = 2, .dx10 = 1, .format = 28,
                             .dimension = 4, .array = 2 });
    assert(decode(o + 8, 0, &im) == CODEC_INVALID);
    put32(data + 140, 1);
    assert(count(o + 8) == 2);
}

static void test_malformed(void)
{
    struct dds_image im;
    unsigned long n;
    size_t o, i;

    o = header((struct hdr){ .width = 1, .height = 1, .flags = PF_RGB, .bits = 32,
                             .mask = { 0xff0000, 0xff00, 0xff, 0 } });
    for (i = 0; i < o + 4; i++) {
        enum codec_result r = decode(i, 0, &im);
        assert(r == CODEC_TRUNCATED);
        assert(im.rgba == NULL);
        assert(dds_count(data, i, &n) == (i < o ? CODEC_TRUNCATED : CODEC_OK));
    }
    data[0] = 'X';
    assert(decode(o + 4, 0, &im) == CODEC_INVALID);
    assert(decode(1, 0, &im) == CODEC_INVALID);
    data[0] = 'D';
    put32(data + 4, 128);
    assert(decode(o + 4, 0, &im) == CODEC_INVALID);
    put32(data + 4, 124);

    /* Sizes. */
    put32(data + 16, 0);
    assert(decode(o + 4, 0, &im) == CODEC_INVALID);
    put32(data + 16, 65536);
    assert(decode(o + 4, 0, &im) == CODEC_TOO_LARGE);
    put32(data + 16, 1);
    put32(data + 12, 0xffffffffu);
    assert(decode(o + 4, 0, &im) == CODEC_TOO_LARGE);
    put32(data + 12, 1);
    o = header((struct hdr){ .width = 8192, .height = 2049, .levels = 2, .fourcc = "DXT1" });
    assert(decode(o, 0, &im) == CODEC_TOO_LARGE);
    assert(decode(o, 1, &im) == CODEC_TRUNCATED);

    /* Pixel formats. */
    o = header((struct hdr){ .width = 1, .height = 1, .flags = PF_RGB, .bits = 12 });
    assert(decode(o + 4, 0, &im) == CODEC_INVALID);
    put32(data + 88, 0);
    assert(decode(o + 4, 0, &im) == CODEC_INVALID);
    put32(data + 80, 0);
    put32(data + 88, 32);
    assert(decode(o + 4, 0, &im) == CODEC_INVALID);
    put32(data + 80, 0x200);
    assert(decode(o + 4, 0, &im) == CODEC_INVALID);
    o = header((struct hdr){ .width = 4, .height = 4, .fourcc = "DXT2" });
    assert(decode(o + 16, 0, &im) == CODEC_INVALID);
    memcpy(data + 84, "\x24\x00\x00\x00", 4);
    assert(decode(o + 16, 0, &im) == CODEC_INVALID);
    o = header((struct hdr){ .width = 4, .height = 4, .dx10 = 1, .format = 72 });
    assert(decode(o + 16, 0, &im) == CODEC_INVALID);
    assert(decode(140, 0, &im) == CODEC_TRUNCATED);
    /* "DX10" without DDPF_FOURCC is not a DX10 header. */
    put32(data + 80, 0);
    assert(decode(o + 16, 0, &im) == CODEC_INVALID);
}

static void test_encode(void)
{
    uint8_t rgba[8] = { 1, 2, 3, 255, 250, 251, 252, 255 };
    struct dds_image im;
    uint8_t *p = data;

    assert(!dds_make_header(0, 1, 0, data));
    assert(!dds_make_header(65536, 1, 0, data));
    assert(!dds_row_has_alpha(rgba, 2));
    assert(dds_make_header(2, 1, 0, data));
    dds_encode_row(rgba, 2, 0, data + DDS_HEADER_SIZE);
    assert(p[DDS_HEADER_SIZE] == 3 && p[DDS_HEADER_SIZE + 2] == 1);
    assert(decode(DDS_HEADER_SIZE + 6, 0, &im) == CODEC_OK);
    assert(memcmp(im.rgba, rgba, 8) == 0);
    dds_free(&im);

    rgba[7] = 0;
    assert(dds_row_has_alpha(rgba, 2));
    assert(dds_make_header(2, 1, 1, data));
    dds_encode_row(rgba, 2, 1, data + DDS_HEADER_SIZE);
    assert(decode(DDS_HEADER_SIZE + 8, 0, &im) == CODEC_OK);
    assert(memcmp(im.rgba, rgba, 8) == 0);
    dds_free(&im);
}

int main(void)
{
    test_masks();
    test_gray_and_palette();
    test_bc1_to_bc5();
    test_bc7();
    test_bc6h();
    test_images();
    test_malformed();
    test_encode();
    puts("dds: ok");
    return 0;
}
