#ifndef BITPLANE_PSP_PSP_H
#define BITPLANE_PSP_PSP_H
#include <stddef.h>
#include <stdint.h>
#include "common/result.h"

#define PSP_MAX_SIDE 65535u
#define PSP_MAX_PIXELS (16u * 1024u * 1024u)
/* A layer may be larger than the canvas, but not by much more than this. */
#define PSP_MAX_LAYER_PIXELS (32u * 1024u * 1024u)
/* Group layers nested deeper than this are CODEC_INVALID. */
#define PSP_MAX_DEPTH 16u

struct psp_image { unsigned width, height; uint8_t *rgba; };

/* Decode a Paint Shop Pro image (file format 3.0, PSP 5, and later) to
   straight RGBA. Raster layers are composited with their transparency,
   opacity, blend modes, user masks, mask layers and groups. Files with
   visible vector, adjustment or art media layers use the composite
   Paint Shop Pro stored with them, and are CODEC_INVALID without one. */
enum codec_result psp_decode(const uint8_t *data, size_t length,
                             struct psp_image *image);
void psp_free(struct psp_image *image);

/* A one-layer file, format 5.0 (Paint Shop Pro 7 and later), 24-bit with a
   transparency mask when some pixel isn't opaque, channels compressed with
   zlib. *out is malloc'd; the caller frees it. */
enum codec_result psp_encode(const uint8_t *rgba, unsigned width,
                             unsigned height, uint8_t **out, size_t *length);

static inline uint16_t psp_le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static inline uint32_t psp_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static inline void psp_put16(uint8_t *p, unsigned v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static inline void psp_put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

#endif
