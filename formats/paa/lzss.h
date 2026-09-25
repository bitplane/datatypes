#ifndef BITPLANE_PAA_LZSS_H
#define BITPLANE_PAA_LZSS_H
#include <stddef.h>
#include <stdint.h>
#include "common/result.h"

/* Bohemia's LZSS: a flag byte, low bit first, marks each of the next eight
   items as a literal (1) or a two-byte back reference: a 12-bit distance and
   a 4-bit length less 3. Positions before the start read as spaces. The data
   ends with a 4-byte sum of the output bytes, which PAA textures sum as
   signed bytes and other BI files as unsigned ones.

   Expand exactly out_len bytes and check the sum, either way. */
enum codec_result paa_lzss_expand(const uint8_t *in, size_t in_len,
                                  uint8_t *out, size_t out_len);

/* Largest packed size of len bytes, including the sum. */
size_t paa_lzss_bound(size_t len);
/* Pack len bytes into out, which holds paa_lzss_bound(len) bytes, with a
   signed sum. Returns the packed size. */
size_t paa_lzss_pack(const uint8_t *in, size_t len, uint8_t *out);
#endif
