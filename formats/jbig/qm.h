#ifndef BITPLANE_JBIG_QM_H
#define BITPLANE_JBIG_QM_H
#include <stddef.h>
#include <stdint.h>

/* The adaptive binary arithmetic coder of T.82 6.8. Each context is one
   byte: its Table 24 state, with the more probable symbol in the top bit.
   A zeroed context is the state at the top of an image. */
#define QM_MPS 0x80u

/* Decoder over protected stripe coded data, where 0xff 0x00 stands for 0xff.
   Zero bytes follow the end. */
struct qm_decoder { const uint8_t *next, *end; uint32_t c, a; unsigned ct; };

void qm_decode_init(struct qm_decoder *d, const uint8_t *pscd, size_t length);
int qm_decode(struct qm_decoder *d, uint8_t *context);

/* Receives the encoder's protected stripe coded data a byte at a time;
   returns 0 on failure. */
typedef int qm_sink(void *state, uint8_t byte);

struct qm_encoder {
    uint32_t c, a;
    unsigned ct;
    unsigned long sc;    /* 0xff bytes waiting on a carry */
    unsigned long zeros; /* 0x00 bytes held back: trailing ones are dropped */
    unsigned buffer;     /* the byte waiting on a carry */
    int first;           /* the first byte out is the spare initial buffer */
    int failed;
    qm_sink *sink;
    void *sink_state;
};

void qm_encode_init(struct qm_encoder *e, qm_sink *sink, void *sink_state);
void qm_encode(struct qm_encoder *e, uint8_t *context, int pixel);
/* End the stripe coded data. Returns 0 if the sink failed at any point. */
int qm_encode_flush(struct qm_encoder *e);
#endif
