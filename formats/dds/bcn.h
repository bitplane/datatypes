#ifndef BITPLANE_DDS_BCN_H
#define BITPLANE_DDS_BCN_H
#include <stdint.h>

/* Block decoders. Each reads one compressed 4x4 block and writes its 16 pixels
   as RGBA, row by row, to out[64]. */

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
/* BC6H, 16 bytes: writes 16 pixels of three half floats to half[48]. */
void bc6h_block(const uint8_t *in, uint16_t *half, int is_signed);
/* BC7, 16 bytes. A block with a reserved mode is opaque black. */
void bc7_block(const uint8_t *in, uint8_t *out);

#endif
