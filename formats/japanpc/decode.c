#include "decode.h"

#include <stdlib.h>
#include <string.h>

#include "japanpc.h"

static unsigned widen(unsigned value, unsigned bits)
{
    unsigned top = value & (0xffu << (8 - bits)) & 0xffu;
    unsigned out = top;
    unsigned shift;
    for (shift = bits; shift < 8; shift += bits)
        out |= top >> shift;
    return out;
}

uint32_t jp_dac_colour(enum jp_dac dac, unsigned r, unsigned g, unsigned b)
{
    switch (dac) {
    case JP_DAC_4:
        r = widen(r, 4), g = widen(g, 4), b = widen(b, 4);
        break;
    case JP_DAC_3:
        r = widen(r, 3), g = widen(g, 3), b = widen(b, 3);
        break;
    case JP_DAC_565:
        r = widen(r, 5), g = widen(g, 6), b = widen(b, 5);
        break;
    case JP_DAC_TOWNS:
        r = widen(r, 5), g = widen(g, 5), b = widen(b, 5);
        break;
    case JP_DAC_X68K: {
        /* The intensity bit is bit 2 of green, and lights all three. */
        unsigned i = g & 4u;
        r = (r & 0xf8u) | i | r >> 6;
        g = (g & 0xf8u) | i | g >> 6;
        b = (b & 0xf8u) | i | b >> 6;
        break;
    }
    case JP_DAC_8:
        break;
    }
    return (uint32_t)r << 16 | (uint32_t)g << 8 | b;
}

uint32_t jp_x68k_colour(unsigned c)
{
    unsigned i = (c & 1u) << 2;
    unsigned r = (c >> 6 & 31u) << 3, g = (c >> 11 & 31u) << 3,
             b = (c >> 1 & 31u) << 3;
    r |= i | r >> 6;
    g |= i | g >> 6;
    b |= i | b >> 6;
    return (uint32_t)r << 16 | (uint32_t)g << 8 | b;
}

uint32_t jp_g6r5b5_colour(unsigned c)
{
    unsigned r = (c >> 5 & 31u) << 3, g = (c >> 10 & 63u) << 2,
             b = (c & 31u) << 3;
    r |= r >> 5;
    g |= g >> 6;
    b |= b >> 5;
    return (uint32_t)r << 16 | (uint32_t)g << 8 | b;
}

enum jp_dac jp_machine_dac(const uint8_t *code, int tall, unsigned colours,
                           unsigned *repeat_y)
{
    *repeat_y = tall ? 2 : 1;
    if (memcmp(code, "TOWN", 4) == 0)
        return JP_DAC_TOWNS;
    if (memcmp(code, "X68K", 4) == 0)
        return JP_DAC_X68K;
    /* The PC-8001 and PC-8801 screens are 200 lines. */
    if (memcmp(code, "PC80", 4) == 0 || memcmp(code, "PC88", 4) == 0) {
        *repeat_y = 2;
        return code[3] == '0' ? JP_DAC_8 : JP_DAC_4;
    }
    if (memcmp(code, "PCVA", 4) == 0)
        return tall ? JP_DAC_4 : JP_DAC_565;
    if (memcmp(code, "MSX", 3) == 0 &&
        (code[3] == '1' || code[3] == '2' || code[3] == 'P' || code[3] == 'R'))
        return JP_DAC_3;
    /* PC-98 and anything unnamed. */
    return tall || colours == 16 ? JP_DAC_4 : JP_DAC_8;
}

void jp_palette(uint32_t *palette, const uint8_t *entries, unsigned count,
                unsigned red, unsigned green, enum jp_dac dac)
{
    unsigned i;
    for (i = 0; i < count; i++, entries += 3)
        palette[i] = jp_dac_colour(dac, entries[red], entries[green],
                                   entries[2]);
}

enum codec_result jp_canvas_init(struct jp_canvas *canvas, unsigned width,
                                 unsigned height, unsigned repeat_x,
                                 unsigned repeat_y)
{
    unsigned long wide = (unsigned long)width * repeat_x;
    unsigned long tall = (unsigned long)height * repeat_y;

    if (width == 0 || height == 0)
        return CODEC_INVALID;
    if (wide > JP_MAX_SIDE || tall > JP_MAX_SIDE ||
        wide * tall > JP_MAX_PIXELS)
        return CODEC_TOO_LARGE;
    canvas->rgb = malloc((size_t)width * height * sizeof *canvas->rgb);
    if (canvas->rgb == NULL)
        return CODEC_NO_MEMORY;
    canvas->width = width;
    canvas->height = height;
    canvas->repeat_x = repeat_x;
    canvas->repeat_y = repeat_y;
    return CODEC_OK;
}

static int magic(const uint8_t *data, size_t size, const char *text)
{
    size_t length = strlen(text);
    return size >= length && memcmp(data, text, length) == 0;
}

static int known(const uint8_t *data, size_t size)
{
    return magic(data, size, "MAKI0") || magic(data, size, "Pi") ||
           magic(data, size, "PIC");
}

/* Files from a Macintosh may keep a 128-byte MacBinary header: a zero, a
   file name of 1 to 63 bytes, zeros at 74 and 82, and the data fork's
   length at 83. */
static void skip_macbinary(const uint8_t **data, size_t *size)
{
    const uint8_t *d = *data;
    size_t fork;

    if (*size <= 128 || known(d, *size) || d[0] != 0 || d[1] == 0 ||
        d[1] > 63 || d[74] != 0 || d[82] != 0 || !known(d + 128, *size - 128))
        return;
    fork = (size_t)d[83] << 24 | (size_t)d[84] << 16 | (size_t)d[85] << 8 | d[86];
    *data += 128;
    *size -= 128;
    if (fork != 0 && fork < *size)
        *size = fork;
}

enum codec_result japanpc_decode(const uint8_t *data, size_t size,
                                 struct japanpc_image *image)
{
    struct jp_canvas canvas;
    enum codec_result result;
    unsigned x, y, rx, ry;
    uint8_t *out;

    memset(image, 0, sizeof *image);
    canvas.rgb = NULL;
    skip_macbinary(&data, &size);
    if (magic(data, size, "MAKI02  "))
        result = jp_decode_mag(data, size, &canvas);
    else if (magic(data, size, "MAKI01A ") || magic(data, size, "MAKI01B "))
        result = jp_decode_mki(data, size, &canvas);
    else if (magic(data, size, "Pi"))
        result = jp_decode_pi(data, size, &canvas);
    else if (magic(data, size, "PIC"))
        result = jp_decode_pic(data, size, &canvas);
    else
        return size < 8 ? CODEC_TRUNCATED : CODEC_INVALID;
    if (result != CODEC_OK) {
        free(canvas.rgb);
        return result;
    }

    image->width = canvas.width * canvas.repeat_x;
    image->height = canvas.height * canvas.repeat_y;
    image->rgba = malloc((size_t)image->width * image->height * 4u);
    if (image->rgba == NULL) {
        free(canvas.rgb);
        memset(image, 0, sizeof *image);
        return CODEC_NO_MEMORY;
    }
    out = image->rgba;
    for (y = 0; y < canvas.height; y++)
        for (ry = 0; ry < canvas.repeat_y; ry++)
            for (x = 0; x < canvas.width; x++) {
                uint32_t c = canvas.rgb[(size_t)y * canvas.width + x];
                for (rx = 0; rx < canvas.repeat_x; rx++) {
                    *out++ = (uint8_t)(c >> 16);
                    *out++ = (uint8_t)(c >> 8);
                    *out++ = (uint8_t)c;
                    *out++ = 255;
                }
            }
    free(canvas.rgb);
    return CODEC_OK;
}

void japanpc_free(struct japanpc_image *image)
{
    free(image->rgba);
    image->rgba = NULL;
}
