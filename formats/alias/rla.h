#ifndef BITPLANE_ALIAS_RLA_H
#define BITPLANE_ALIAS_RLA_H
/* Wavefront RLA: a 740-byte big-endian header, then one 32-bit offset per
   scanline, bottom row first. */
#define RLA_HEADER_SIZE 740u
/* Channel storage types. */
#define RLA_BYTE 0u
#define RLA_WORD 1u
#endif
