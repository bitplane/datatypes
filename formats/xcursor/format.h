#ifndef BITPLANE_XCURSOR_FORMAT_H
#define BITPLANE_XCURSOR_FORMAT_H
/* Layout constants shared by the decoder and encoder. All fields are
   little-endian 32-bit words. */
#define XCURSOR_FILE_HEADER 16u
#define XCURSOR_TOC_ENTRY 12u
#define XCURSOR_IMAGE_HEADER 36u
#define XCURSOR_IMAGE_TYPE 0xfffd0002u
#define XCURSOR_MAX_SIDE 0x7fffu
#endif
