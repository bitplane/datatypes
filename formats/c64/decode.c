#include "decode.h"
#include <stdlib.h>
#include <string.h>

/* Pepto's PAL palette, as RECOIL and VICE use it. */
static const uint8_t palette[16][3] = {
    { 0x00, 0x00, 0x00 }, { 0xff, 0xff, 0xff }, { 0x68, 0x37, 0x2b }, { 0x70, 0xa4, 0xb2 },
    { 0x6f, 0x3d, 0x86 }, { 0x58, 0x8d, 0x43 }, { 0x35, 0x28, 0x79 }, { 0xb8, 0xc7, 0x6f },
    { 0x6f, 0x4f, 0x25 }, { 0x43, 0x39, 0x00 }, { 0x9a, 0x67, 0x59 }, { 0x44, 0x44, 0x44 },
    { 0x6c, 0x6c, 0x6c }, { 0x9a, 0xd2, 0x84 }, { 0x6c, 0x5e, 0xb5 }, { 0x95, 0x95, 0x95 }
};

void c64_colour(unsigned index, uint8_t rgb[3])
{
    memcpy(rgb, palette[index & 15u], 3);
}

int c64_index(const uint8_t rgb[3])
{
    int i;

    for (i = 0; i < 16; i++)
        if (memcmp(palette[i], rgb, 3) == 0)
            return i;
    return -1;
}

void c64_free(struct c64_image *image)
{
    free(image->rgba);
    image->rgba = NULL;
    image->width = image->height = 0;
}

/* Sizes of the VIC-II's memory areas as files store them. */
#define BITMAP 8000u
#define MATRIX 1000u
/* Eight video matrices 1K apart, one for each pixel row of a cell. */
#define FLI_MATRICES (7u * 1024u + MATRIX)
#define LOAD_ANY 0xffffffffu

/* One picture as the VIC-II shows it: the bitmap's cells take their colours
   from the video matrix (or a fixed byte), the colour RAM (multicolour only)
   and the background register. */
struct frame {
    const uint8_t *bitmap;
    const uint8_t *matrix;      /* NULL: every cell uses fixed_matrix */
    const uint8_t *colour;      /* NULL: every cell uses fixed_colour */
    const uint8_t *backgrounds; /* one per line, or NULL for background */
    uint8_t fixed_matrix, fixed_colour, background;
    uint8_t multicolour;
    uint8_t fli;                /* a new video matrix on each line of a cell */
    uint8_t shift;              /* pixels moved right, background shown instead */
};

/* A frame and the file it points into. A region past the end marks the frame bad. */
struct source {
    const uint8_t *data;
    size_t length;
    int bad;
};

static const uint8_t *region(struct source *s, size_t offset, size_t size)
{
    if (offset > s->length || s->length - offset < size) {
        s->bad = 1;
        return NULL;
    }
    return s->data + offset;
}

static uint8_t byte_at(struct source *s, size_t offset)
{
    const uint8_t *p = region(s, offset, 1);
    return p == NULL ? 0 : *p;
}

static void hires(struct frame *f, struct source *s, size_t bitmap, size_t matrix)
{
    memset(f, 0, sizeof *f);
    f->bitmap = region(s, bitmap, BITMAP);
    f->matrix = region(s, matrix, MATRIX);
}

/* Hires with one pair of colours for the whole picture: 0xF0 set, 0x0F clear. */
static void hires_fixed(struct frame *f, struct source *s, size_t bitmap, uint8_t colours)
{
    memset(f, 0, sizeof *f);
    f->bitmap = region(s, bitmap, BITMAP);
    f->fixed_matrix = colours;
}

static void multicolour(struct frame *f, struct source *s, size_t bitmap,
                        size_t matrix, size_t colour, size_t background)
{
    memset(f, 0, sizeof *f);
    f->bitmap = region(s, bitmap, BITMAP);
    f->matrix = region(s, matrix, MATRIX);
    f->colour = region(s, colour, MATRIX);
    f->background = byte_at(s, background);
    f->multicolour = 1;
}

static void fli(struct frame *f, struct source *s, size_t bitmap, size_t matrices,
                size_t colour)
{
    memset(f, 0, sizeof *f);
    f->bitmap = region(s, bitmap, BITMAP);
    f->matrix = region(s, matrices, FLI_MATRICES);
    f->colour = region(s, colour, MATRIX);
    f->multicolour = 1;
    f->fli = 1;
}

static void afli(struct frame *f, struct source *s, size_t bitmap, size_t matrices)
{
    memset(f, 0, sizeof *f);
    f->bitmap = region(s, bitmap, BITMAP);
    f->matrix = region(s, matrices, FLI_MATRICES);
    f->fli = 1;
}

/* Colour index of pixel (x, y) in a picture whose columns before left are
   cut off. Pixels shifted in from there show the background. */
static unsigned pixel(const struct frame *f, unsigned x, unsigned y, unsigned left)
{
    unsigned background = f->backgrounds != NULL ? f->backgrounds[y] : f->background;
    unsigned cell, bits, matrix;

    if (x < left + f->shift)
        return background & 15u;
    x -= f->shift;
    cell = y / 8u * 40u + x / 8u;
    bits = f->bitmap[cell * 8u + y % 8u];
    matrix = f->matrix == NULL ? f->fixed_matrix
           : f->matrix[(f->fli ? y % 8u * 1024u : 0) + cell];
    if (!f->multicolour)
        return (bits >> (7u - x % 8u) & 1u ? matrix >> 4 : matrix) & 15u;
    switch (bits >> (6u - (x & 6u)) & 3u) {
    case 0: return background & 15u;
    case 1: return matrix >> 4;
    case 2: return matrix & 15u;
    default: return (f->colour != NULL ? f->colour[cell] : f->fixed_colour) & 15u;
    }
}

/* Render one frame, or blend two. FLI frames lose the FLI bug columns. */
static enum codec_result render(struct c64_image *image, const struct source *s,
                                const struct frame *a, const struct frame *b)
{
    unsigned left = a->fli ? C64_WIDTH - C64_FLI_WIDTH : 0;
    unsigned width = C64_WIDTH - left, x, y;
    uint8_t *p;

    if (s->bad || a->bitmap == NULL || (b != NULL && b->bitmap == NULL))
        return CODEC_INVALID;
    image->rgba = malloc((size_t)width * C64_HEIGHT * 4u);
    if (image->rgba == NULL)
        return CODEC_NO_MEMORY;
    image->width = width;
    image->height = C64_HEIGHT;
    p = image->rgba;
    for (y = 0; y < C64_HEIGHT; y++)
        for (x = left; x < C64_WIDTH; x++) {
            const uint8_t *c = palette[pixel(a, x, y, left)];
            if (b == NULL) {
                memcpy(p, c, 3);
            } else {
                const uint8_t *d = palette[pixel(b, x, y, left)];
                p[0] = (uint8_t)((c[0] + d[0]) / 2u);
                p[1] = (uint8_t)((c[1] + d[1]) / 2u);
                p[2] = (uint8_t)((c[2] + d[2]) / 2u);
            }
            p[3] = 255;
            p += 4;
        }
    return CODEC_OK;
}

static uint16_t load_address(const uint8_t *data)
{
    return (uint16_t)(data[0] | data[1] << 8);
}

/* Byte-oriented RLE: escape, then a count and a value in either order. The
   count has bias added; a run of zero writes nothing, and a run past the end
   of the output is cut short. */
struct rle {
    int escape;
    int value_first;
    unsigned bias;
};

static enum codec_result unrle(const uint8_t *in, size_t length, size_t pos,
                               const struct rle *rle, uint8_t *out, size_t size)
{
    size_t o = 0;

    while (o < size) {
        unsigned b, count, value;
        if (pos >= length)
            return CODEC_TRUNCATED;
        b = in[pos++];
        if ((int)b != rle->escape) {
            out[o++] = (uint8_t)b;
            continue;
        }
        if (length - pos < 2)
            return CODEC_TRUNCATED;
        value = in[pos + (rle->value_first ? 0 : 1)];
        count = in[pos + (rle->value_first ? 1 : 0)] + rle->bias;
        pos += 2;
        for (; count > 0 && o < size; count--)
            out[o++] = (uint8_t)value;
    }
    return CODEC_OK;
}

/* Unpack s from pos into a new source of size bytes, the first start of
   them copied from s so packed files keep their unpacked layout. The
   caller frees u->data. */
static enum codec_result unpack(const struct source *s, size_t pos, const struct rle *rle,
                                size_t start, size_t size, struct source *u)
{
    uint8_t *out;
    enum codec_result r;

    u->data = NULL;
    u->length = size;
    u->bad = 0;
    if (s->length < start)
        return CODEC_TRUNCATED;
    out = calloc(1, size);
    if (out == NULL)
        return CODEC_NO_MEMORY;
    memcpy(out, s->data, start);
    r = unrle(s->data, s->length, pos, rle, out + start, size - start);
    if (r != CODEC_OK) {
        free(out);
        return r;
    }
    u->data = out;
    return CODEC_OK;
}

/* Render from an unpacked source and free it. */
static enum codec_result render_unpacked(struct c64_image *image, struct source *u,
                                         const struct frame *a, const struct frame *b)
{
    enum codec_result r = render(image, u, a, b);
    free((void *)u->data);
    return r;
}

/* CODEC_OK when s is exactly size bytes long. */
static enum codec_result sized(const struct source *s, size_t size)
{
    if (s->length == size)
        return CODEC_OK;
    return s->length < size ? CODEC_TRUNCATED : CODEC_INVALID;
}

static int signature_at(const uint8_t *data, size_t length, size_t offset, const char *text)
{
    size_t n = strlen(text);
    return length >= offset + n && memcmp(data + offset, text, n) == 0;
}

/* ---- Hires ---- */

/* Art Studio, Interpaint hires, Hi-Pic Creator, Gigapaint hires. */
static enum codec_result art_studio(struct c64_image *image, struct source *s)
{
    struct frame f;
    hires(&f, s, 2, 0x1f42);
    return render(image, s, &f, NULL);
}

/* Hires bitmaps without colours: white on black. */
static enum codec_result hires_bitmap(struct c64_image *image, struct source *s)
{
    struct frame f;
    hires_fixed(&f, s, 2, 0x10);
    return render(image, s, &f, NULL);
}

/* Run Paint's monochrome pictures: black on white. */
static enum codec_result run_paint_mono(struct c64_image *image, struct source *s)
{
    struct frame f;
    hires_fixed(&f, s, 2, 0x01);
    return render(image, s, &f, NULL);
}

/* Hi-Eddi and Image System hires. */
static enum codec_result hi_eddi(struct c64_image *image, struct source *s)
{
    struct frame f;
    hires(&f, s, 2, 0x2002);
    return render(image, s, &f, NULL);
}

/* Doodle, Hires-Editor and Run Paint hires: video matrix before the bitmap. */
static enum codec_result doodle(struct c64_image *image, struct source *s)
{
    struct frame f;
    hires(&f, s, 0x402, 2);
    return render(image, s, &f, NULL);
}

/* Koala-style RLE: 0xFE, value, count. */
static const struct rle koala_rle = { 0xfe, 1, 0 };

/* Doodle packed with Koala-style RLE: the 9024 bytes after the load address. */
static enum codec_result doodle_packed(struct c64_image *image, struct source *s)
{
    struct source u;
    struct frame f;
    enum codec_result r = unpack(s, 2, &koala_rle, 2, 2 + 9024, &u);

    if (r != CODEC_OK)
        return r;
    hires(&f, &u, 0x402, 2);
    return render_unpacked(image, &u, &f, NULL);
}

static enum codec_result afli_editor(struct c64_image *image, struct source *s)
{
    struct frame f;
    afli(&f, s, 0x2002, 2);
    return render(image, s, &f, NULL);
}

/* ---- Multicolour ---- */

/* Koala Painter and the programs that copied its layout. */
static enum codec_result koala(struct c64_image *image, struct source *s)
{
    struct frame f;
    multicolour(&f, s, 2, 0x1f42, 0x232a, 0x2712);
    return render(image, s, &f, NULL);
}

/* Koala files copied off disk sometimes carry up to a block's worth of
   junk at the end. They are told apart by Koala's load addresses. */
#define KOALA_SIZE 10003u
#define KOALA_PADDING 64u
static int is_koala_padded(const uint8_t *d, size_t n)
{
    unsigned load = (unsigned)(d[0] | d[1] << 8);
    return n > KOALA_SIZE && n <= KOALA_SIZE + KOALA_PADDING &&
           (load == 0x6000 || load == 0x4400);
}

static enum codec_result koala_padded(struct c64_image *image, struct source *s)
{
    if (!is_koala_padded(s->data, s->length))
        return CODEC_INVALID;
    return koala(image, s);
}

/* Koala without its load address. */
static enum codec_result koala_bare(struct c64_image *image, struct source *s)
{
    struct frame f;
    multicolour(&f, s, 0, 0x1f40, 0x2328, 0x2710);
    return render(image, s, &f, NULL);
}

/* Koala's 10001 bytes packed after a load address. */
static enum codec_result koala_unpacked(struct c64_image *image, struct source *s,
                                        const struct rle *rle)
{
    struct source u;
    struct frame f;
    enum codec_result r = unpack(s, 2, rle, 2, 2 + 10001, &u);

    if (r != CODEC_OK)
        return r;
    multicolour(&f, &u, 2, 0x1f42, 0x232a, 0x2712);
    return render_unpacked(image, &u, &f, NULL);
}

/* Koala packed by Graphics Galaxy. */
static enum codec_result koala_packed(struct c64_image *image, struct source *s)
{
    return koala_unpacked(image, s, &koala_rle);
}

/* Amica Paint: Koala's layout, RLE as 0xC2, count, value. */
static enum codec_result amica(struct c64_image *image, struct source *s)
{
    static const struct rle rle = { 0xc2, 0, 0 };
    return koala_unpacked(image, s, &rle);
}

static enum codec_result advanced_art_studio(struct c64_image *image, struct source *s)
{
    struct frame f;
    multicolour(&f, s, 2, 0x1f42, 0x233a, 0x232b);
    return render(image, s, &f, NULL);
}

static enum codec_result blazing_paddles(struct c64_image *image, struct source *s)
{
    struct frame f;
    multicolour(&f, s, 2, 0x2002, 0x2402, 0x1f82);
    return render(image, s, &f, NULL);
}

static enum codec_result artist64(struct c64_image *image, struct source *s)
{
    struct frame f;
    multicolour(&f, s, 2, 0x2002, 0x2402, 0x2801);
    return render(image, s, &f, NULL);
}

/* Rainbow Painter: no background colour is stored, so it is black. */
static enum codec_result rainbow_painter(struct c64_image *image, struct source *s)
{
    struct frame f;
    multicolour(&f, s, 0x402, 2, 0x2402, 0);
    f.background = 0;
    return render(image, s, &f, NULL);
}

/* Dolphin Ed and Vidcom 64: colour RAM, video matrix, then bitmap. */
static enum codec_result dolphin_ed(struct c64_image *image, struct source *s)
{
    struct frame f;
    multicolour(&f, s, 0x802, 0x402, 2, 0x7ea);
    return render(image, s, &f, NULL);
}

static enum codec_result picasso64(struct c64_image *image, struct source *s)
{
    struct frame f;
    multicolour(&f, s, 0x802, 0x402, 2, 0x801);
    return render(image, s, &f, NULL);
}

static enum codec_result cdu_paint(struct c64_image *image, struct source *s)
{
    struct frame f;
    multicolour(&f, s, 0x113, 0x2053, 0x243b, 0x2823);
    return render(image, s, &f, NULL);
}

static enum codec_result cheese(struct c64_image *image, struct source *s)
{
    struct frame f;
    multicolour(&f, s, 2, 0x4202, 0x4802, 0x4fff);
    return render(image, s, &f, NULL);
}

static enum codec_result image_system(struct c64_image *image, struct source *s)
{
    struct frame f;
    multicolour(&f, s, 0x402, 0x2402, 2, 0x2401);
    return render(image, s, &f, NULL);
}

static enum codec_result saracen(struct c64_image *image, struct source *s)
{
    struct frame f;
    multicolour(&f, s, 0x402, 2, 0x2402, 0x3f2);
    return render(image, s, &f, NULL);
}

/* Paint Magic: one colour RAM value for the whole picture. */
static enum codec_result paint_magic(struct c64_image *image, struct source *s)
{
    struct frame f;
    multicolour(&f, s, 0x74, 0x2074, 0, 0x1fb4);
    f.colour = NULL;
    f.fixed_colour = byte_at(s, 0x1fb7);
    return render(image, s, &f, NULL);
}

/* Micro Illustrator, uncompressed only. */
#define MICRO_ILLUSTRATOR_SIZE 10022u
static enum codec_result micro_illustrator(struct c64_image *image, struct source *s)
{
    struct frame f;
    enum codec_result r = sized(s, MICRO_ILLUSTRATOR_SIZE);

    if (r != CODEC_OK)
        return r;
    if (s->data[7] != 0)
        return CODEC_INVALID;
    multicolour(&f, s, 0x7e6, 0x16, 0x3fe, 8);
    return render(image, s, &f, NULL);
}

/* Drazpaint and Drazlace pack everything after the load address behind a
   signature: escape byte at 15, then escape, count, value from 16. An
   unpacked file must be exactly size bytes. u->data is allocated only when
   the file was packed. */
static enum codec_result draz_unpack(const struct source *s, const char *signature,
                                     size_t size, struct source *u)
{
    struct rle rle = { 0, 0, 0 };

    if (!signature_at(s->data, s->length, 2, signature)) {
        *u = *s;
        return sized(s, size);
    }
    if (s->length < 16)
        return CODEC_TRUNCATED;
    rle.escape = s->data[15];
    return unpack(s, 16, &rle, 2, size, u);
}

static enum codec_result draz_render(struct c64_image *image, const struct source *s,
                                     struct source *u, const struct frame *a,
                                     const struct frame *b)
{
    return u->data == s->data ? render(image, u, a, b) : render_unpacked(image, u, a, b);
}

#define DRAZPAINT_SIZE 10051u
static enum codec_result drazpaint(struct c64_image *image, struct source *s)
{
    struct source u;
    struct frame f;
    enum codec_result r = draz_unpack(s, "DRAZPAINT 1.4", DRAZPAINT_SIZE, &u);

    if (r != CODEC_OK)
        return r;
    multicolour(&f, &u, 0x802, 0x402, 2, 0x2742);
    return draz_render(image, s, &u, &f, NULL);
}

/* ---- FLI ---- */

/* FLI Graph and FLI Designer: black background. */
static enum codec_result fli_graph(struct c64_image *image, struct source *s)
{
    struct frame f;
    fli(&f, s, 0x2402, 0x402, 2);
    return render(image, s, &f, NULL);
}

/* Blackmail FLI: a background colour for each line. */
static enum codec_result blackmail_fli(struct c64_image *image, struct source *s)
{
    struct frame f;
    fli(&f, s, 0x2502, 0x502, 0x102);
    f.backgrounds = region(s, 2, C64_HEIGHT);
    return render(image, s, &f, NULL);
}

/* FLI Editor: Blackmail's layout with the line colours six bytes later. */
static enum codec_result fli_editor(struct c64_image *image, struct source *s)
{
    struct frame f;
    fli(&f, s, 0x2502, 0x502, 0x102);
    f.backgrounds = region(s, 8, C64_HEIGHT);
    return render(image, s, &f, NULL);
}

static enum codec_result flimatic(struct c64_image *image, struct source *s)
{
    struct frame f;
    fli(&f, s, 0x2402, 0x402, 2);
    f.background = byte_at(s, 0x4381);
    return render(image, s, &f, NULL);
}

/* ---- Interlace: two frames shown on alternate screen refreshes ---- */

#define DRAZLACE_SIZE 18242u
static enum codec_result drazlace(struct c64_image *image, struct source *s)
{
    struct source u;
    struct frame a, b;
    enum codec_result r = draz_unpack(s, "DRAZLACE! 1.0", DRAZLACE_SIZE, &u);

    if (r != CODEC_OK)
        return r;
    /* Both frames share colours; the second may be one pixel to the right. */
    multicolour(&a, &u, 0x802, 0x402, 2, 0x2742);
    multicolour(&b, &u, 0x2802, 0x402, 2, 0x2742);
    b.shift = byte_at(&u, 0x2744);
    if (b.shift > 1)
        u.bad = 1;
    return draz_render(image, s, &u, &a, &b);
}

static enum codec_result true_paint(struct c64_image *image, struct source *s)
{
    struct frame a, b;
    multicolour(&a, s, 0x402, 2, 0x4802, 0x3ea);
    multicolour(&b, s, 0x2402, 0x4402, 0x4802, 0x3ea);
    b.shift = 1;
    return render(image, s, &a, &b);
}

static enum codec_result fuckpaint(struct c64_image *image, struct source *s)
{
    struct frame a, b;
    multicolour(&a, s, 0xc02, 0x402, 2, 0x2b42);
    multicolour(&b, s, 0x2c02, 0x802, 2, 0x2b42);
    b.shift = 1;
    return render(image, s, &a, &b);
}

/* Interlaced FLI: two bitmaps and two sets of matrices, one colour RAM. */
static void ifli(struct frame *a, struct frame *b, struct source *s,
                 size_t bitmap1, size_t matrices1, size_t bitmap2, size_t matrices2,
                 size_t colour)
{
    fli(a, s, bitmap1, matrices1, colour);
    fli(b, s, bitmap2, matrices2, colour);
    b->shift = 1;
}

/* Gunpaint keeps line backgrounds in the gaps between its data: lines
   0-176, then 177-196, the last of which also colours lines 197-199. */
static enum codec_result gunpaint(struct c64_image *image, struct source *s)
{
    uint8_t lines[C64_HEIGHT];
    const uint8_t *top = region(s, 0x3f51, 177);
    const uint8_t *bottom = region(s, 0x47ea, 20);
    struct frame a, b;

    if (top == NULL || bottom == NULL)
        return CODEC_INVALID;
    memcpy(lines, top, 177);
    memcpy(lines + 177, bottom, 20);
    memset(lines + 197, bottom[19], 3);
    ifli(&a, &b, s, 0x2002, 2, 0x6402, 0x4402, 0x4002);
    a.backgrounds = b.backgrounds = lines;
    return render(image, s, &a, &b);
}

#define FUNPAINT_SIZE 33694u
static enum codec_result funpaint(struct c64_image *image, struct source *s)
{
    struct source u = *s;
    struct frame a, b;

    if (s->length < 18)
        return CODEC_TRUNCATED;
    if (!signature_at(s->data, s->length, 2, "FUNPAINT (MT) "))
        return CODEC_INVALID;
    if (s->data[16] != 0) {
        /* Packed after the 18-byte header: escape at 17, then escape, count, value. */
        struct rle rle = { 0, 0, 0 };
        enum codec_result r;
        rle.escape = s->data[17];
        r = unpack(s, 18, &rle, 18, FUNPAINT_SIZE, &u);
        if (r != CODEC_OK)
            return r;
    } else if (s->length != FUNPAINT_SIZE) {
        return s->length < FUNPAINT_SIZE ? CODEC_TRUNCATED : CODEC_INVALID;
    }
    ifli(&a, &b, &u, 0x2012, 0x12, 0x63fa, 0x43fa, 0x4012);
    return u.data == s->data ? render(image, &u, &a, &b) : render_unpacked(image, &u, &a, &b);
}

/* Flash FLI: one bitmap, two sets of matrices and line backgrounds. */
#define FLASH_FLI_SIZE 26115u
static enum codec_result flash_fli(struct c64_image *image, struct source *s)
{
    struct frame a, b;
    enum codec_result r = sized(s, FLASH_FLI_SIZE);

    if (r != CODEC_OK)
        return r;
    if (s->data[2] != 'f')
        return CODEC_INVALID;
    fli(&a, s, 0x2503, 0x503, 0x103);
    fli(&b, s, 0x2503, 0x4503, 0x103);
    a.backgrounds = region(s, 3, C64_HEIGHT);
    b.backgrounds = region(s, 0x6503, C64_HEIGHT);
    return render(image, s, &a, &b);
}

static enum codec_result hires_interlace(struct c64_image *image, struct source *s)
{
    struct frame a, b;
    hires(&a, s, 2, 0x2802);
    hires(&b, s, 0x4002, 0x2402);
    return render(image, s, &a, &b);
}

static enum codec_result hireslace(struct c64_image *image, struct source *s)
{
    struct frame a, b;
    hires(&a, s, 2, 0x2002);
    hires(&b, s, 0x6002, 0x4002);
    return render(image, s, &a, &b);
}

/* Interlace Hires Editor: two bitmaps in black and grey. */
static enum codec_result interlace_hires(struct c64_image *image, struct source *s)
{
    struct frame a, b;
    hires_fixed(&a, s, 2, 0x0c);
    hires_fixed(&b, s, 0x2002, 0x0c);
    return render(image, s, &a, &b);
}

/* Vertical Hires Interlace: two bitmaps sharing a video matrix. */
static enum codec_result vertical_hires(struct c64_image *image, struct source *s)
{
    struct frame a, b;
    hires(&a, s, 2, 0x4002);
    hires(&b, s, 0x2002, 0x4002);
    return render(image, s, &a, &b);
}

#define ECI_SIZE 32770u
static enum codec_result eci(struct c64_image *image, struct source *s)
{
    struct frame a, b;
    afli(&a, s, 2, 0x2002);
    afli(&b, s, 0x4002, 0x6002);
    return render(image, s, &a, &b);
}

/* ECI packed: escape at 2, then escape, count, value. */
static enum codec_result eci_packed(struct c64_image *image, struct source *s)
{
    struct source u;
    struct frame a, b;
    struct rle rle = { 0, 0, 0 };
    enum codec_result r;

    if (s->length < 3)
        return CODEC_TRUNCATED;
    rle.escape = s->data[2];
    r = unpack(s, 3, &rle, 2, ECI_SIZE, &u);
    if (r != CODEC_OK)
        return r;
    afli(&a, &u, 2, 0x2002);
    afli(&b, &u, 0x4002, 0x6002);
    return render_unpacked(image, &u, &a, &b);
}

/* ---- Recognition ---- */

enum { FIXED, SIGNATURE, EXT_ONLY };

struct format {
    const char *exts;       /* lower case, each followed by a space */
    unsigned kind;          /* FIXED: exact sizes; SIGNATURE: probe; EXT_ONLY: packed, no magic */
    uint32_t load;          /* required load address when found by content */
    uint16_t sizes[4];      /* FIXED: accepted lengths */
    int (*probe)(const uint8_t *data, size_t length);
    enum codec_result (*decode)(struct c64_image *image, struct source *s);
};

/* Found by content, a signature must come with its program's load address:
   the same bytes can open another format's bitmap. */
static int is_drazpaint(const uint8_t *d, size_t n)
{
    return (signature_at(d, n, 2, "DRAZPAINT 1.4") && load_address(d) == 0x5800) ||
           n == DRAZPAINT_SIZE;
}

static int is_drazlace(const uint8_t *d, size_t n)
{
    return (signature_at(d, n, 2, "DRAZLACE! 1.0") && load_address(d) == 0x5800) ||
           n == DRAZLACE_SIZE;
}

static int is_funpaint(const uint8_t *d, size_t n)
{
    return signature_at(d, n, 2, "FUNPAINT (MT) ") && load_address(d) == 0x3ff0;
}

static int is_flash_fli(const uint8_t *d, size_t n)
{
    return n == FLASH_FLI_SIZE && d[2] == 'f';
}

static int is_micro_illustrator(const uint8_t *d, size_t n)
{
    return n == MICRO_ILLUSTRATOR_SIZE && d[7] == 0;
}

static int is_eci_packed(const uint8_t *d, size_t n)
{
    (void)d;
    return n >= 4 && n < ECI_SIZE;
}

/* Order matters where content alone can't tell formats apart: the first
   match wins when the extension doesn't decide. */
static const struct format formats[] = {
    { "koa kla gig ipt lre rpm cwg fpt fcp ", FIXED, LOAD_ANY, { 10003, 10004, 10006, 10007 }, NULL, koala },
    { "koa ", FIXED, LOAD_ANY, { 10001 }, NULL, koala_bare },
    { "koa kla ", SIGNATURE, LOAD_ANY, { 0 }, is_koala_padded, koala_padded },
    { "gg ", EXT_ONLY, LOAD_ANY, { 0 }, NULL, koala_packed },
    { "ami ", EXT_ONLY, LOAD_ANY, { 0 }, NULL, amica },
    { "ocp mpi mpic ", FIXED, LOAD_ANY, { 10018 }, NULL, advanced_art_studio },
    { "drz drp ", SIGNATURE, LOAD_ANY, { 0 }, is_drazpaint, drazpaint },
    { "pi bpl ", FIXED, 0xa000, { 10242 }, NULL, blazing_paddles },
    { "a64 wig ", FIXED, 0x4000, { 10242 }, NULL, artist64 },
    { "rp ", FIXED, 0x5c00, { 10242 }, NULL, rainbow_painter },
    { "dol bed vid vic ", FIXED, 0x5800, { 10241, 10242, 10050 }, NULL, dolphin_ed },
    { "p64 fly ", FIXED, LOAD_ANY, { 10050 }, NULL, picasso64 },
    { "cdu ", FIXED, LOAD_ANY, { 10277 }, NULL, cdu_paint },
    { "che ", FIXED, LOAD_ANY, { 20482 }, NULL, cheese },
    { "ism ", FIXED, LOAD_ANY, { 10218 }, NULL, image_system },
    { "sar ", FIXED, LOAD_ANY, { 10219 }, NULL, saracen },
    { "pmg ", FIXED, LOAD_ANY, { 9332 }, NULL, paint_magic },
    { "mil ", SIGNATURE, LOAD_ANY, { 0 }, is_micro_illustrator, micro_illustrator },
    { "art aas iph hpi hpc gig hre ", FIXED, LOAD_ANY, { 9002, 9003, 9009 }, NULL, art_studio },
    { "dd ddp het rph ", FIXED, 0x5c00, { 9218, 9217, 9026, 9346 }, NULL, doodle },
    { "hed ish ", FIXED, 0x2000, { 9218, 9194 }, NULL, hi_eddi },
    { "jj dd ", EXT_ONLY, LOAD_ANY, { 0 }, NULL, doodle_packed },
    { "hbm hir fgs hpi gcd mon ", FIXED, LOAD_ANY, { 8002, 8194 }, NULL, hires_bitmap },
    { "rpo gih ", FIXED, LOAD_ANY, { 8002 }, NULL, run_paint_mono },
    { "afl ", FIXED, LOAD_ANY, { 16385 }, NULL, afli_editor },
    { "fli fd2 ", FIXED, LOAD_ANY, { 17409, 17218 }, NULL, fli_graph },
    { "fed ", FIXED, 0x3800, { 17665 }, NULL, fli_editor },
    { "bml flg fli vic ", FIXED, LOAD_ANY, { 17474, 17665, 17666 }, NULL, blackmail_fli },
    { "flm ", FIXED, LOAD_ANY, { 17410 }, NULL, flimatic },
    { "drl dlp ", SIGNATURE, LOAD_ANY, { 0 }, is_drazlace, drazlace },
    { "mci ", FIXED, LOAD_ANY, { 19434 }, NULL, true_paint },
    { "fp ", FIXED, LOAD_ANY, { 19266 }, NULL, fuckpaint },
    { "gun ifl ", FIXED, LOAD_ANY, { 33603, 33602 }, NULL, gunpaint },
    { "fun fp2 ", SIGNATURE, LOAD_ANY, { 0 }, is_funpaint, funpaint },
    { "ffl ffli fli ", SIGNATURE, LOAD_ANY, { 0 }, is_flash_fli, flash_fli },
    { "hlf hie ", FIXED, LOAD_ANY, { 24578 }, NULL, hires_interlace },
    { "eci ", FIXED, LOAD_ANY, { ECI_SIZE }, NULL, eci },
    { "hle ", FIXED, LOAD_ANY, { 32770 }, NULL, hireslace },
    { "ecp ", EXT_ONLY, LOAD_ANY, { 0 }, is_eci_packed, eci_packed },
    { "ihe ", FIXED, LOAD_ANY, { 16194 }, NULL, interlace_hires },
    { "vhi ", FIXED, LOAD_ANY, { 17389 }, NULL, vertical_hires },
};
#define FORMATS (sizeof formats / sizeof formats[0])

static int fits(const struct format *f, const uint8_t *data, size_t length)
{
    unsigned i;

    if (f->kind != FIXED)
        return f->probe == NULL || f->probe(data, length);
    for (i = 0; i < 4 && f->sizes[i] != 0; i++)
        if (length == f->sizes[i])
            return 1;
    return 0;
}

static size_t smallest(const struct format *f)
{
    size_t n = f->sizes[0];
    unsigned i;

    for (i = 1; i < 4 && f->sizes[i] != 0; i++)
        if (f->sizes[i] < n)
            n = f->sizes[i];
    return n;
}

/* The lower-case extension of name in ext, or "" when it has none or it
   is too long to be one of ours. */
static void extension(const char *name, char ext[8])
{
    const char *dot = name != NULL ? strrchr(name, '.') : NULL;
    size_t i;

    ext[0] = '\0';
    if (dot == NULL || strlen(dot + 1) > 5)
        return;
    for (i = 0; dot[1 + i] != '\0'; i++) {
        char c = dot[1 + i];
        ext[i] = (char)(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    }
    ext[i] = ' ';
    ext[i + 1] = '\0';
}

static int named(const struct format *f, const char *ext)
{
    const char *p = f->exts;
    size_t n = strlen(ext);

    if (n < 2)
        return 0;
    while ((p = strstr(p, ext)) != NULL) {
        if (p == f->exts || p[-1] == ' ')
            return 1;
        p++;
    }
    return 0;
}

enum codec_result c64_decode(const uint8_t *data, size_t length,
                             const char *name, struct c64_image *image)
{
    struct source s;
    char ext[8];
    unsigned i;
    int short_named = 0;

    if (image == NULL)
        return CODEC_INVALID;
    image->width = image->height = 0;
    image->rgba = NULL;
    if (data == NULL || length < 2)
        return CODEC_TRUNCATED;
    if (length > C64_MAX_FILE)
        return CODEC_INVALID;
    s.data = data;
    s.length = length;
    s.bad = 0;
    extension(name, ext);

    /* A format the extension names, if the content fits it. */
    for (i = 0; i < FORMATS; i++) {
        const struct format *f = &formats[i];
        if (!named(f, ext))
            continue;
        if (f->kind == FIXED) {
            if (fits(f, data, length))
                return f->decode(image, &s);
            if (length < smallest(f))
                short_named = 1;
        } else {
            /* Formats with signatures or packing judge the content
               themselves; if it isn't theirs, the content decides. */
            enum codec_result r = f->decode(image, &s);
            if (r != CODEC_INVALID)
                return r;
        }
    }
    /* Otherwise whatever the content says: signatures first, then lengths
       with the load addresses that separate formats of the same length,
       then lengths alone. */
    for (i = 0; i < FORMATS; i++)
        if (formats[i].kind == SIGNATURE && fits(&formats[i], data, length))
            return formats[i].decode(image, &s);
    for (i = 0; i < 2 * FORMATS; i++) {
        const struct format *f = &formats[i % FORMATS];
        if (f->kind != FIXED || !fits(f, data, length))
            continue;
        if (i < FORMATS && f->load != LOAD_ANY && load_address(data) != f->load)
            continue;
        return f->decode(image, &s);
    }
    return short_named ? CODEC_TRUNCATED : CODEC_INVALID;
}
