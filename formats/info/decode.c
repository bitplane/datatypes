#include "decode.h"

#include <stdlib.h>
#include <string.h>

#include "common/zlib.h"

#define DISKOBJECT_SIZE 78u
#define DRAWERDATA_SIZE 56u
#define DRAWERDATA2_SIZE 6u
#define IMAGE_SIZE 20u
#define GADGIMAGE 0x0004u
#define GADGHIGHBITS 0x0003u
#define GADGHIMAGE 0x0002u
#define IMAG_HEADER 10u
#define IMAG_TRANSPARENT 0x01u
#define IMAG_PALETTE 0x02u
#define ARGB_HEADER 10u

static const char newicon_marker[] = "*** DON'T EDIT THE FOLLOWING LINES!! ***";

/* Workbench's pens for planar images, which carry no palette.
   OS 1.x as netpbm's infotopam shows it; OS 2 and later; MagicWB's eight;
   and the rest of AROS's default screen palette, from Intuition's coltab. */
static const uint8_t palette_13[4][3] = {
    {0x00, 0x55, 0xaa}, {0xff, 0xff, 0xff}, {0x00, 0x00, 0x20}, {0xff, 0x8a, 0x00}
};
static const uint8_t palette_20[4][3] = {
    {0xaa, 0xaa, 0xaa}, {0x00, 0x00, 0x00}, {0xff, 0xff, 0xff}, {0x66, 0x88, 0xbb}
};
static const uint8_t palette_mwb[8][3] = {
    {0x95, 0x95, 0x95}, {0x00, 0x00, 0x00}, {0xff, 0xff, 0xff}, {0x3b, 0x67, 0xa2},
    {0x7b, 0x7b, 0x7b}, {0xaf, 0xaf, 0xaf}, {0xaa, 0x90, 0x7c}, {0xff, 0xa9, 0x97}
};
static const uint8_t aros_last4[4][3] = {
    {0xee, 0x44, 0x44}, {0x55, 0xdd, 0x55}, {0x00, 0x44, 0xdd}, {0xee, 0x99, 0x00}
};
static const uint8_t aros_pointer[3][3] = {
    {0xbb, 0x00, 0x00}, {0xdd, 0x00, 0x00}, {0xee, 0x00, 0x00}
};

static unsigned be16(const uint8_t *p) { return (unsigned)p[0] << 8 | p[1]; }
static uint32_t be32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

/* ---- file structure ---- */

struct cursor { const uint8_t *data; size_t length, at; };

static enum codec_result skip(struct cursor *c, uint64_t n)
{
    if (n > c->length - c->at)
        return CODEC_TRUNCATED;
    c->at += (size_t)n;
    return CODEC_OK;
}

/* A string: a 32-bit length that counts the NUL, then the bytes. */
static enum codec_result skip_string(struct cursor *c)
{
    if (c->length - c->at < 4)
        return CODEC_TRUNCATED;
    c->at += 4;
    return skip(c, be32(c->data + c->at - 4));
}

static void add(struct info_icon *icon, const struct info_entry *entry)
{
    if (icon->count < INFO_MAX_ENTRIES)
        icon->entries[icon->count++] = *entry;
}

/* The pens a planar image can reach: its picked planes and the planes it sets. */
static unsigned planar_pens(unsigned depth, unsigned pick, unsigned on_off)
{
    unsigned mask = 0, plane = 0, bit;
    for (bit = 0; bit < 8; bit++) {
        if (pick & 1u << bit) {
            if (plane++ < depth)
                mask |= 1u << bit;
        } else {
            mask |= on_off & 1u << bit;
        }
    }
    return mask;
}

static enum codec_result read_planar(struct cursor *c, int selected, unsigned revision,
                                     struct info_icon *icon)
{
    struct info_entry entry;
    const uint8_t *h;
    int width, height, depth;
    uint64_t size;

    if (c->length - c->at < IMAGE_SIZE)
        return CODEC_TRUNCATED;
    h = c->data + c->at;
    width = (int16_t)be16(h + 4);
    height = (int16_t)be16(h + 6);
    depth = (int16_t)be16(h + 8);
    if (width < 0 || height < 0 || depth < 0)
        return CODEC_INVALID;
    c->at += IMAGE_SIZE;
    size = (uint64_t)((width + 15) >> 4) * 2u * (unsigned)height * (unsigned)depth;
    if (size > c->length - c->at)
        return CODEC_TRUNCATED;
    memset(&entry, 0, sizeof entry);
    entry.kind = INFO_PLANAR;
    entry.selected = selected;
    entry.width = (unsigned)width;
    entry.height = (unsigned)height;
    entry.offset = c->at;
    entry.size = (size_t)size;
    entry.depth = (unsigned)depth;
    entry.pick = h[14];
    entry.on_off = h[15];
    entry.revision = revision;
    c->at += (size_t)size;
    /* Empty images show nothing. */
    if (width > 0 && height > 0 && depth > 0)
        add(icon, &entry);
    return CODEC_OK;
}

/* The tooltype string at an offset, as a pointer and length up to its NUL. */
static void tooltype(const uint8_t *data, size_t at, const uint8_t **text, size_t *length)
{
    size_t n = be32(data + at);
    const uint8_t *nul;
    *text = data + at + 4;
    nul = n != 0 ? memchr(*text, 0, n) : NULL;
    *length = nul != NULL ? (size_t)(nul - *text) : n;
}

static int starts(const uint8_t *text, size_t length, const char *prefix)
{
    size_t n = strlen(prefix);
    return length >= n && memcmp(text, prefix, n) == 0;
}

/* NewIcons keep their images in tooltypes after the marker line: IM1= for
   the normal image and IM2= for the selected one. */
static enum codec_result read_newicons(const uint8_t *data, size_t first, unsigned count,
                                       struct info_icon *icon)
{
    const uint8_t *text;
    size_t at = first, length, found[2] = {0, 0};
    unsigned i, which;
    int marked = 0;

    for (i = 0; i < count; i++, at += 4 + be32(data + at)) {
        tooltype(data, at, &text, &length);
        if (!marked) {
            marked = length == sizeof newicon_marker - 1 &&
                     memcmp(text, newicon_marker, length) == 0;
            continue;
        }
        for (which = 0; which < 2; which++)
            if (found[which] == 0 && starts(text, length, which ? "IM2=" : "IM1="))
                found[which] = at;
    }
    for (which = 0; which < 2; which++) {
        struct info_entry entry;
        if (found[which] == 0)
            continue;
        tooltype(data, found[which], &text, &length);
        if (length < 9 || text[5] < 0x22 || text[6] < 0x22 ||
            text[7] < 0x21 || text[8] < 0x21 || (text[4] != 'B' && text[4] != 'C'))
            return CODEC_INVALID;
        memset(&entry, 0, sizeof entry);
        entry.kind = INFO_NEWICON;
        entry.selected = (int)which;
        entry.width = text[5] - 0x21u;
        entry.height = text[6] - 0x21u;
        entry.offset = found[which];
        entry.size = at - found[which];
        entry.colours = ((text[7] - 0x21u) << 6) + (text[8] - 0x21u);
        if (entry.colours == 0 || entry.colours > 256)
            return CODEC_INVALID;
        add(icon, &entry);
    }
    /* Keep file order when IM2 lines come first. */
    if (icon->count >= 2) {
        struct info_entry *a = &icon->entries[icon->count - 2];
        struct info_entry *b = &icon->entries[icon->count - 1];
        if (a->kind == INFO_NEWICON && b->kind == INFO_NEWICON && b->offset < a->offset) {
            struct info_entry t = *a;
            *a = *b;
            *b = t;
        }
    }
    return CODEC_OK;
}

/* The OS 3.5 FORM ICON: a FACE chunk sizes the images, IMAG chunks hold
   palette images and ARGB chunks hold zlib-packed 32-bit images. */
static enum codec_result read_form(const uint8_t *data, size_t at, size_t end,
                                   struct info_icon *icon)
{
    unsigned width = 0, height = 0, imags = 0, argbs = 0;
    size_t first_palette = 0;
    int have_face = 0, have_palette = 0;

    while (at < end) {
        const uint8_t *body;
        uint32_t n;
        struct info_entry entry;

        if (end - at < 8)
            return CODEC_INVALID;
        n = be32(data + at + 4);
        if (n > end - at - 8)
            return CODEC_INVALID;
        body = data + at + 8;
        memset(&entry, 0, sizeof entry);
        entry.offset = at + 8;
        entry.size = n;
        entry.width = width;
        entry.height = height;
        if (memcmp(data + at, "FACE", 4) == 0 && n >= 6) {
            width = body[0] + 1u;
            height = body[1] + 1u;
            have_face = 1;
        } else if (memcmp(data + at, "IMAG", 4) == 0 && have_face && imags < 2) {
            unsigned flags, format, palette_format, bits, colours;
            size_t image_bytes, palette_bytes;

            if (n < IMAG_HEADER)
                return CODEC_INVALID;
            colours = body[1] + 1u;
            flags = body[2];
            format = body[3];
            palette_format = body[4];
            bits = body[5];
            image_bytes = be16(body + 6) + 1u;
            palette_bytes = flags & IMAG_PALETTE ? be16(body + 8) + 1u : 0;
            if (format > 1 || palette_format > 1 || (format == 1 && (bits < 1 || bits > 8)) ||
                IMAG_HEADER + image_bytes + palette_bytes > n ||
                (format == 0 && image_bytes < (size_t)width * height) ||
                (palette_format == 0 && palette_bytes != 0 && palette_bytes < colours * 3u))
                return CODEC_INVALID;
            entry.kind = INFO_IMAG;
            entry.selected = imags++ != 0;
            if (flags & IMAG_PALETTE) {
                entry.palette = entry.offset;
                if (!have_palette)
                    first_palette = entry.offset;
                have_palette = 1;
            } else if (entry.selected && have_palette) {
                entry.palette = first_palette;
            } else {
                return CODEC_INVALID;
            }
            add(icon, &entry);
        } else if (memcmp(data + at, "ARGB", 4) == 0 && have_face && argbs < 2) {
            uint32_t packed;
            if (n < ARGB_HEADER)
                return CODEC_INVALID;
            packed = be32(body + 4);
            if (packed == 0 || packed > n - ARGB_HEADER)
                return CODEC_INVALID;
            entry.kind = INFO_ARGB;
            entry.selected = argbs++ != 0;
            add(icon, &entry);
        }
        /* Chunks are padded to even sizes; the last one may end at the FORM's end. */
        at += 8 + (size_t)n;
        if ((n & 1) && at < end)
            at++;
    }
    return CODEC_OK;
}

/* Whether a FORM ICON, or the start of one cut short, is at this offset. */
static int form_at(const uint8_t *data, size_t length, size_t at)
{
    size_t n = length - at;
    if (n == 0 || memcmp(data + at, "FORM", n < 4 ? n : 4) != 0)
        return 0;
    return n < 12 || memcmp(data + at + 8, "ICON", 4) == 0;
}

enum codec_result info_parse(const uint8_t *data, size_t length, struct info_icon *icon)
{
    struct cursor c;
    const uint8_t *h = data;
    unsigned flags, revision, count = 0;
    size_t tooltypes = 0;
    uint32_t drawer;
    enum codec_result result;

    memset(icon, 0, sizeof *icon);
    if (length < DISKOBJECT_SIZE)
        return length >= 2 && (data[0] != 0xe3 || data[1] != 0x10) ?
               CODEC_INVALID : CODEC_TRUNCATED;
    if (be16(h) != 0xe310)
        return CODEC_INVALID;
    c.data = data;
    c.length = length;
    c.at = DISKOBJECT_SIZE;
    flags = be16(h + 16);
    revision = be32(h + 44) & 0xffu;
    drawer = be32(h + 66);
    if (drawer != 0 && (result = skip(&c, DRAWERDATA_SIZE)) != CODEC_OK)
        return result;
    if ((be32(h + 22) != 0 || (flags & GADGIMAGE)) &&
        (result = read_planar(&c, 0, revision, icon)) != CODEC_OK)
        return result;
    if ((be32(h + 26) != 0 || (flags & GADGHIGHBITS) == GADGHIMAGE) &&
        (result = read_planar(&c, 1, revision, icon)) != CODEC_OK)
        return result;
    if (be32(h + 50) != 0 && (result = skip_string(&c)) != CODEC_OK)
        return result;
    if (be32(h + 54) != 0) {
        uint32_t size, i;
        if (length - c.at < 4)
            return CODEC_TRUNCATED;
        size = be32(data + c.at);
        c.at += 4;
        count = size >= 4 ? size / 4 - 1 : 0;
        tooltypes = c.at;
        for (i = 0; i < count; i++)
            if ((result = skip_string(&c)) != CODEC_OK)
                return result;
    }
    if (be32(h + 70) != 0 && (result = skip_string(&c)) != CODEC_OK)
        return result;
    if (drawer != 0 && revision == 1 && (result = skip(&c, DRAWERDATA2_SIZE)) != CODEC_OK)
        return result;
    if (count != 0 && (result = read_newicons(data, tooltypes, count, icon)) != CODEC_OK)
        return result;
    /* Some revision 0 drawers carry DrawerData2 too. */
    if (drawer != 0 && revision != 1 && !form_at(data, length, c.at) &&
        length - c.at > DRAWERDATA2_SIZE && form_at(data, length, c.at + DRAWERDATA2_SIZE))
        c.at += DRAWERDATA2_SIZE;
    /* Anything after the icon other than a FORM ICON is ignored. */
    if (form_at(data, length, c.at)) {
        uint32_t size;
        if (length - c.at < 12)
            return CODEC_TRUNCATED;
        size = be32(data + c.at + 4);
        if (size < 4)
            return CODEC_INVALID;
        if (size > length - c.at - 8)
            return CODEC_TRUNCATED;
        if ((result = read_form(data, c.at + 12, c.at + 8 + size, icon)) != CODEC_OK)
            return result;
    }
    return icon->count != 0 ? CODEC_OK : CODEC_INVALID;
}

unsigned info_best(const struct info_icon *icon)
{
    unsigned i, best = 0;
    for (i = 1; i < icon->count; i++) {
        const struct info_entry *a = &icon->entries[i], *b = &icon->entries[best];
        if (a->kind > b->kind || (a->kind == b->kind && b->selected && !a->selected))
            best = i;
    }
    return best;
}

/* ---- images ---- */

static enum codec_result alloc_image(struct info_image *image, unsigned width, unsigned height)
{
    if (width == 0 || height == 0)
        return CODEC_INVALID;
    if (width > INFO_MAX_SIDE || height > INFO_MAX_SIDE ||
        (uint64_t)width * height > INFO_MAX_PIXELS)
        return CODEC_TOO_LARGE;
    image->rgba = malloc((size_t)width * height * 4u);
    if (image->rgba == NULL)
        return CODEC_NO_MEMORY;
    image->width = width;
    image->height = height;
    return CODEC_OK;
}

static void put(uint8_t *p, const uint8_t *rgb, uint8_t alpha)
{
    p[0] = rgb[0];
    p[1] = rgb[1];
    p[2] = rgb[2];
    p[3] = alpha;
}

/* Images reaching pens 4-7 only were drawn for MagicWB's eight colours.
   Deeper ones get AROS's default screen of their depth: the first four pens,
   the last four, the pointer's pens 17-19, and black elsewhere. */
static void planar_palette(const struct info_entry *e, uint8_t palette[256][3])
{
    unsigned pens = planar_pens(e->depth, e->pick, e->on_off), depth = 0;

    memset(palette, 0, 256 * 3);
    while (pens >> depth)
        depth++;
    if (depth == 3) {
        memcpy(palette, palette_mwb, sizeof palette_mwb);
        return;
    }
    memcpy(palette, e->revision == 0 ? palette_13 : palette_20, sizeof palette_20);
    if (depth >= 4)
        memcpy(palette[(1u << depth) - 4], aros_last4, sizeof aros_last4);
    if (depth >= 5)
        memcpy(palette[17], aros_pointer, sizeof aros_pointer);
}

static enum codec_result decode_planar(const uint8_t *data, const struct info_entry *e,
                                       struct info_image *image)
{
    uint8_t palette[256][3];
    size_t row = (size_t)((e->width + 15) >> 4) * 2u, plane = row * e->height;
    unsigned x, y, bit, p;
    enum codec_result result;

    if ((result = alloc_image(image, e->width, e->height)) != CODEC_OK)
        return result;
    planar_palette(e, palette);
    for (y = 0; y < e->height; y++) {
        for (x = 0; x < e->width; x++) {
            unsigned pen = 0;
            for (bit = 0, p = 0; bit < 8; bit++) {
                if (e->pick & 1u << bit) {
                    if (p < e->depth &&
                        data[e->offset + p * plane + y * row + x / 8] & 0x80u >> (x & 7))
                        pen |= 1u << bit;
                    p++;
                } else {
                    pen |= e->on_off & 1u << bit;
                }
            }
            put(image->rgba + ((size_t)y * e->width + x) * 4u, palette[pen], 0xff);
        }
    }
    return CODEC_OK;
}

/* NewIcons pack bits seven to a character: 0x20-0x9f hold 0x00-0x7f,
   0xa0-0xd0 hold 0x4f-0x7f, and 0xd1-0xff stand for 1-47 groups of zero bits.
   Bits left at the end of a line are dropped. */
struct newicon_reader {
    const uint8_t *data;
    size_t at, end;          /* the next tooltype, and the end of the list */
    const uint8_t *text;
    size_t left;             /* characters left in the current line */
    char tag[5];
    uint32_t bits;
    unsigned have, zeros;
};

static int next_line(struct newicon_reader *r)
{
    const uint8_t *text;
    size_t length;
    if (r->at >= r->end)
        return 0;
    tooltype(r->data, r->at, &text, &length);
    r->at += 4 + be32(r->data + r->at);
    if (!starts(text, length, r->tag))
        return 0;
    r->text = text + 4;
    r->left = length - 4;
    r->have = 0;
    return 1;
}

static enum codec_result newicon_read(struct newicon_reader *r, unsigned bits, uint8_t *out,
                                      size_t count)
{
    size_t i;
    for (i = 0; i < count; i++) {
        while (r->have < bits) {
            unsigned c, value;
            if (r->zeros != 0) {
                r->zeros--;
                value = 0;
            } else {
                if (r->left == 0 && !next_line(r))
                    return CODEC_INVALID;
                c = *r->text++;
                r->left--;
                if (c < 0x20)
                    return CODEC_INVALID;
                if (c < 0xa0) {
                    value = c - 0x20;
                } else if (c < 0xd1) {
                    value = c - 0x51;
                } else {
                    r->zeros = c - 0xd1;
                    value = 0;
                }
            }
            r->bits = (r->bits << 7 | value) & 0x7fffu;
            r->have += 7;
        }
        r->have -= bits;
        out[i] = (uint8_t)(r->bits >> r->have & ((1u << bits) - 1));
    }
    return CODEC_OK;
}

static enum codec_result decode_newicon(const uint8_t *data, const struct info_entry *e,
                                        struct info_image *image)
{
    struct newicon_reader r;
    uint8_t palette[256][3], *pens;
    unsigned bits = 1, colours = e->colours;
    size_t i, count;
    int transparent;
    enum codec_result result;

    memset(&r, 0, sizeof r);
    r.data = data;
    r.at = e->offset;
    r.end = e->offset + e->size;
    memcpy(r.tag, e->selected ? "IM2=" : "IM1=", 5);
    if (!next_line(&r))
        return CODEC_INVALID;
    transparent = r.text[0] == 'B';
    /* The palette follows the five header characters, and the pixels
       start on the line after the palette ends. */
    r.text += 5;
    r.left -= 5;
    if ((result = newicon_read(&r, 8, &palette[0][0], colours * 3u)) != CODEC_OK)
        return result;
    r.left = 0;
    r.have = 0;
    r.zeros = 0;
    while ((1u << bits) < colours)
        bits++;
    if ((result = alloc_image(image, e->width, e->height)) != CODEC_OK)
        return result;
    count = (size_t)e->width * e->height;
    pens = image->rgba + count * 3u;
    if ((result = newicon_read(&r, bits, pens, count)) != CODEC_OK) {
        info_free(image);
        return result;
    }
    /* Pens past the palette show black. */
    memset(palette[colours], 0, sizeof palette - colours * 3u);
    for (i = 0; i < count; i++)
        put(image->rgba + i * 4u, palette[pens[i]], transparent && pens[i] == 0 ? 0 : 0xff);
    return CODEC_OK;
}

/* OS 3.5 run-length data: in one bit stream, an 8-bit control n followed by
   n+1 literal values (n < 128) or one value repeated 257-n times (n > 128). */
static enum codec_result unpack35(const uint8_t *src, size_t size, unsigned bits,
                                  uint8_t *out, size_t count)
{
    size_t i = 0, bit = 0, total = size * 8u;

    while (i < count) {
        unsigned control, run, literal, k;
        if (total - bit < 8)
            return CODEC_INVALID;
        for (control = 0, k = 0; k < 8; k++, bit++)
            control = control << 1 | (src[bit / 8] >> (7 - bit % 8) & 1u);
        if (control == 128)
            continue;
        literal = control < 128;
        run = literal ? control + 1 : 257 - control;
        for (k = 0; k < run && i < count; k++) {
            unsigned value = 0, b;
            if (literal || k == 0) {
                if (total - bit < bits)
                    return CODEC_INVALID;
                for (b = 0; b < bits; b++, bit++)
                    value = value << 1 | (src[bit / 8] >> (7 - bit % 8) & 1u);
            } else {
                value = out[i - 1];
            }
            out[i++] = (uint8_t)value;
        }
    }
    return CODEC_OK;
}

static enum codec_result decode_imag(const uint8_t *data, const struct info_entry *e,
                                     struct info_image *image)
{
    const uint8_t *body = data + e->offset, *own = data + e->palette;
    uint8_t palette[256][3], *pens;
    unsigned colours = own[1] + 1u, transparent = body[0], flags = body[2];
    size_t i, count = (size_t)e->width * e->height;
    size_t image_bytes = be16(body + 6) + 1u, palette_bytes = be16(own + 8) + 1u;
    size_t own_image_bytes = be16(own + 6) + 1u;
    enum codec_result result;

    memset(palette, 0, sizeof palette);
    if (own[4] == 0)
        memcpy(palette, own + IMAG_HEADER + own_image_bytes, colours * 3u);
    else if ((result = unpack35(own + IMAG_HEADER + own_image_bytes, palette_bytes, 8,
                                &palette[0][0], colours * 3u)) != CODEC_OK)
        return result;
    if ((result = alloc_image(image, e->width, e->height)) != CODEC_OK)
        return result;
    pens = image->rgba + count * 3u;
    if (body[3] == 0)
        memcpy(pens, body + IMAG_HEADER, count);
    else if ((result = unpack35(body + IMAG_HEADER, image_bytes, body[5], pens, count)) != CODEC_OK) {
        info_free(image);
        return result;
    }
    /* Pens past the palette show black, as the zeroed palette makes them. */
    for (i = 0; i < count; i++)
        put(image->rgba + i * 4u, palette[pens[i]],
            (flags & IMAG_TRANSPARENT) && pens[i] == transparent ? 0 : 0xff);
    return CODEC_OK;
}

static enum codec_result decode_argb(const uint8_t *data, const struct info_entry *e,
                                     struct info_image *image)
{
    const uint8_t *body = data + e->offset;
    size_t i, count = (size_t)e->width * e->height, written;
    enum codec_result result;

    if ((result = alloc_image(image, e->width, e->height)) != CODEC_OK)
        return result;
    /* AROS stores the packed size; OS4 and MorphOS store it less one, as
       OS 3.5 does its other sizes. The stream's own end settles it. */
    result = zlib_inflate(body + ARGB_HEADER, e->size - ARGB_HEADER, image->rgba, count * 4u,
                          &written);
    if (result == CODEC_OK && written != count * 4u)
        result = CODEC_INVALID;
    if (result != CODEC_OK) {
        info_free(image);
        return result == CODEC_NO_MEMORY ? result : CODEC_INVALID;
    }
    for (i = 0; i < count; i++) {
        uint8_t *p = image->rgba + i * 4u, a = p[0];
        p[0] = p[1];
        p[1] = p[2];
        p[2] = p[3];
        p[3] = a;
    }
    return CODEC_OK;
}

enum codec_result info_decode(const uint8_t *data, size_t length, const struct info_icon *icon,
                              unsigned index, struct info_image *image)
{
    const struct info_entry *e;

    (void)length;
    memset(image, 0, sizeof *image);
    if (index >= icon->count)
        return CODEC_INVALID;
    e = &icon->entries[index];
    switch (e->kind) {
    case INFO_PLANAR:
        return decode_planar(data, e, image);
    case INFO_NEWICON:
        return decode_newicon(data, e, image);
    case INFO_IMAG:
        return decode_imag(data, e, image);
    default:
        return decode_argb(data, e, image);
    }
}

void info_free(struct info_image *image)
{
    free(image->rgba);
    memset(image, 0, sizeof *image);
}
