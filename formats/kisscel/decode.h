#ifndef BITPLANE_KISSCEL_DECODE_H
#define BITPLANE_KISSCEL_DECODE_H
#include <stddef.h>
#include <stdint.h>
#include "common/result.h"

struct kisscel_info { unsigned width, height, x, y, bpp; };
struct kisscel_palette { unsigned count; uint8_t rgb[256 * 3]; };
struct kisscel_image { unsigned width, height; uint8_t *rgba; };

/* Read a cel's header: old headerless 4-bit cels, or "KiSS" cels of 4, 8 or
   32 bits. Checks the pixel data is all there. */
enum codec_result kisscel_info(const uint8_t *data, size_t length,
                               struct kisscel_info *info);
/* Decode a cel onto a canvas that includes its x and y offset, as GIMP does.
   Index 0 is transparent. palette may be NULL for a grey ramp, which is what
   GIMP shows without a palette file; it is ignored for 32-bit cels. */
enum codec_result kisscel_decode(const uint8_t *data, size_t length,
                                 const struct kisscel_palette *palette,
                                 struct kisscel_image *image);
void kisscel_free(struct kisscel_image *image);

/* Read palette group from a KCF file, old headerless or "KiSS". A group the
   file doesn't hold falls back to the first. */
enum codec_result kisscel_palette(const uint8_t *data, size_t length,
                                  unsigned group,
                                  struct kisscel_palette *palette);

/* Find the palette file a KiSS configuration (.cnf) gives the cel named cel,
   and the palette group of the first set the cel belongs to. Returns 0 when
   the configuration doesn't list the cel or its palette file; otherwise 1,
   with *kcf pointing into cnf and *kcf_length bytes long. */
int kisscel_cnf_palette(const char *cnf, size_t length, const char *cel,
                        const char **kcf, size_t *kcf_length,
                        unsigned *group);
#endif
