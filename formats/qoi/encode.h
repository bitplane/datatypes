#ifndef BITPLANE_QOI_ENCODE_H
#define BITPLANE_QOI_ENCODE_H
#include <stddef.h>
#include <stdint.h>
struct qoi_encoder {
    uint8_t previous[4];
    uint8_t index[64][4];
    unsigned run;
};
int qoi_make_header(unsigned width, unsigned height, uint8_t header[14]);
void qoi_encoder_init(struct qoi_encoder *encoder);
/* SIZE_MAX signals a too-small output buffer; zero means the row stays in a run. */
size_t qoi_encode_row(struct qoi_encoder *encoder, const uint8_t *rgba,
                      unsigned width, uint8_t *output, size_t capacity);
size_t qoi_encode_end(struct qoi_encoder *encoder, uint8_t *output, size_t capacity);
#endif
