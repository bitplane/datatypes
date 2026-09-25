#ifndef BITPLANE_WAL_ENCODE_H
#define BITPLANE_WAL_ENCODE_H
#include <stddef.h>
#include <stdint.h>
struct wal_encoder;
/* NULL for an unsupported size or no memory. */
struct wal_encoder *wal_encoder_new(unsigned width, unsigned height);
/* Add the next RGBA row, composited over white. Zero if a pixel's colour
   isn't in the Quake 2 palette. */
int wal_encoder_row(struct wal_encoder *encoder, const uint8_t *rgba);
/* Build the mip levels once every row is in; the file stays owned by
   the encoder. NULL if rows are missing. */
const uint8_t *wal_encoder_finish(struct wal_encoder *encoder, size_t *size);
void wal_encoder_free(struct wal_encoder *encoder);
#endif
