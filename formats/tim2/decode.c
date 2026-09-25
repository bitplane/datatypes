#include "decode.h"
#include <stdlib.h>
#include <string.h>

#define TIM2_MAX_PIXELS (16u * 1024u * 1024u)
#define TIM2_PICTURE_HEADER 48u
/* The mipmap header follows the picture header: two GS registers, then a
   32-bit size per level. */
#define TIM2_MIPMAP_SIZES (TIM2_PICTURE_HEADER + 16u)
/* The GS takes the base texture and at most six smaller levels. */
#define TIM2_MAX_LEVELS 7u
/* CLUT type: the entry format, a flag for 16-colour CLUTs stored in pairs in
   CSM1 order, and CSM2, whose entries are in index order. 256-colour CSM1
   CLUTs are always in CSM1 order. */
#define TIM2_CLUT_FORMAT 0x3fu
#define TIM2_CLUT_COMPOUND 0x40u
#define TIM2_CLUT_CSM2 0x80u

/* Where one picture's parts lie in the file. */
struct picture {
    unsigned levels, bits, clut_bits, csm1_order, width, height;
    const uint8_t *image, *level_sizes, *clut;
    size_t image_size, clut_entries;
    uint64_t size;
};

static uint32_t le16(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8);
}

static uint32_t le32(const uint8_t *p)
{
    return le16(p) | (le16(p + 2) << 16);
}

static unsigned level_width(const struct picture *pic, unsigned level)
{
    return pic->width >> level ? pic->width >> level : 1u;
}

static unsigned level_height(const struct picture *pic, unsigned level)
{
    return pic->height >> level ? pic->height >> level : 1u;
}

/* Pixels are one stream in GS transfer order; rows aren't padded. */
static size_t level_bytes(const struct picture *pic, unsigned level)
{
    return ((size_t)level_width(pic, level) * level_height(pic, level) *
            pic->bits + 7u) / 8u;
}

/* The space a level takes, including padding to align the next. */
static size_t level_size(const struct picture *pic, unsigned level)
{
    return pic->levels == 1 ? pic->image_size
                            : le32(pic->level_sizes + level * 4u);
}

/* Image types 1-5: 16, 24 and 32-bit direct colour, 4 and 8-bit indexed. */
static unsigned image_bits(unsigned type)
{
    static const unsigned bits[] = { 0, 16, 24, 32, 4, 8 };
    return type < sizeof bits / sizeof bits[0] ? bits[type] : 0;
}

/* A picture is its header, the image data (every level), then the CLUT.
   The part sizes place each one and lead to the next picture. The total
   size is ignored: real files hold garbage there. */
static enum codec_result parse(const uint8_t *p, size_t length,
                               struct picture *out)
{
    uint32_t clut_size, header_size, colours, type, span;
    size_t offset = 0, end = 0, size, need;
    uint64_t clut_at;
    unsigned i;

    if (length < TIM2_PICTURE_HEADER)
        return CODEC_TRUNCATED;
    clut_size = le32(p + 4);
    out->image_size = le32(p + 8);
    header_size = le16(p + 12);
    colours = le16(p + 14);
    /* 0 marks a CLUT-only picture, but PS3 games write it for one level. */
    out->levels = p[17] ? p[17] : 1u;
    type = p[18];
    out->bits = image_bits(p[19]);
    out->width = le16(p + 20);
    out->height = le16(p + 22);
    if (out->bits == 0 || out->levels > TIM2_MAX_LEVELS ||
        out->width == 0 || out->height == 0 || header_size < TIM2_PICTURE_HEADER ||
        (out->levels > 1 && header_size < TIM2_MIPMAP_SIZES + out->levels * 4u))
        return CODEC_INVALID;
    if ((size_t)out->width * out->height > TIM2_MAX_PIXELS)
        return CODEC_TOO_LARGE;
    if (header_size > length)
        return CODEC_TRUNCATED;
    out->level_sizes = p + TIM2_MIPMAP_SIZES;
    for (i = 0; i < out->levels; i++) {
        need = level_bytes(out, i);
        size = level_size(out, i);
        if (size < need || size > out->image_size - offset)
            return CODEC_INVALID;
        end = offset + need;
        offset += size;
    }
    /* Padding after the last level may be missing; its pixels may not. */
    if (end > length - header_size)
        return CODEC_TRUNCATED;
    out->image = p + header_size;
    out->size = (uint64_t)header_size + out->image_size + clut_size;
    out->clut_bits = 0;
    out->clut_entries = 0;
    out->csm1_order = 0;
    /* A direct-colour picture's CLUT is unused and skipped. */
    if (out->bits > 8)
        return CODEC_OK;
    switch (type & TIM2_CLUT_FORMAT) {
    case 0: return CODEC_OK;
    case 1: out->clut_bits = 16; break;
    case 2: out->clut_bits = 24; break;
    case 3: out->clut_bits = 32; break;
    default: return CODEC_INVALID;
    }
    if (!(type & TIM2_CLUT_CSM2))
        out->csm1_order = out->bits == 8 || (type & TIM2_CLUT_COMPOUND);
    /* Only the first palette is used. In CSM1 order a 16-colour palette is
       spread over the first 32 entries. */
    span = out->bits == 8 ? 256u : out->csm1_order ? 32u : 16u;
    out->clut_entries = colours < span ? colours : span;
    /* The colour count sizes the CLUT; some files give its byte size as 0. */
    clut_at = (uint64_t)header_size + out->image_size;
    if (clut_at + out->clut_entries * (out->clut_bits / 8u) > length)
        return CODEC_TRUNCATED;
    out->clut = p + (size_t)clut_at;
    return CODEC_OK;
}

static uint8_t five_to_eight(uint32_t v)
{
    /* The GS pads 5-bit channels with zeros, and so does ImageMagick. */
    return (uint8_t)((v & 31u) << 3);
}

/* The GS reads alpha 0x80 as fully opaque. */
static uint8_t alpha_to_eight(uint32_t v)
{
    return v >= 0x80u ? 255 : (uint8_t)(v * 2u);
}

static void put_colour(uint8_t *dst, const uint8_t *src, unsigned bits)
{
    uint32_t word;
    switch (bits) {
    case 16:
        word = le16(src);
        dst[0] = five_to_eight(word);
        dst[1] = five_to_eight(word >> 5);
        dst[2] = five_to_eight(word >> 10);
        dst[3] = word & 0x8000u ? 255 : 0;
        break;
    case 24:
        dst[0] = src[0];
        dst[1] = src[1];
        dst[2] = src[2];
        dst[3] = 255;
        break;
    default:
        dst[0] = src[0];
        dst[1] = src[1];
        dst[2] = src[2];
        dst[3] = alpha_to_eight(src[3]);
        break;
    }
}

/* CSM1 stores each run of 32 entries with the second and third groups of
   eight swapped, which is swapping bits 3 and 4 of the index. */
static unsigned csm1_entry(unsigned index)
{
    return index ^ ((((index >> 3) ^ (index >> 4)) & 1u) * 0x18u);
}

static void make_palette(const struct picture *pic, uint8_t palette[256][4])
{
    unsigned colours = pic->bits == 4 ? 16u : 256u, i, entry;
    for (i = 0; i < colours; i++) {
        entry = pic->csm1_order ? csm1_entry(i) : i;
        if (pic->clut_bits == 0) {
            uint8_t gray = (uint8_t)(i * 255u / (colours - 1u));
            palette[i][0] = palette[i][1] = palette[i][2] = gray;
            palette[i][3] = 255;
        } else if (entry < pic->clut_entries) {
            put_colour(palette[i], pic->clut + entry * (pic->clut_bits / 8u),
                       pic->clut_bits);
        } else {
            /* A CLUT shorter than the index range: missing entries are black. */
            palette[i][0] = palette[i][1] = palette[i][2] = 0;
            palette[i][3] = 255;
        }
    }
}

/* Check the file header; *pos is where the first picture starts. */
static enum codec_result file_header(const uint8_t *data, size_t length,
                                     size_t *pos, unsigned *pictures)
{
    if (data == NULL || length < 16)
        return CODEC_TRUNCATED;
    if (memcmp(data, "TIM2", 4) != 0)
        return CODEC_INVALID;
    /* The version isn't checked: games write private ones (0xc0 up are
       compatible), and the layout never changed. */
    /* Format 1 pads the header to 128 bytes to align the pictures. */
    if (data[5] > 1)
        return CODEC_INVALID;
    *pos = data[5] ? 128u : 16u;
    *pictures = le16(data + 6);
    if (length < *pos)
        return CODEC_TRUNCATED;
    return CODEC_OK;
}

void tim2_free(struct tim2_image *image)
{
    free(image->rgba);
    image->rgba = NULL;
    image->width = image->height = 0;
}

/* Where the picture after pic starts; the end of the file if it is cut short,
   so the next parse reports truncation. */
static size_t next_picture(size_t pos, size_t length, const struct picture *pic)
{
    return pic->size > length - pos ? length : pos + (size_t)pic->size;
}

unsigned tim2_count(const uint8_t *data, size_t length)
{
    struct picture pic;
    size_t pos;
    unsigned pictures, i, count = 0;

    if (file_header(data, length, &pos, &pictures) != CODEC_OK)
        return 0;
    for (i = 0; i < pictures; i++) {
        if (parse(data + pos, length - pos, &pic) != CODEC_OK)
            break;
        count += pic.levels;
        pos = next_picture(pos, length, &pic);
    }
    return count;
}

enum codec_result tim2_decode(const uint8_t *data, size_t length,
                              unsigned index, struct tim2_image *image)
{
    uint8_t palette[256][4];
    struct picture pic;
    enum codec_result result;
    const uint8_t *pixels;
    size_t pos, n, pixel_count;
    unsigned pictures, i, first = 0, level;

    if (image == NULL)
        return CODEC_INVALID;
    image->width = image->height = 0;
    image->rgba = NULL;
    result = file_header(data, length, &pos, &pictures);
    if (result != CODEC_OK)
        return result;
    for (i = 0;; i++) {
        if (i == pictures)
            return CODEC_INVALID;
        result = parse(data + pos, length - pos, &pic);
        /* Anything but a whole picture before the one asked for means it isn't there. */
        if (result != CODEC_OK)
            return i == 0 || first == index ? result : CODEC_INVALID;
        if (index - first < pic.levels)
            break;
        first += pic.levels;
        pos = next_picture(pos, length, &pic);
    }
    level = index - first;
    pixels = pic.image;
    for (i = 0; i < level; i++)
        pixels += level_size(&pic, i);
    image->width = level_width(&pic, level);
    image->height = level_height(&pic, level);
    pixel_count = (size_t)image->width * image->height;
    if (pic.bits <= 8)
        make_palette(&pic, palette);
    image->rgba = malloc(pixel_count * 4u);
    if (image->rgba == NULL) {
        image->width = image->height = 0;
        return CODEC_NO_MEMORY;
    }
    for (n = 0; n < pixel_count; n++) {
        uint8_t *dst = image->rgba + n * 4u;
        switch (pic.bits) {
        case 4: /* the low nibble comes first */
            memcpy(dst, palette[(pixels[n / 2u] >> (n & 1u ? 4 : 0)) & 15u], 4);
            break;
        case 8:
            memcpy(dst, palette[pixels[n]], 4);
            break;
        default:
            put_colour(dst, pixels + n * (pic.bits / 8u), pic.bits);
            break;
        }
    }
    return CODEC_OK;
}
