#ifndef BITPLANE_PCD_DECODE_H
#define BITPLANE_PCD_DECODE_H
#include <stddef.h>
#include <stdint.h>
#include "common/result.h"

struct pcd_image { unsigned width, height; uint8_t *rgba; };

/* Load the default picture: the largest resolution of an image pack, or the
   first thumbnail of an overview pack. */
#define PCD_DEFAULT ((unsigned long)-1)

/* An image pack holds Base/16 (192x128), Base/4, Base (768x512) and, when its
   header says so, the Huffman-coded 4Base and 16Base, in that order. An
   overview pack holds 192x128 thumbnails; only those wholly inside the file
   count. */
enum codec_result pcd_count(const uint8_t *data, size_t length, unsigned long *count);
/* Decode one picture to opaque RGBA, turned upright as the header says. */
enum codec_result pcd_decode(const uint8_t *data, size_t length, unsigned long index,
                             struct pcd_image *image);
void pcd_free(struct pcd_image *image);
#endif
