#ifndef BITPLANE_QOI_DECODE_H
#define BITPLANE_QOI_DECODE_H
#include <stddef.h>
#include <stdint.h>
#include "common/result.h"
struct qoi_image { unsigned width, height; uint8_t *rgba; };
enum codec_result qoi_decode(const uint8_t *data, size_t length, struct qoi_image *image);
void qoi_free(struct qoi_image *image);
#endif
