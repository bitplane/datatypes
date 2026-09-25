#ifndef BITPLANE_PSD_DECODE_H
#define BITPLANE_PSD_DECODE_H
#include <stddef.h>
#include <stdint.h>
#include "common/result.h"

/* Photoshop files are mostly layer data the composite doesn't need, so the
   decoder reads only the parts it uses. read copies up to length bytes from
   offset and returns how many it copied; size is the file's length. */
struct psd_source {
    size_t (*read)(void *context, uint64_t offset, void *buffer, size_t length);
    void *context;
    uint64_t size;
};

struct psd_image { unsigned width, height; uint8_t *rgba; };

/* Decodes the merged composite of a PSD or PSB file to RGBA. Files saved
   without a real composite, 32-bit files and ZIP-compressed composites are
   CODEC_INVALID. */
enum codec_result psd_decode(const struct psd_source *source,
                             struct psd_image *image);
void psd_free(struct psd_image *image);
#endif
