#ifndef BITPLANE_LUNAPAINT_DECODE_H
#define BITPLANE_LUNAPAINT_DECODE_H
#include <stddef.h>
#include <stdint.h>
#include "common/result.h"

/* Projects hold every layer of every frame at 8 bytes a pixel, so the
   decoder reads what it needs from the source instead of taking the whole
   file. read copies length bytes at offset into buffer and returns 0, or
   non-zero if it can't; the decoder never asks for bytes past size. */
struct lunapaint_source {
    void *context;
    uint64_t size;
    int (*read)(void *context, uint64_t offset, void *buffer, size_t length);
};

struct lunapaint_image {
    unsigned width, height;
    unsigned frames;        /* set whenever the header is readable */
    uint8_t *rgba;
};

/* Flatten frame index of a project: visible layers bottom (layer 0) up,
   each scaled by its opacity, as Lunapaint composites them. A bad index is
   CODEC_INVALID, with image->frames still set. */
enum codec_result lunapaint_decode(const struct lunapaint_source *source,
                                   unsigned index,
                                   struct lunapaint_image *image);
void lunapaint_free(struct lunapaint_image *image);
#endif
