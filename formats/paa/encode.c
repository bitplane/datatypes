#include "encode.h"
#include "lzss.h"
#include <stdlib.h>
#include <string.h>

#define MAX_PIXELS (16ul * 1024ul * 1024ul)
/* Type, up to four taggs (three of 4 bytes, offsets of 64), empty palette. */
#define HEADER_SIZE (2u + 3u * 16u + 12u + 64u + 2u)
#define FLAG_TAGG 16u
#define MIP_HEADER 7u
#define MAX_PACKED 0xFFFFFFul

static uint8_t *put16(uint8_t *p, unsigned v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    return p + 2;
}

static uint8_t *put32(uint8_t *p, uint32_t v)
{
    return put16(put16(p, v & 0xFFFFu), v >> 16);
}

static uint8_t *tagg(uint8_t *p, const char *name, uint32_t length)
{
    memcpy(p, "GGAT", 4);
    memcpy(p + 4, name, 4);
    return put32(p + 8, length);
}

enum codec_result paa_encode(const uint8_t *rgba, unsigned width, unsigned height,
                             uint8_t **out, size_t *out_len)
{
    size_t pixels = (size_t)width * height, i, packed;
    uint64_t sum[4] = { 0, 0, 0, 0 };
    uint32_t average = 0;
    uint8_t *argb, *file, *p;
    int alpha = 0;
    unsigned c, header;

    *out = NULL;
    if (width == 0 || height == 0 || width > 65535u || height > 65535u)
        return CODEC_INVALID;
    if (pixels > MAX_PIXELS)
        return CODEC_TOO_LARGE;
    /* A8R8G8B8 is stored B, G, R, A. */
    if ((argb = malloc(pixels * 4)) == NULL)
        return CODEC_NO_MEMORY;
    for (i = 0; i < pixels; i++) {
        const uint8_t *s = rgba + i * 4;
        argb[i * 4] = s[2];
        argb[i * 4 + 1] = s[1];
        argb[i * 4 + 2] = s[0];
        argb[i * 4 + 3] = s[3];
        for (c = 0; c < 4; c++)
            sum[c] += s[c];
        alpha |= s[3] != 255;
    }
    /* The average is an A8R8G8B8 word, like a pixel. */
    for (c = 0; c < 4; c++)
        average |= (uint32_t)(sum[c] / pixels) << (c == 3 ? 24 : 16 - 8 * c);
    file = malloc(HEADER_SIZE + MIP_HEADER + paa_lzss_bound(pixels * 4) + 6);
    if (file == NULL) {
        free(argb);
        return CODEC_NO_MEMORY;
    }
    header = alpha ? HEADER_SIZE : HEADER_SIZE - FLAG_TAGG;
    p = put16(file, 0x8888);
    p = put32(tagg(p, "CGVA", 4), average);
    p = put32(tagg(p, "CXAM", 4), 0xFFFFFFFFu);
    /* BI's tools flag textures with transparency and leave the flag out
       otherwise. */
    if (alpha)
        p = put32(tagg(p, "GALF", 4), 1);
    /* The offset of each level, here only the first. */
    p = tagg(p, "SFFO", 64);
    p = put32(p, header);
    memset(p, 0, 60);
    p = put16(p + 60, 0);
    p = put16(put16(p, width), height);
    packed = paa_lzss_pack(argb, pixels * 4, p + 3);
    free(argb);
    if (packed > MAX_PACKED) {
        free(file);
        return CODEC_TOO_LARGE;
    }
    p[0] = (uint8_t)packed;
    p[1] = (uint8_t)(packed >> 8);
    p[2] = (uint8_t)(packed >> 16);
    p += 3 + packed;
    /* An empty level ends the list, then the closing zero word. */
    p = put16(put16(put16(p, 0), 0), 0);
    *out = file;
    *out_len = (size_t)(p - file);
    return CODEC_OK;
}
