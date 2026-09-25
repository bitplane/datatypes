#ifndef BITPLANE_COMMON_ICO_H
#define BITPLANE_COMMON_ICO_H
#include <stddef.h>
#include <stdint.h>
#include "common/result.h"

#define ICO_MAX_SIDE 65535u
#define ICO_MAX_PIXELS (16u * 1024u * 1024u)

/* One directory entry, described by its image's own header. */
struct ico_entry {
    unsigned width, height, depth;
    int png;                 /* PNG data, which the codec leaves to the caller */
    size_t offset, size;     /* the entry's bytes within the file */
    unsigned hot_x, hot_y;   /* cursor hotspot; 0 in icons */
};
struct ico_image { unsigned width, height; uint8_t *rgba; };

/* Check the file header. *cursor is nonzero for CUR files. */
enum codec_result ico_directory(const uint8_t *data, size_t length,
                                unsigned *count, int *cursor);
/* Describe entry index, in file order, checking that its data is all there. */
enum codec_result ico_entry(const uint8_t *data, size_t length, unsigned index,
                            struct ico_entry *entry);
/* The largest, then deepest, readable entry; the first one wins ties. */
enum codec_result ico_best(const uint8_t *data, size_t length, unsigned *index);
/* Decode a BMP entry to RGBA. */
enum codec_result ico_decode_bmp(const uint8_t *data, size_t length,
                                 const struct ico_entry *entry,
                                 struct ico_image *image);
void ico_free(struct ico_image *image);
#endif
