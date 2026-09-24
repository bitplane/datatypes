#ifndef BITPLANE_QOI_DECODE_H
#define BITPLANE_QOI_DECODE_H
#include <stddef.h>
#include <stdint.h>
enum qoi_result { QOI_OK = 0, QOI_INVALID, QOI_TRUNCATED, QOI_TOO_LARGE, QOI_NO_MEMORY };
struct qoi_image { unsigned width, height; uint8_t *rgba; };
enum qoi_result qoi_decode(const uint8_t *data, size_t length, struct qoi_image *image);
void qoi_free(struct qoi_image *image);
#endif
