#ifndef BITPLANE_COMMON_PNG_H
#define BITPLANE_COMMON_PNG_H
#include <stddef.h>
#include <stdint.h>
#include "common/result.h"

/* PNG decoding and encoding for codecs. png.c needs zlib.c, so users include
   common/zlib.h too: build.sh links only the modules a format includes. */

#define PNG_MAX_PIXELS (16u * 1024u * 1024u)
#define PNG_MAX_SIDE 65535u

int png_signature(const uint8_t *data, size_t length);
/* Read the size from IHDR, checking the header without decoding pixels. */
enum codec_result png_info(const uint8_t *data, size_t length,
                           unsigned *width, unsigned *height);
/* Decode to 8-bit straight RGBA; the caller frees *rgba. */
enum codec_result png_decode(const uint8_t *data, size_t length,
                             unsigned *width, unsigned *height, uint8_t **rgba);
/* Encode RGBA as 8-bit RGB, or RGBA when a pixel isn't opaque. The caller
   frees *out. */
enum codec_result png_encode(const uint8_t *rgba, unsigned width, unsigned height,
                             uint8_t **out, size_t *out_length);
#endif
