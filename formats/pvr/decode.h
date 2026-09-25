#ifndef BITPLANE_PVR_DECODE_H
#define BITPLANE_PVR_DECODE_H
#include <stddef.h>
#include <stdint.h>
#include "common/result.h"

struct pvr_image { unsigned width, height; uint8_t *rgba; };

/* Images are numbered in file order. Version 3 stores each mip level in
   turn, top first, and within it every array surface, cube face and depth
   slice. Version 2 stores each surface or face in turn, and within it every
   mip level. Only images wholly inside the file count. */
enum codec_result pvr_count(const uint8_t *data, size_t length, unsigned long *count);
/* Decode one image to straight RGBA, turned upright when the header says it
   is stored mirrored or bottom up. */
enum codec_result pvr_decode(const uint8_t *data, size_t length, unsigned long index,
                             struct pvr_image *image);
void pvr_free(struct pvr_image *image);
#endif
