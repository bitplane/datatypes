#include "encode.h"
#include "codes.h"

#define EOL_CODE "000000000001"

struct sink { struct fax_encoder *encoder; uint8_t *output; size_t size; };

static void put(struct sink *sink, const char *code)
{
    struct fax_encoder *e = sink->encoder;
    for (; *code != '\0'; code++) {
        e->bits = (e->bits << 1) | (uint32_t)(*code == '1');
        if (++e->count == 8) {
            sink->output[sink->size++] = (uint8_t)e->bits;
            e->bits = 0;
            e->count = 0;
        }
    }
}

static void put_run(struct sink *sink, unsigned colour, unsigned run)
{
    while (run >= FAX_MAX_MAKEUP + 64u) {
        put(sink, fax_extended[FAX_EXTENDED_CODES - 1]);
        run -= FAX_MAX_MAKEUP;
    }
    if (run >= 1792u)
        put(sink, fax_extended[run / 64u - 28u]);
    else if (run >= 64u)
        put(sink, fax_makeup[colour][run / 64u - 1u]);
    put(sink, fax_terminating[colour][run % 64u]);
}

static unsigned over_white(const uint8_t *pixel, unsigned channel)
{
    unsigned a = pixel[3];
    return (pixel[channel] * a + 255u * (255u - a) + 127u) / 255u;
}

static unsigned is_black(const uint8_t *pixel)
{
    unsigned luma = 77u * over_white(pixel, 0) + 150u * over_white(pixel, 1) +
                    29u * over_white(pixel, 2);
    return luma < 128u * 256u;
}

void fax_encoder_init(struct fax_encoder *encoder)
{
    encoder->bits = 0;
    encoder->count = 0;
}

size_t fax_row_capacity(unsigned width)
{
    /* At worst every pixel is a run: a 1-pixel white run is 6 bits and a
       1-pixel black run 3, so 5 bits a pixel covers the row, plus the EOL
       and a white run of 0 before a black start. */
    return ((size_t)width * 5u + 12u + 8u + 7u) / 8u + 1u;
}

size_t fax_encode_row(struct fax_encoder *encoder, const uint8_t *rgba, unsigned width,
                      uint8_t *output, size_t capacity)
{
    struct sink sink;
    unsigned x = 0, colour = 0, run;

    if (encoder == NULL || rgba == NULL || output == NULL || width == 0 || width > 65535u ||
        capacity < fax_row_capacity(width))
        return SIZE_MAX;
    sink.encoder = encoder;
    sink.output = output;
    sink.size = 0;
    put(&sink, EOL_CODE);
    /* Runs alternate from white, so a row starting black opens with white 0. */
    while (x < width) {
        for (run = 0; x + run < width && is_black(rgba + (size_t)(x + run) * 4u) == colour; run++)
            ;
        put_run(&sink, colour, run);
        x += run;
        colour ^= 1u;
    }
    return sink.size;
}

size_t fax_encode_end(struct fax_encoder *encoder, uint8_t output[FAX_END_MAX])
{
    struct sink sink;
    unsigned i;

    sink.encoder = encoder;
    sink.output = output;
    sink.size = 0;
    for (i = 0; i < 6; i++)
        put(&sink, EOL_CODE);
    while (encoder->count != 0)
        put(&sink, "0");
    return sink.size;
}
