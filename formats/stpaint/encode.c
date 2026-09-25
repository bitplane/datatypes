#include "encode.h"
#include "common/atarist.h"
#include <stdlib.h>
#include <string.h>

#define WORDS 16000u

static void put_be16(uint8_t *p, unsigned v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static uint8_t over_white(uint8_t c, uint8_t a)
{
    return (uint8_t)((c * a + 255u * (255u - a) + 127u) / 255u);
}

/* The 12-bit palette entry for rgb, or -1 if it can't be stored. */
static int entry(const uint8_t *rgb, int ste)
{
    int r = st_nibble(rgb[0], ste), g = st_nibble(rgb[1], ste), b = st_nibble(rgb[2], ste);

    if (r < 0 || g < 0 || b < 0)
        return -1;
    return (r << 8) | (g << 4) | b;
}

/* Builds the ST screen and palette words; returns the colour count or -1. */
static int screen(const uint8_t *rgba, unsigned mode, uint8_t *bits, int words[16])
{
    unsigned planes = 4u >> mode, width = mode ? 640u : 320u;
    unsigned height = mode == 2 ? 400u : 200u, stride = width * planes / 8u;
    unsigned limit = 1u << planes, count = 0, x, y, i;
    uint8_t colour[16][3];
    int ste, marked = 0;

    if (mode == 2) {
        /* High resolution has no palette: 0 is white and 1 is black. */
        memset(colour[0], 255, 3);
        memset(colour[1], 0, 3);
        count = 2;
    }
    memset(bits, 0, WORDS * 2u);
    for (y = 0; y < height; y++)
        for (x = 0; x < width; x++) {
            const uint8_t *p = rgba + ((size_t)y * width + x) * 4u;
            uint8_t rgb[3], *group = bits + y * stride + (x / 16u) * planes * 2u;
            unsigned bit = 15u - x % 16u, plane;
            rgb[0] = over_white(p[0], p[3]);
            rgb[1] = over_white(p[1], p[3]);
            rgb[2] = over_white(p[2], p[3]);
            for (i = 0; i < count && memcmp(colour[i], rgb, 3) != 0; i++)
                ;
            if (i == count) {
                if (count == limit)
                    return -1;
                memcpy(colour[count++], rgb, 3);
            }
            for (plane = 0; plane < planes; plane++)
                if (i >> plane & 1u)
                    group[plane * 2u + (bit < 8u)] |= (uint8_t)(1u << bit % 8u);
        }

    memset(words, 0, 16 * sizeof words[0]);
    if (mode == 2) {
        words[0] = 0x777;
        return (int)count;
    }
    /* ST levels where they cover the image, STE levels otherwise. */
    for (ste = 0; ste < 2; ste++) {
        for (i = 0; i < count && (words[i] = entry(colour[i], ste)) >= 0; i++)
            marked |= words[i] & 0x888;
        if (i == count)
            break;
        marked = 0;
    }
    if (ste == 2)
        return -1;
    /* An STE palette is recognised by a fourth bit in a colour the mode uses. */
    if (ste && !marked) {
        if (count == limit)
            return -1;
        words[count++] = 0x888;
    }
    return (int)count;
}

/* Word n of Tiny's order: four sets of 20 word columns, each 200 lines high. */
static unsigned word_at(const uint8_t *bits, unsigned n)
{
    unsigned column = n / 4000u + (n % 4000u) / 200u * 4u;
    const uint8_t *p = bits + (n % 200u) * 160u + column * 2u;

    return ((unsigned)p[0] << 8) | p[1];
}

enum codec_result tiny_encode(const uint8_t *rgba, unsigned width, unsigned height,
                              uint8_t output[TINY_MAX_SIZE], size_t *size)
{
    uint8_t *bits, *controls;
    int palette[16];
    unsigned mode, n = 0, i, ncontrols = 0, nwords = 0;
    uint8_t *data;

    if (rgba == NULL || output == NULL || size == NULL)
        return CODEC_INVALID;
    if (width == 320 && height == 200)
        mode = 0;
    else if (width == 640 && height == 200)
        mode = 1;
    else if (width == 640 && height == 400)
        mode = 2;
    else
        return CODEC_INVALID;
    /* AROS tasks have small stacks, so the screen and controls go on the heap. */
    bits = malloc(WORDS * 3u);
    if (bits == NULL)
        return CODEC_NO_MEMORY;
    controls = bits + WORDS * 2u;
    if (screen(rgba, mode, bits, palette) < 0) {
        free(bits);
        return CODEC_INVALID;
    }

    /* Data words go straight to their place after the largest control stream;
       they move down once its real length is known. */
    data = output + 37u + WORDS;
    while (n < WORDS) {
        unsigned value = word_at(bits, n), run = 1, literals;
        while (n + run < WORDS && run < 32767u && word_at(bits, n + run) == value)
            run++;
        if (run >= 2) {
            /* 2-127 repeats fit the control byte; longer ones take a word. */
            if (run < 128) {
                controls[ncontrols++] = (uint8_t)run;
            } else {
                controls[ncontrols++] = 0;
                controls[ncontrols++] = (uint8_t)(run >> 8);
                controls[ncontrols++] = (uint8_t)run;
            }
            put_be16(data + nwords++ * 2u, value);
            n += run;
            continue;
        }
        /* Literals run until the next pair of equal words. */
        literals = 1;
        while (n + literals < WORDS &&
               (n + literals + 1u >= WORDS ||
                word_at(bits, n + literals) != word_at(bits, n + literals + 1u)))
            literals++;
        if (literals <= 128) {
            controls[ncontrols++] = (uint8_t)(256u - literals);
        } else {
            controls[ncontrols++] = 1;
            controls[ncontrols++] = (uint8_t)(literals >> 8);
            controls[ncontrols++] = (uint8_t)literals;
        }
        for (i = 0; i < literals; i++)
            put_be16(data + nwords++ * 2u, word_at(bits, n + i));
        n += literals;
    }

    output[0] = (uint8_t)mode;
    for (i = 0; i < 16; i++)
        put_be16(output + 1 + i * 2u, (unsigned)palette[i]);
    put_be16(output + 33, ncontrols);
    put_be16(output + 35, nwords);
    memcpy(output + 37, controls, ncontrols);
    memmove(output + 37 + ncontrols, data, nwords * 2u);
    *size = 37u + ncontrols + nwords * 2u;
    free(bits);
    return CODEC_OK;
}
