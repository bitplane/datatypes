#ifndef BITPLANE_PVR_ETC_H
#define BITPLANE_PVR_ETC_H
#include <stdint.h>

/* Ericsson texture compression block decoders. Each reads one 4x4 block and
   writes its pixels row by row to out[64] as RGBA, or into one channel of it. */

/* ETC2 RGB, 8 bytes; also decodes ETC1, a subset of it. With punch_through
   (ETC2 RGB A1) a clear opaque bit makes index 2 transparent black. Alpha is
   255 elsewhere. */
void etc2_rgb_block(const uint8_t *in, uint8_t *out, int punch_through);
/* EAC, 8 bytes: an 8-bit channel, as ETC2 RGBA stores alpha. */
void eac8_block(const uint8_t *in, uint8_t *out, unsigned channel);
/* EAC R11 or one half of RG11, 8 bytes, unsigned: the top 8 of 11 bits. */
void eac11_block(const uint8_t *in, uint8_t *out, unsigned channel);

#endif
