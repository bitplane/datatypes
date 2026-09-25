#ifndef BITPLANE_GIMP_GIMP_H
#define BITPLANE_GIMP_GIMP_H
#include <stddef.h>
#include <stdint.h>
#include "common/result.h"

#define GIMP_MAX_SIDE 65535u
#define GIMP_MAX_PIXELS (16u * 1024u * 1024u)

enum gimp_kind { GIMP_UNKNOWN, GIMP_GBR, GIMP_GIH, GIMP_PAT, GIMP_XCF };

struct gimp_image { unsigned width, height; uint8_t *rgba; };

/* Tell the four formats apart by content. */
enum gimp_kind gimp_sniff(const uint8_t *data, size_t length);

/* Load one picture as straight RGBA. A brush pipe holds one picture per
   cell, in file order; every other file holds one. *count is set once the
   file's structure is known to be sound. An index past the end is
   CODEC_INVALID. */
enum codec_result gimp_decode(const uint8_t *data, size_t length, long index,
                              struct gimp_image *image, unsigned *count);
void gimp_free(struct gimp_image *image);

static inline uint32_t gimp_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static inline void gimp_put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

/* Brushes, brush pipes and patterns (brush.c). */
enum codec_result gbr_decode(const uint8_t *data, size_t length,
                             struct gimp_image *image);
enum codec_result gih_decode(const uint8_t *data, size_t length, long index,
                             struct gimp_image *image, unsigned *count);
/* Parse a pipe's two text lines: *header gets the offset of the first cell
   and *cells the number that starts the second line. */
enum codec_result gih_parse_header(const uint8_t *data, size_t length,
                                   size_t *header, unsigned long *cells);
enum codec_result pat_decode(const uint8_t *data, size_t length,
                             struct gimp_image *image);

/* Layered images (xcf.c): the visible layers composited as GIMP shows them. */
enum codec_result xcf_decode(const uint8_t *data, size_t length,
                             struct gimp_image *image);

#endif
