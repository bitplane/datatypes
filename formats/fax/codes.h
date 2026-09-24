#ifndef BITPLANE_FAX_CODES_H
#define BITPLANE_FAX_CODES_H
#include <stdint.h>

/* ITU-T T.4 modified Huffman run-length codes, written as bit strings. */
#define FAX_MAKEUP_CODES 27    /* 64 to 1728 in steps of 64, one table per colour */
#define FAX_EXTENDED_CODES 13  /* 1792 to 2560 in steps of 64, shared by both colours */
#define FAX_LOOKUP_BITS 13     /* the longest code */
#define FAX_MAX_MAKEUP 2560

extern const char *const fax_terminating[2][64];
extern const char *const fax_makeup[2][FAX_MAKEUP_CODES];
extern const char *const fax_extended[FAX_EXTENDED_CODES];

/* Indexed by the next FAX_LOOKUP_BITS bits of input. A length of zero means
   no code starts with those bits. */
struct fax_lookup {
    uint16_t run[1u << FAX_LOOKUP_BITS];
    uint8_t length[1u << FAX_LOOKUP_BITS];
};

/* colour is 0 for white, 1 for black. */
void fax_build_lookup(struct fax_lookup *lookup, int colour);

#endif
