#ifndef BITPLANE_CMUWM_DECODE_H
#define BITPLANE_CMUWM_DECODE_H
#include <stddef.h>
#include <stdint.h>
#include "common/result.h"
#define CMUWM_HEADER_SIZE 14
#define CMUWM_LONG_HEADER_SIZE 16
#define CMUWM_MAX_SIDE 65535
#define CMUWM_MAX_PIXELS (16UL * 1024 * 1024)
/* One byte per pixel: 0 is white, 1 is black. */
struct cmuwm_image { unsigned width, height; uint8_t *pixels; };
/* Magic 0xf10040bb, width, height and a depth of 1, in the byte order the
   magic is stored in, then byte-padded rows where a set bit is white. The
   header is 14 bytes, or 16 when the file has room for two more. */
enum codec_result cmuwm_decode(const uint8_t *data, size_t length, struct cmuwm_image *image);
void cmuwm_free(struct cmuwm_image *image);
#endif
