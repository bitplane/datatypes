#ifndef BITPLANE_PIXAR_ENCODE_H
#define BITPLANE_PIXAR_ENCODE_H
#include <stddef.h>
#include <stdint.h>
#define PIXAR_ENCODE_HEADER 1024u
/* Header and tile table for one dumped 8-bit tile holding the whole picture:
   RGB, or RGBA with unassociated alpha when alpha is non-zero. The pixels
   follow at offset 1024. */
int pixar_make_header(unsigned width, unsigned height, int alpha,
                      uint8_t header[PIXAR_ENCODE_HEADER]);
/* Encode one RGBA row as RGBA, or as RGB over white. Returns the bytes written. */
size_t pixar_encode_row(const uint8_t *rgba, unsigned width, int alpha,
                        uint8_t *output, size_t capacity);
#endif
