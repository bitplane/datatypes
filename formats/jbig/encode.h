#ifndef BITPLANE_JBIG_ENCODE_H
#define BITPLANE_JBIG_ENCODE_H
#include <stddef.h>
#include <stdint.h>
#include "decode.h"
#include "qm.h"

/* Options byte flags the encoder can use. */
#define JBIG_TWO_LINE 0x40u
#define JBIG_TYPICAL 0x08u

/* Sequential single-plane coding in one stripe, with no AT moves: the
   lowest resolution layer of T.82 and nothing more. */
struct jbig_encoder {
    unsigned width, height, y, options;
    size_t stride;
    uint8_t *lines; /* the two lines above and the current one */
    uint8_t contexts[1024];
    int ltp;
    struct qm_encoder qm;
};

void jbig_make_header(uint8_t header[JBIG_HEADER_SIZE], unsigned width, unsigned height,
                      unsigned options);
/* Returns 0 if out of memory. The sink gets the stripe data entity. */
int jbig_encoder_init(struct jbig_encoder *e, unsigned width, unsigned height,
                      unsigned options, qm_sink *sink, void *sink_state);
/* One line, packed most significant bit first, 1 for black. */
void jbig_encode_bits(struct jbig_encoder *e, const uint8_t *bits);
/* One RGBA line composited over white, black where its luminance is under half. */
void jbig_encode_rgba(struct jbig_encoder *e, const uint8_t *rgba);
/* End the stripe once every line is in, and free the encoder. Returns 0 if
   the sink failed at any point. */
int jbig_encoder_end(struct jbig_encoder *e);
/* Free an encoder given up on. */
void jbig_encoder_free(struct jbig_encoder *e);
#endif
