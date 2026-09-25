#ifndef BITPLANE_GIMP_XCF_H
#define BITPLANE_GIMP_XCF_H
#include <stddef.h>
#include <stdint.h>
#include "common/result.h"

/* XCF's parsed structure, shared by the parser (xcf.c) and the compositor
   (compose.c). Offsets are into the file. */

#define XCF_TILE 64u

/* Pixel storage of one layer or mask. */
struct xcf_buffer {
    unsigned width, height;
    unsigned channels;       /* 1 to 4 */
    size_t tiles;            /* offset of the level's tile table, 0 if empty */
};

enum xcf_base { XCF_RGB, XCF_GREY, XCF_INDEXED };

struct xcf_layer {
    unsigned width, height;
    long x, y;
    enum xcf_base base;
    int alpha;
    int visible, group, floating;
    float opacity;
    int mode;
    /* As stored: 0 when absent, otherwise the property's value, negative
       for "auto" resolved when the file was saved. */
    int blend_space, composite_space, composite_mode;
    struct xcf_buffer pixels;
    int has_mask, apply_mask, show_mask;
    struct xcf_buffer mask;
    /* A visible non-destructive filter that we can't apply. */
    int filtered;
    /* Tree links in stacking order: first child is the top one. */
    long parent, first_child, next_sibling;
    /* The floating selection's target: the offset of a layer or mask. */
    size_t start, mask_start, float_target;
};

struct xcf_file {
    const uint8_t *data;
    size_t length;
    int version;
    unsigned offset_size;     /* 4, or 8 from version 11 */
    unsigned width, height;
    enum xcf_base base;
    unsigned bpc;             /* bytes per component: 1, 2 or 4 */
    int linear;               /* components are linear light */
    int compression;          /* 0 none, 1 RLE, 2 zlib */
    unsigned colours;
    uint8_t colourmap[768];
    struct xcf_layer *layers; /* in file order */
    long count, top;          /* top: first top-level layer, or -1 */
    long floating;            /* the floating selection, or -1 */
    size_t entries;           /* tile table entries checked so far */
};

/* Parse xcf->data and xcf->length into the rest of *xcf, which starts
   zeroed. The caller frees xcf->layers, even after an error. */
enum codec_result xcf_parse(struct xcf_file *xcf);

/* Bytes of scratch space xcf_tile needs. */
#define XCF_TILE_SCRATCH (XCF_TILE * XCF_TILE * 16u)

/* Decode one tile of b into out, each component scaled to 0..65535 (4-byte
   components are rounded to 16 bits). out holds 64 * 64 * channels values,
   interleaved; tw and th get the tile's size. An empty buffer reads as
   zeros. */
enum codec_result xcf_tile(const struct xcf_file *xcf, const struct xcf_buffer *b,
                           unsigned col, unsigned row, uint8_t *scratch,
                           uint16_t *out, unsigned *tw, unsigned *th);

/* Composite the parsed layers into rgba (width * height * 4 bytes). */
enum codec_result xcf_composite(const struct xcf_file *xcf, uint8_t *rgba);
#endif
