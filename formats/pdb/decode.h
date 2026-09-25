#ifndef BITPLANE_PDB_DECODE_H
#define BITPLANE_PDB_DECODE_H
#include <stddef.h>
#include <stdint.h>
#include "common/result.h"
/* One byte per pixel, as stored: 0 is white and (1 << depth) - 1 is black. */
struct pdb_image { unsigned width, height, depth; uint8_t *pixels; };
/* Reads the image record of a Palm ImageViewer database ("vIMG", "View"):
   1-bit, 2-bit and 4-bit gray, uncompressed or RLE. */
enum codec_result pdb_decode(const uint8_t *data, size_t length, struct pdb_image *image);
void pdb_free(struct pdb_image *image);
/* The gray level, 0 to 255, of a pixel value at this depth. */
unsigned pdb_shade(unsigned depth, unsigned value);
#endif
