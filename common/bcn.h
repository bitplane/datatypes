#ifndef BITPLANE_COMMON_BCN_H
#define BITPLANE_COMMON_BCN_H
#include <stdint.h>

/* S3TC and RGTC block decoders. Each reads one compressed 4x4 block and writes
   its 16 pixels as RGBA, row by row, to out[64]. BC6H and BC7 are in bptc.h. */

/* BC1 (DXT1), 8 bytes. In three-colour blocks, index 3 is black with alpha 0
   when punch_through is set, and opaque black otherwise. */
void bc1_block(const uint8_t *in, uint8_t *out, int punch_through);
/* BC2 (DXT3), 16 bytes: 4-bit explicit alpha. */
void bc2_block(const uint8_t *in, uint8_t *out);
/* BC3 (DXT5), 16 bytes: interpolated alpha. */
void bc3_block(const uint8_t *in, uint8_t *out);
/* BC4, 8 bytes: one channel, shown as gray. */
void bc4_block(const uint8_t *in, uint8_t *out);
/* BC5, 16 bytes: red and green; blue is 0, or 128 when is_signed, where
   each signed value v is shown as v + 128. */
void bc5_block(const uint8_t *in, uint8_t *out, int is_signed);

#endif
