#include <stdlib.h>
#include <string.h>

#include "common/bcn.h"
#include "common/bptc.h"
#include "decode.h"

#define HEADER_SIZE 128u
#define DX10_SIZE 20u
#define PALETTE_SIZE 1024u
#define MAX_SIDE 65535u
#define MAX_PIXELS (16ul * 1024ul * 1024ul)

/* Pixel format flags. */
#define PF_ALPHAPIXELS 0x1u
#define PF_ALPHA 0x2u
#define PF_FOURCC 0x4u
#define PF_PALETTE8 0x20u
#define PF_RGB 0x40u
#define PF_LUMINANCE 0x20000u
/* caps2 */
#define CAPS2_CUBEMAP 0x200u
#define CAPS2_FACES 0xfc00u
#define CAPS2_VOLUME 0x200000u
/* DX10 header */
#define DIMENSION_3D 4u
#define MISC_CUBE 0x4u
#define ALPHA_MODE_PREMULTIPLIED 2u
#define ALPHA_MODE_OPAQUE 3u

/* Linear values at which the 8-bit sRGB encoding steps up: srgb_cut[i]
   is where 255 times the sRGB curve reaches i + 0.5. */
static const float srgb_cut[255] = {
    0.000151763492f, 0.000455290475f, 0.000758817459f, 0.00106234444f,
    0.00136587143f, 0.00166939841f, 0.00197292539f, 0.00227645238f,
    0.00257997936f, 0.00288350634f, 0.0031883009f, 0.00350925935f,
    0.00384831493f, 0.00420574803f, 0.00458183274f, 0.00497683725f,
    0.00539102416f, 0.00582465078f, 0.00627796943f, 0.00675122763f,
    0.00724466842f, 0.0077585305f, 0.00829304845f, 0.00884845295f,
    0.00942497089f, 0.0100228256f, 0.0106422369f, 0.0112834213f,
    0.0119465921f, 0.0126319598f, 0.0133397316f, 0.014070112f,
    0.0148233028f, 0.0155995031f, 0.0163989095f, 0.0172217161f,
    0.0180681146f, 0.0189382945f, 0.0198324428f, 0.0207507446f,
    0.0216933829f, 0.0226605384f, 0.0236523902f, 0.024669115f,
    0.0257108881f, 0.0267778826f, 0.0278702702f, 0.0289882206f,
    0.0301319019f, 0.0313014806f, 0.0324971216f, 0.0337189882f,
    0.0349672424f, 0.0362420443f, 0.037543553f, 0.0388719259f,
    0.0402273192f, 0.0416098877f, 0.0430197848f, 0.0444571628f,
    0.0459221727f, 0.047414964f, 0.0489356854f, 0.0504844842f,
    0.0520615066f, 0.0536668976f, 0.0553008013f, 0.0569633604f,
    0.0586547169f, 0.0603750115f, 0.0621243839f, 0.0639029729f,
    0.0657109163f, 0.0675483509f, 0.0694154125f, 0.0713122362f,
    0.0732389559f, 0.0751957047f, 0.077182615f, 0.0791998181f,
    0.0812474446f, 0.0833256241f, 0.0854344855f, 0.087574157f,
    0.0897447658f, 0.0919464383f, 0.0941793004f, 0.096443477f,
    0.0987390924f, 0.10106627f, 0.103425133f, 0.105815802f,
    0.108238401f, 0.110693048f, 0.113179865f, 0.11569897f,
    0.118250482f, 0.12083452f, 0.1234512f, 0.12610064f,
    0.128782955f, 0.131498261f, 0.134246673f, 0.137028306f,
    0.139843272f, 0.142691686f, 0.14557366f, 0.148489305f,
    0.151438734f, 0.154422057f, 0.157439385f, 0.160490827f,
    0.163576493f, 0.166696492f, 0.169850932f, 0.17303992f,
    0.176263564f, 0.179521971f, 0.182815248f, 0.186143498f,
    0.189506829f, 0.192905345f, 0.196339151f, 0.19980835f,
    0.203313045f, 0.20685334f, 0.210429338f, 0.21404114f,
    0.217688849f, 0.221372565f, 0.225092389f, 0.228848422f,
    0.232640764f, 0.236469515f, 0.240334772f, 0.244236636f,
    0.248175205f, 0.252150577f, 0.256162849f, 0.260212118f,
    0.264298482f, 0.268422037f, 0.272582879f, 0.276781103f,
    0.281016805f, 0.285290081f, 0.289601024f, 0.293949728f,
    0.298336289f, 0.302760799f, 0.307223352f, 0.31172404f,
    0.316262956f, 0.320840192f, 0.325455841f, 0.330109993f,
    0.33480274f, 0.339534173f, 0.344304382f, 0.349113458f,
    0.353961491f, 0.35884857f, 0.363774785f, 0.368740224f,
    0.373744977f, 0.378789131f, 0.383872775f, 0.388995998f,
    0.394158885f, 0.399361525f, 0.404604005f, 0.409886411f,
    0.41520883f, 0.420571347f, 0.42597405f, 0.431417022f,
    0.43690035f, 0.442424119f, 0.447988412f, 0.453593316f,
    0.459238914f, 0.46492529f, 0.470652528f, 0.476420711f,
    0.482229923f, 0.488080246f, 0.493971763f, 0.499904557f,
    0.505878709f, 0.511894303f, 0.517951419f, 0.524050139f,
    0.530190544f, 0.536372716f, 0.542596734f, 0.54886268f,
    0.555170635f, 0.561520677f, 0.567912887f, 0.574347344f,
    0.580824128f, 0.587343319f, 0.593904994f, 0.600509233f,
    0.607156115f, 0.613845717f, 0.620578117f, 0.627353395f,
    0.634171626f, 0.641032889f, 0.647937261f, 0.654884819f,
    0.66187564f, 0.668909801f, 0.675987377f, 0.683108445f,
    0.690273081f, 0.697481362f, 0.704733362f, 0.712029156f,
    0.719368822f, 0.726752432f, 0.734180063f, 0.741651788f,
    0.749167683f, 0.756727821f, 0.764332277f, 0.771981125f,
    0.779674438f, 0.787412289f, 0.795194753f, 0.803021903f,
    0.810893811f, 0.81881055f, 0.826772194f, 0.834778813f,
    0.842830482f, 0.850927271f, 0.859069253f, 0.867256499f,
    0.875489082f, 0.883767073f, 0.892090542f, 0.900459561f,
    0.908874202f, 0.917334534f, 0.925840628f, 0.934392556f,
    0.942990386f, 0.95163419f, 0.960324036f, 0.969059996f,
    0.977842139f, 0.986670534f, 0.99554525f
};

enum kind { BC1, BC2, BC3, BC4, BC5, BC5S, BC6H, BC6HS, BC7, MASKED, PALETTE };
enum alpha { OPAQUE, STRAIGHT, PREMULTIPLIED };

struct layout {
    enum kind kind;
    enum alpha alpha;
    unsigned pixel_bytes;       /* MASKED */
    uint32_t mask[4];           /* MASKED: red, green, blue, alpha */
    int gray;                   /* MASKED: mask[0] is luminance */
    int white;                  /* MASKED: alpha only, on white */
    const uint8_t *palette;     /* PALETTE: 256 entries of R, G, B, A */
    uint32_t width, height, depth;
    unsigned levels;
    int volume;
    uint32_t elements;          /* array slices times cube faces */
    size_t offset;              /* of the first image */
};

static uint32_t get32(const uint8_t *p)
{
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint32_t fourcc(const char *s)
{
    return (uint8_t)s[0] | (uint32_t)(uint8_t)s[1] << 8 |
           (uint32_t)(uint8_t)s[2] << 16 | (uint32_t)(uint8_t)s[3] << 24;
}

static void set_masks(struct layout *l, unsigned bytes, uint32_t r, uint32_t g,
                      uint32_t b, uint32_t a)
{
    l->kind = MASKED;
    l->pixel_bytes = bytes;
    l->mask[0] = r;
    l->mask[1] = g;
    l->mask[2] = b;
    l->mask[3] = a;
}

static enum codec_result dx10_format(struct layout *l, uint32_t format)
{
    switch (format) {
    case 70: case 71: l->kind = BC1; break;
    case 73: case 74: l->kind = BC2; break;
    case 76: case 77: l->kind = BC3; break;
    case 79: case 80: l->kind = BC4; l->alpha = OPAQUE; break;
    case 82: case 83: l->kind = BC5; l->alpha = OPAQUE; break;
    case 84: l->kind = BC5S; l->alpha = OPAQUE; break;
    case 95: l->kind = BC6H; l->alpha = OPAQUE; break;
    case 96: l->kind = BC6HS; l->alpha = OPAQUE; break;
    case 97: case 98: case 99: l->kind = BC7; break;
    /* R8G8B8A8 typeless, UNORM and UNORM_SRGB */
    case 27: case 28: case 29:
        set_masks(l, 4, 0xffu, 0xff00u, 0xff0000u, 0xff000000u);
        break;
    case 87: set_masks(l, 4, 0xff0000u, 0xff00u, 0xffu, 0xff000000u); break;
    case 88:
        set_masks(l, 4, 0xff0000u, 0xff00u, 0xffu, 0);
        l->alpha = OPAQUE;
        break;
    case 24: set_masks(l, 4, 0x3ffu, 0xffc00u, 0x3ff00000u, 0xc0000000u); break;
    default: return CODEC_INVALID;
    }
    return CODEC_OK;
}

static enum codec_result legacy_format(struct layout *l, const uint8_t *pf)
{
    uint32_t flags = get32(pf + 4), code = get32(pf + 8), bits = get32(pf + 12);
    uint32_t r = get32(pf + 16), g = get32(pf + 20), b = get32(pf + 24);
    uint32_t a = get32(pf + 28), used;

    if (flags & PF_FOURCC) {
        /* DXT1's three-colour blocks carry their own transparency, so it
           doesn't depend on DDPF_ALPHAPIXELS. */
        if (code == fourcc("DXT1")) {
            l->kind = BC1;
        } else if (code == fourcc("DXT3")) {
            l->kind = BC2;
        } else if (code == fourcc("DXT5")) {
            l->kind = BC3;
        } else if (code == fourcc("ATI1") || code == fourcc("BC4U")) {
            l->kind = BC4;
        } else if (code == fourcc("ATI2") || code == fourcc("BC5U")) {
            l->kind = BC5;
        } else if (code == fourcc("BC5S")) {
            l->kind = BC5S;
        } else {
            return CODEC_INVALID;
        }
        return CODEC_OK;
    }
    if (flags & PF_PALETTE8) {
        /* The bit count is often wrong; indices are always bytes. */
        l->kind = PALETTE;
        l->alpha = (flags & PF_ALPHAPIXELS) ? STRAIGHT : OPAQUE;
        return CODEC_OK;
    }
    if (!(flags & (PF_RGB | PF_LUMINANCE | PF_ALPHA)))
        return CODEC_INVALID;
    if (bits != 8 && bits != 16 && bits != 24 && bits != 32)
        return CODEC_INVALID;
    used = bits == 32 ? 0xffffffffu : (1u << bits) - 1u;
    if (!(flags & (PF_ALPHAPIXELS | PF_ALPHA)))
        a = 0;
    if (flags & PF_RGB) {
        set_masks(l, bits / 8u, r & used, g & used, b & used, a & used);
    } else if (flags & PF_LUMINANCE) {
        /* Some writers leave the masks unset or out of range; assume the
           usual layouts, gray in the low byte and alpha above it. */
        r &= used;
        a &= used;
        if (r == 0)
            r = 0xffu;
        if (a == 0 && (flags & PF_ALPHAPIXELS) && bits == 16)
            a = 0xff00u;
        set_masks(l, bits / 8u, r, 0, 0, a);
        l->gray = 1;
    } else {
        /* Alpha only: white with the stored alpha. */
        set_masks(l, bits / 8u, 0, 0, 0, (a & used) ? a & used : used);
        l->white = 1;
    }
    if (l->mask[3] == 0)
        l->alpha = OPAQUE;
    return CODEC_OK;
}

static unsigned bits_needed(uint32_t v)
{
    unsigned n = 0;
    while (v) {
        n++;
        v >>= 1;
    }
    return n;
}

static unsigned count_bits(uint32_t v)
{
    unsigned n = 0;
    for (; v; v &= v - 1u)
        n++;
    return n;
}

static enum codec_result parse(const uint8_t *data, size_t length, struct layout *l)
{
    uint32_t caps2, levels, largest;
    size_t n = length < 4 ? length : 4;

    memset(l, 0, sizeof *l);
    if (memcmp(data, "DDS ", n) != 0)
        return CODEC_INVALID;
    if (length < HEADER_SIZE)
        return CODEC_TRUNCATED;
    if (get32(data + 4) != 124)
        return CODEC_INVALID;
    l->height = get32(data + 12);
    l->width = get32(data + 16);
    l->depth = get32(data + 24);
    levels = get32(data + 28);
    caps2 = get32(data + 112);
    l->alpha = STRAIGHT;
    l->elements = 1;
    l->offset = HEADER_SIZE;
    if (get32(data + 84) == fourcc("DX10") && (get32(data + 80) & PF_FOURCC)) {
        const uint8_t *x = data + HEADER_SIZE;
        uint32_t array_size, alpha_mode;
        enum codec_result result;
        if (length < HEADER_SIZE + DX10_SIZE)
            return CODEC_TRUNCATED;
        result = dx10_format(l, get32(x));
        if (result != CODEC_OK)
            return result;
        l->volume = get32(x + 4) == DIMENSION_3D;
        array_size = get32(x + 12);
        if (array_size == 0)
            array_size = 1;
        if (l->volume && array_size != 1)
            return CODEC_INVALID;
        l->elements = array_size;
        if (get32(x + 8) & MISC_CUBE) {
            if (array_size > 0xffffffffu / 6u)
                return CODEC_INVALID;
            l->elements = array_size * 6u;
        }
        alpha_mode = get32(x + 16) & 7u;
        if (l->alpha != OPAQUE && alpha_mode == ALPHA_MODE_OPAQUE)
            l->alpha = OPAQUE;
        else if (l->alpha != OPAQUE && alpha_mode == ALPHA_MODE_PREMULTIPLIED)
            l->alpha = PREMULTIPLIED;
        l->offset += DX10_SIZE;
    } else {
        enum codec_result result = legacy_format(l, data + 76);
        if (result != CODEC_OK)
            return result;
        l->volume = (caps2 & CAPS2_VOLUME) != 0;
        if (caps2 & CAPS2_CUBEMAP) {
            /* A cube map lists its faces; one that lists none has all six. */
            l->elements = count_bits(caps2 & CAPS2_FACES);
            if (l->elements == 0)
                l->elements = 6;
        }
        if (l->kind == PALETTE) {
            if (length < HEADER_SIZE + PALETTE_SIZE)
                return CODEC_TRUNCATED;
            l->palette = data + HEADER_SIZE;
            l->offset += PALETTE_SIZE;
        }
    }
    if (!l->volume || l->depth == 0)
        l->depth = 1;
    if (l->width == 0 || l->height == 0)
        return CODEC_INVALID;
    if (l->width > MAX_SIDE || l->height > MAX_SIDE || l->depth > MAX_SIDE)
        return CODEC_TOO_LARGE;
    largest = l->width > l->height ? l->width : l->height;
    if (l->depth > largest)
        largest = l->depth;
    if (levels == 0)
        levels = 1;
    if (levels > bits_needed(largest))
        return CODEC_INVALID;
    l->levels = levels;
    return CODEC_OK;
}

static uint32_t level_size(uint32_t size, unsigned level)
{
    size >>= level;
    return size ? size : 1;
}

/* Bytes in one depth slice of a level. At most 2^32 * 4. */
static uint64_t slice_bytes(const struct layout *l, unsigned level)
{
    uint64_t w = level_size(l->width, level), h = level_size(l->height, level);
    switch (l->kind) {
    case BC1: case BC4:
        return ((w + 3u) / 4u) * ((h + 3u) / 4u) * 8u;
    case MASKED:
        return w * h * l->pixel_bytes;
    case PALETTE:
        return w * h;
    default:
        return ((w + 3u) / 4u) * ((h + 3u) / 4u) * 16u;
    }
}

static uint32_t level_depth(const struct layout *l, unsigned level)
{
    return l->volume ? level_size(l->depth, level) : 1;
}

/* The images in one array slice or face, and their bytes. */
static void element(const struct layout *l, unsigned long *images, uint64_t *bytes)
{
    unsigned level;
    *images = 0;
    *bytes = 0;
    for (level = 0; level < l->levels; level++) {
        *images += level_depth(l, level);
        *bytes += slice_bytes(l, level) * level_depth(l, level);
    }
}

/* Count the leading images wholly inside the file. */
static unsigned long present(const struct layout *l, size_t length)
{
    uint64_t available = length - l->offset, bytes, whole;
    unsigned long per_element, count;
    unsigned level;
    uint32_t slice;

    element(l, &per_element, &bytes);
    whole = available / bytes;
    if (whole >= l->elements)
        return (unsigned long)l->elements * per_element;
    count = (unsigned long)whole * per_element;
    available -= whole * bytes;
    for (level = 0; level < l->levels; level++) {
        for (slice = 0; slice < level_depth(l, level); slice++) {
            if (available < slice_bytes(l, level))
                return count;
            available -= slice_bytes(l, level);
            count++;
        }
    }
    return count;
}

enum codec_result dds_count(const uint8_t *data, size_t length, unsigned long *count)
{
    struct layout l;
    enum codec_result result = parse(data, length, &l);
    *count = 0;
    if (result != CODEC_OK)
        return result;
    *count = present(&l, length);
    return CODEC_OK;
}

/* Clamp linear light to 0-1 and encode it with the sRGB curve. */
static uint8_t encode_srgb(uint16_t half)
{
    unsigned exponent = (half >> 10) & 31u, mantissa = half & 1023u;
    unsigned low = 0, high = 255, middle;
    float value;

    if (half & 0x8000u)
        return 0;
    if (exponent >= 15)
        return 255;
    if (exponent == 0)
        value = (float)mantissa / 16777216.0f;
    else
        value = (float)((1024u + mantissa) << exponent) / 33554432.0f;
    while (low < high) {
        middle = (low + high) / 2u;
        if (srgb_cut[middle] <= value)
            low = middle + 1u;
        else
            high = middle;
    }
    return (uint8_t)low;
}

static uint8_t scale(uint32_t pixel, uint32_t mask)
{
    unsigned shift = 0;
    if (mask == 0)
        return 0;
    while (!((mask >> shift) & 1u))
        shift++;
    return (uint8_t)((uint64_t)((pixel & mask) >> shift) * 255u / (mask >> shift));
}

static void decode_blocks(const struct layout *l, const uint8_t *p, uint8_t *rgba,
                          unsigned width, unsigned height)
{
    unsigned bx, by, x, y, i, block_bytes = (l->kind == BC1 || l->kind == BC4) ? 8u : 16u;
    uint8_t block[64];
    uint16_t half[48];

    for (by = 0; by < height; by += 4) {
        for (bx = 0; bx < width; bx += 4) {
            switch (l->kind) {
            case BC1: bc1_block(p, block, l->alpha != OPAQUE); break;
            case BC2: bc2_block(p, block); break;
            case BC3: bc3_block(p, block); break;
            case BC4: bc4_block(p, block); break;
            case BC5: bc5_block(p, block, 0); break;
            case BC5S: bc5_block(p, block, 1); break;
            case BC7: bc7_block(p, block); break;
            default:
                bc6h_block(p, half, l->kind == BC6HS);
                for (i = 0; i < 16; i++) {
                    block[i * 4u] = encode_srgb(half[i * 3u]);
                    block[i * 4u + 1u] = encode_srgb(half[i * 3u + 1u]);
                    block[i * 4u + 2u] = encode_srgb(half[i * 3u + 2u]);
                    block[i * 4u + 3u] = 255;
                }
                break;
            }
            p += block_bytes;
            for (y = 0; y < 4 && by + y < height; y++)
                for (x = 0; x < 4 && bx + x < width; x++)
                    memcpy(rgba + ((size_t)(by + y) * width + bx + x) * 4u,
                           block + (y * 4u + x) * 4u, 4);
        }
    }
}

static void decode_pixels(const struct layout *l, const uint8_t *p, uint8_t *rgba,
                          size_t pixels)
{
    size_t i;
    unsigned k;

    for (i = 0; i < pixels; i++, rgba += 4) {
        if (l->kind == PALETTE) {
            memcpy(rgba, l->palette + p[i] * 4u, 4);
        } else {
            uint32_t v = 0;
            for (k = 0; k < l->pixel_bytes; k++)
                v |= (uint32_t)*p++ << (8u * k);
            rgba[0] = l->white ? 255 : scale(v, l->mask[0]);
            rgba[1] = l->gray || l->white ? rgba[0] : scale(v, l->mask[1]);
            rgba[2] = l->gray || l->white ? rgba[0] : scale(v, l->mask[2]);
            rgba[3] = scale(v, l->mask[3]);
        }
    }
}

static void fix_alpha(enum alpha alpha, uint8_t *rgba, size_t pixels)
{
    size_t i;
    unsigned k;
    if (alpha == OPAQUE) {
        for (i = 0; i < pixels; i++)
            rgba[i * 4u + 3u] = 255;
        return;
    }
    if (alpha != PREMULTIPLIED)
        return;
    for (i = 0; i < pixels; i++, rgba += 4) {
        unsigned a = rgba[3];
        if (a == 0 || a == 255)
            continue;
        for (k = 0; k < 3; k++) {
            unsigned c = (rgba[k] * 255u + a / 2u) / a;
            rgba[k] = (uint8_t)(c > 255u ? 255u : c);
        }
    }
}

enum codec_result dds_decode(const uint8_t *data, size_t length, unsigned long index,
                             struct dds_image *image)
{
    struct layout l;
    unsigned long per_element;
    uint64_t element_bytes, offset;
    unsigned level;
    uint32_t w, h;
    size_t pixels;
    enum codec_result result = parse(data, length, &l);

    image->width = image->height = 0;
    image->rgba = NULL;
    if (result != CODEC_OK)
        return result;
    element(&l, &per_element, &element_bytes);
    if (index / per_element >= l.elements)
        return CODEC_INVALID;
    offset = (uint64_t)(index / per_element) * element_bytes;
    index %= per_element;
    for (level = 0; index >= level_depth(&l, level); level++) {
        index -= level_depth(&l, level);
        offset += slice_bytes(&l, level) * level_depth(&l, level);
    }
    offset += index * slice_bytes(&l, level);
    w = level_size(l.width, level);
    h = level_size(l.height, level);
    if ((uint64_t)w * h > MAX_PIXELS)
        return CODEC_TOO_LARGE;
    if (offset > length - l.offset || slice_bytes(&l, level) > length - l.offset - offset)
        return CODEC_TRUNCATED;
    pixels = (size_t)w * h;
    image->rgba = malloc(pixels * 4u);
    if (image->rgba == NULL)
        return CODEC_NO_MEMORY;
    if (l.kind == MASKED || l.kind == PALETTE)
        decode_pixels(&l, data + l.offset + offset, image->rgba, pixels);
    else
        decode_blocks(&l, data + l.offset + offset, image->rgba, w, h);
    fix_alpha(l.alpha, image->rgba, pixels);
    image->width = w;
    image->height = h;
    return CODEC_OK;
}

void dds_free(struct dds_image *image)
{
    free(image->rgba);
    image->rgba = NULL;
}
