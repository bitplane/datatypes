#ifndef BITPLANE_FAX_ENCODE_H
#define BITPLANE_FAX_ENCODE_H
#include <stddef.h>
#include <stdint.h>

/* The RTC that ends the page, plus the last partial byte. */
#define FAX_END_MAX 10

/* Bits waiting for a whole byte. */
struct fax_encoder { uint32_t bits; unsigned count; };

void fax_encoder_init(struct fax_encoder *encoder);
/* Output space one row may need. */
size_t fax_row_capacity(unsigned width);
/* Group 3 MH: an EOL, then one RGBA row composited over white and set black
   where its luminance is under half. Returns the bytes completed, which may
   be none, or SIZE_MAX on error. */
size_t fax_encode_row(struct fax_encoder *encoder, const uint8_t *rgba, unsigned width,
                      uint8_t *output, size_t capacity);
/* RTC, then zero bits to the end of the byte. Returns the bytes written. */
size_t fax_encode_end(struct fax_encoder *encoder, uint8_t output[FAX_END_MAX]);
#endif
