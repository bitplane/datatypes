#ifndef BITPLANE_INFO_DECODE_H
#define BITPLANE_INFO_DECODE_H
#include <stddef.h>
#include <stdint.h>
#include "common/result.h"

#define INFO_MAX_SIDE 65535u
#define INFO_MAX_PIXELS (16u * 1024u * 1024u)
/* Normal and selected images of each kind. */
#define INFO_MAX_ENTRIES 8

/* Kinds in the order Workbench prefers them. */
enum info_kind { INFO_PLANAR, INFO_NEWICON, INFO_IMAG, INFO_ARGB };

/* One loadable image. Offsets are into the whole file. */
struct info_entry {
    enum info_kind kind;
    int selected;            /* the second image of its kind */
    unsigned width, height;
    size_t offset;           /* planar: bitplanes; IMAG, ARGB: chunk body;
                                NewIcons: the length of its first IMn= tooltype */
    size_t size;             /* bytes available from offset */
    size_t palette;          /* IMAG: body of the chunk holding the palette */
    unsigned depth;          /* planar: planes */
    unsigned colours;        /* NewIcons: palette entries */
    unsigned pick, on_off;   /* planar: PlanePick and PlaneOnOff */
    unsigned revision;       /* planar: the icon's Workbench revision */
};

struct info_icon {
    unsigned count;
    struct info_entry entries[INFO_MAX_ENTRIES];
};

struct info_image { unsigned width, height; uint8_t *rgba; };

/* Find every loadable image in an icon file. */
enum codec_result info_parse(const uint8_t *data, size_t length,
                             struct info_icon *icon);
/* The image Workbench would show: the richest kind, normal before selected. */
unsigned info_best(const struct info_icon *icon);
enum codec_result info_decode(const uint8_t *data, size_t length,
                              const struct info_icon *icon, unsigned index,
                              struct info_image *image);
void info_free(struct info_image *image);
#endif
