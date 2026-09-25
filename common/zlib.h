#ifndef BITPLANE_COMMON_ZLIB_H
#define BITPLANE_COMMON_ZLIB_H

#include <stddef.h>

#include "common/result.h"

/* One-shot zlib (RFC 1950) streams. On AROS these use z1.library, opened for
   the calling task for the length of the call; on the host, the system zlib. */

/* Inflate src into dst. *written is always set to the bytes produced.
   CODEC_OK: the stream ended within dst.
   CODEC_TRUNCATED: all of src was used and the stream hadn't ended.
   CODEC_TOO_LARGE: dst filled while src still had data.
   CODEC_INVALID: the stream is corrupt, or its checksum is wrong.
   CODEC_NO_MEMORY: zlib or its library couldn't be set up. */
enum codec_result zlib_inflate(const unsigned char *src, size_t src_length,
                               unsigned char *dst, size_t dst_length,
                               size_t *written);

/* Inflate raw deflate data (RFC 1951), as zip members hold. Results as
   zlib_inflate, without a checksum to check. */
enum codec_result zlib_inflate_raw(const unsigned char *src, size_t src_length,
                                   unsigned char *dst, size_t dst_length,
                                   size_t *written);

/* Deflate src into dst at level 0-9, or -1 for zlib's default.
   Returns CODEC_TOO_LARGE if dst is too small; zlib_deflate_bound is enough. */
enum codec_result zlib_deflate(const unsigned char *src, size_t src_length,
                               unsigned char *dst, size_t dst_length,
                               int level, size_t *written);

/* An output size that zlib_deflate always fits in, or 0 if that overflows. */
static inline size_t zlib_deflate_bound(size_t src_length)
{
    size_t extra = (src_length >> 3) + 64;
    return src_length > (size_t)-1 - extra ? 0 : src_length + extra;
}

#endif
