#ifndef BITPLANE_DDS_DECODE_H
#define BITPLANE_DDS_DECODE_H
#include <stddef.h>
#include <stdint.h>
#include "common/result.h"

struct dds_image { unsigned width, height; uint8_t *rgba; };

/* Images are numbered in file order: each array slice or cube face in turn,
   and within it each mip level, top first. A volume texture numbers every
   depth slice of every level. Only images wholly inside the file count. */
enum codec_result dds_count(const uint8_t *data, size_t length, unsigned long *count);
/* Decode one image to straight RGBA. Premultiplied alpha is divided out and
   an alpha channel that is zero everywhere is shown opaque. */
enum codec_result dds_decode(const uint8_t *data, size_t length, unsigned long index,
                             struct dds_image *image);
void dds_free(struct dds_image *image);
#endif
