#ifndef BITPLANE_DCX_DECODE_H
#define BITPLANE_DCX_DECODE_H
#include <stddef.h>
#include <stdint.h>
#include "common/result.h"
#include "pcxdecode.h"

#define DCX_MAGIC 0x3ade68b1u
#define DCX_MAX_PAGES 1024u

/* Check the page directory and count the pages. */
enum codec_result dcx_count(const uint8_t *data, size_t length, unsigned *count);
/* Decode page index, counted from 0 in directory order. Free with pcx_free. */
enum codec_result dcx_decode(const uint8_t *data, size_t length, unsigned index,
                             struct pcx_image *image);
#endif
