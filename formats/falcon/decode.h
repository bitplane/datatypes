#ifndef BITPLANE_FALCON_DECODE_H
#define BITPLANE_FALCON_DECODE_H
#include <stddef.h>
#include <stdint.h>
#include "common/result.h"

#define FALCON_MAX_SIDE 65535u
#define FALCON_MAX_PIXELS (16u * 1024u * 1024u)

enum falcon_format {
    FALCON_UNKNOWN = 0,
    FALCON_PRISM,     /* Prism Paint and TruePaint PNT/TPI */
    FALCON_DUNE,      /* DuneGraph DG1 */
    FALCON_DUNE_PACKED, /* DuneGraph DC1 */
    FALCON_EGG,       /* EggPaint TRP */
    FALCON_INDY,      /* IndyPaint TRU */
    FALCON_COKE,      /* COKE TG1 */
    FALCON_REMBRANDT, /* Rembrandt TCP */
    FALCON_TT_LOW,    /* DEGAS PI4, 320x480x256 */
    FALCON_TT_MEDIUM, /* DEGAS PI5, 640x480x16 */
    FALCON_TT_HIGH,   /* DEGAS PI6, 1280x960 mono */
    FALCON_FUCKPAINT, /* Fuckpaint PI4, PI7 and PI9 */
    FALCON_FTC,       /* Falcon true colour screen dump */
    FALCON_GOD        /* GodPaint GOD */
};

struct falcon_image {
    unsigned width, height;
    enum falcon_format format;
    uint8_t *rgba;
};

/* Which format data holds, from its content alone. */
enum falcon_format falcon_identify(const uint8_t *data, size_t length);
/* Decode picture index (0 for every format but Rembrandt, which can hold
   several) to opaque RGBA. count, when not NULL, receives the number of
   pictures once the header has been read, even if decoding then fails. */
enum codec_result falcon_decode(const uint8_t *data, size_t length,
                                unsigned index, struct falcon_image *image,
                                unsigned *count);
void falcon_free(struct falcon_image *image);
#endif
