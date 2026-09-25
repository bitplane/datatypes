#ifndef BITPLANE_COMMON_BPTC_H
#define BITPLANE_COMMON_BPTC_H
#include <stdint.h>

/* BPTC block decoders. Each reads one compressed 16-byte 4x4 block. */

/* BC6H: writes 16 pixels of three half floats, row by row, to half[48]. */
void bc6h_block(const uint8_t *in, uint16_t *half, int is_signed);
/* BC7: writes 16 pixels as RGBA, row by row, to out[64]. A block with a
   reserved mode is opaque black. */
void bc7_block(const uint8_t *in, uint8_t *out);

#endif
