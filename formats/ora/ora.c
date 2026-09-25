#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common/png.h"
/* png.c needs zlib.c, and build.sh links only the modules included here. */
#include "common/zlib.h"
#include "ora.h"
#include "zip.h"

/* A 16M-pixel RGBA PNG stored with no compression stays below this. */
#define MAX_MERGED (80u * 1024u * 1024u)
#define THUMB_SIDE 256u

static const char *const mimetypes[] = {"image/openraster", "application/x-krita"};

/* The mimetype member names the format. Some templates carry an empty or
   generic one, so the format's own document also identifies it. */
static enum codec_result check_type(const uint8_t *data, size_t length)
{
    struct zip_entry entry;
    enum codec_result result;
    uint8_t *text;
    size_t n, i;

    result = zip_find(data, length, "stack.xml", &entry);
    if (result == CODEC_INVALID)
        result = zip_find(data, length, "maindoc.xml", &entry);
    if (result != CODEC_INVALID)
        return result;
    result = zip_find(data, length, "mimetype", &entry);
    if (result != CODEC_OK)
        return result;
    if (entry.size > 64)
        return CODEC_INVALID;
    result = zip_extract(&entry, 64, &text);
    if (result != CODEC_OK)
        return result;
    for (n = entry.size; n > 0 && (text[n - 1] == '\n' || text[n - 1] == '\r' ||
                                   text[n - 1] == ' ' || text[n - 1] == '\0'); n--)
        ;
    result = CODEC_INVALID;
    for (i = 0; i < sizeof mimetypes / sizeof mimetypes[0]; i++)
        if (n == strlen(mimetypes[i]) && memcmp(text, mimetypes[i], n) == 0)
            result = CODEC_OK;
    free(text);
    return result;
}

enum codec_result ora_decode(const uint8_t *data, size_t length, struct ora_image *image)
{
    struct zip_entry entry;
    enum codec_result result;
    uint8_t *png;

    memset(image, 0, sizeof *image);
    result = check_type(data, length);
    if (result != CODEC_OK)
        return result;
    /* Files from before mergedimage.png became required need compositing. */
    result = zip_find(data, length, "mergedimage.png", &entry);
    if (result != CODEC_OK)
        return result;
    result = zip_extract(&entry, MAX_MERGED, &png);
    if (result != CODEC_OK)
        return result;
    result = png_decode(png, entry.size, &image->width, &image->height, &image->rgba);
    free(png);
    return result;
}

void ora_free(struct ora_image *image)
{
    free(image->rgba);
    image->rgba = NULL;
}

/* Shrink to fit THUMB_SIDE, averaging each box with alpha weighting. */
static uint8_t *thumbnail(const uint8_t *rgba, unsigned width, unsigned height,
                          unsigned *tw, unsigned *th)
{
    unsigned x, y, sx, sy, c;
    uint8_t *out;

    if (width <= THUMB_SIDE && height <= THUMB_SIDE) {
        *tw = width;
        *th = height;
    } else if (width >= height) {
        *tw = THUMB_SIDE;
        *th = (unsigned)(((unsigned long)height * THUMB_SIDE + width / 2u) / width);
    } else {
        *th = THUMB_SIDE;
        *tw = (unsigned)(((unsigned long)width * THUMB_SIDE + height / 2u) / height);
    }
    if (*tw == 0)
        *tw = 1;
    if (*th == 0)
        *th = 1;
    out = malloc((size_t)*tw * *th * 4u);
    if (out == NULL)
        return NULL;
    for (y = 0; y < *th; y++) {
        unsigned y0 = (unsigned)((unsigned long)y * height / *th);
        unsigned y1 = (unsigned)((unsigned long)(y + 1u) * height / *th);
        for (x = 0; x < *tw; x++) {
            unsigned x0 = (unsigned)((unsigned long)x * width / *tw);
            unsigned x1 = (unsigned)((unsigned long)(x + 1u) * width / *tw);
            unsigned long sum[4] = {0, 0, 0, 0}, count = 0;
            uint8_t *o = out + ((size_t)y * *tw + x) * 4u;
            for (sy = y0; sy < y1; sy++)
                for (sx = x0; sx < x1; sx++) {
                    const uint8_t *p = rgba + ((size_t)sy * width + sx) * 4u;
                    for (c = 0; c < 3; c++)
                        sum[c] += (unsigned long)p[c] * p[3];
                    sum[3] += p[3];
                    count++;
                }
            for (c = 0; c < 3; c++)
                o[c] = sum[3] ? (uint8_t)((sum[c] + sum[3] / 2u) / sum[3]) : 0;
            o[3] = (uint8_t)((sum[3] + count / 2u) / count);
        }
    }
    return out;
}

enum codec_result ora_encode(const uint8_t *rgba, unsigned width, unsigned height,
                             uint8_t **out, size_t *out_length)
{
    static const char mimetype[] = "image/openraster";
    char stack[400];
    struct zip_writer w;
    uint8_t *png = NULL, *thumb_png = NULL, *thumb, *zip;
    size_t png_length, thumb_length, bound;
    unsigned tw, th;
    int stack_length;
    enum codec_result result;

    *out = NULL;
    *out_length = 0;
    result = png_encode(rgba, width, height, &png, &png_length);
    if (result != CODEC_OK)
        return result;
    thumb = thumbnail(rgba, width, height, &tw, &th);
    if (thumb == NULL) {
        free(png);
        return CODEC_NO_MEMORY;
    }
    result = png_encode(thumb, tw, th, &thumb_png, &thumb_length);
    free(thumb);
    if (result != CODEC_OK) {
        free(png);
        return result;
    }
    stack_length = snprintf(stack, sizeof stack,
        "<?xml version='1.0' encoding='UTF-8'?>\n"
        "<image version=\"0.0.6\" w=\"%u\" h=\"%u\" xres=\"72\" yres=\"72\">\n"
        " <stack>\n"
        "  <layer name=\"Background\" src=\"data/layer0.png\" x=\"0\" y=\"0\""
        " opacity=\"1.000\" visibility=\"visible\" composite-op=\"svg:src-over\"/>\n"
        " </stack>\n"
        "</image>\n", width, height);

    /* The layer and the composite are the same picture. */
    bound = png_length > ((size_t)-1 - thumb_length - 1024u) / 2u ? 0 :
            zip_writer_bound(5, 128,sizeof mimetype - 1u + (size_t)stack_length +
                             2u * png_length + thumb_length);
    zip = bound ? malloc(bound) : NULL;
    if (zip == NULL) {
        free(png);
        free(thumb_png);
        return bound ? CODEC_NO_MEMORY : CODEC_TOO_LARGE;
    }
    zip_writer_init(&w, zip, bound);
    /* mimetype comes first and stored, so the file has a fixed magic. */
    result = zip_add(&w, "mimetype", (const uint8_t *)mimetype, sizeof mimetype - 1u);
    if (result == CODEC_OK)
        result = zip_add(&w, "stack.xml", (const uint8_t *)stack, (size_t)stack_length);
    if (result == CODEC_OK)
        result = zip_add(&w, "data/layer0.png", png, png_length);
    if (result == CODEC_OK)
        result = zip_add(&w, "Thumbnails/thumbnail.png", thumb_png, thumb_length);
    if (result == CODEC_OK)
        result = zip_add(&w, "mergedimage.png", png, png_length);
    if (result == CODEC_OK)
        result = zip_finish(&w);
    free(png);
    free(thumb_png);
    if (result != CODEC_OK) {
        free(zip);
        return result;
    }
    *out = zip;
    *out_length = w.length;
    return CODEC_OK;
}
