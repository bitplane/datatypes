#ifndef BITPLANE_PAM_DECODE_H
#define BITPLANE_PAM_DECODE_H
#include <stddef.h>
#include <stdint.h>
#include "common/result.h"
/* A stream of images, each PAM (P7) or float map (PF, Pf, PF4, PH, Ph). */
struct pam_image { unsigned width, height; uint8_t *rgba; };
/* Number of complete images before the end of the stream or the first
   one that doesn't decode. */
unsigned pam_count(const uint8_t *data, size_t length);
/* Decode image index, counting from 0 in file order. */
enum codec_result pam_decode(const uint8_t *data, size_t length, unsigned index,
                             struct pam_image *image);
void pam_free(struct pam_image *image);
#endif
