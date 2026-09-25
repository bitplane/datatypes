#ifndef BITPLANE_WAL_DECODE_H
#define BITPLANE_WAL_DECODE_H
#include <stddef.h>
#include <stdint.h>
#include "common/result.h"
#define WAL_HEADER 100
#define WAL_LEVELS 4
struct wal_image { unsigned width, height; uint8_t *rgba; };
/* Mip levels present, largest first; zero for a bad header. */
unsigned wal_count(const uint8_t *data, size_t length);
/* Decode one mip level through the Quake 2 palette, opaque. */
enum codec_result wal_decode(const uint8_t *data, size_t length, unsigned level,
                             struct wal_image *image);
void wal_free(struct wal_image *image);
#endif
