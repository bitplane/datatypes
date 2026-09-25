#ifndef BITPLANE_INFO_ENCODE_H
#define BITPLANE_INFO_ENCODE_H
#include <stddef.h>
#include <stdint.h>

/* OS 3.5 icon images are at most 256 pixels a side. */
#define INFO_ENCODE_MAX_SIDE 256u

/* Bytes info_encode may need, or 0 if the image can't be saved. */
size_t info_encode_capacity(unsigned width, unsigned height);
/* Save straight RGBA as a project icon: a two-plane image for old Workbenches,
   then an OS 3.5 palette image when the picture has at most 256 colours and
   no partial transparency, or a zlib ARGB image otherwise.
   Returns the bytes written, or 0 on failure. */
size_t info_encode(const uint8_t *rgba, unsigned width, unsigned height,
                   uint8_t *out, size_t capacity);
#endif
