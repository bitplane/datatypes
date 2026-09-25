#ifndef BITPLANE_ANI_DECODE_H
#define BITPLANE_ANI_DECODE_H
#include <stddef.h>
#include <stdint.h>
#include "common/ico.h"
#include "common/result.h"

/* One frame: an ICO or CUR file held in an icon chunk, and its chosen entry. */
struct ani_frame {
    const uint8_t *ico;      /* the chunk's data, within the ANI file */
    size_t length;
    int cursor;              /* the frame is a CUR, with a hotspot */
    struct ico_entry entry;  /* the largest, then deepest, entry, within ico */
};

/* Count the icon chunks, in file order. */
enum codec_result ani_count(const uint8_t *data, size_t length, unsigned *count);
/* Find frame index and pick its entry. A PNG entry is left to the caller; a
   BMP one decodes with ico_decode_bmp(frame->ico, frame->length, &frame->entry, ...). */
enum codec_result ani_frame(const uint8_t *data, size_t length, unsigned index,
                            struct ani_frame *frame);
#endif
