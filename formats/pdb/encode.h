#ifndef BITPLANE_PDB_ENCODE_H
#define BITPLANE_PDB_ENCODE_H
#include <stddef.h>
#include <stdint.h>
/* PDB header, one record entry and the image record's header. */
#define PDB_HEADER_SIZE 144
/* ImageViewer needs a width that is a multiple of 16; rows are padded with white. */
unsigned pdb_padded_width(unsigned width);
/* The smallest depth (1, 2 or 4 bits) that holds this row's grays exactly,
   after compositing over white, or 4 when none does. */
unsigned pdb_row_depth(const uint8_t *rgba, unsigned width);
/* An uncompressed image of this size and depth, named name (NULL for "Image").
   Returns 0 for an unsupported size or depth. */
int pdb_make_header(const char *name, unsigned width, unsigned height, unsigned depth,
                    uint8_t header[PDB_HEADER_SIZE]);
/* Bytes in one stored row. */
size_t pdb_row_bytes(unsigned width, unsigned depth);
/* Pack one RGBA row, composited over white and padded, at this depth.
   Returns the pdb_row_bytes written, or zero. */
size_t pdb_encode_row(const uint8_t *rgba, unsigned width, unsigned depth,
                      uint8_t *output, size_t capacity);
#endif
