#include "decode.h"
#include <stdlib.h>
#include <string.h>

#define PAM_MAX_PIXELS (16ul * 1024ul * 1024ul)
#define PAM_MAX_SIDE 65535ul
#define PAM_MAX_TYPE 32u

enum { KIND_PAM, KIND_FLOAT, KIND_HALF };
enum { GRAY, GRAY_ALPHA, RGB, RGB_ALPHA, CMYK, CMYK_ALPHA };

/* Planes each model reads; any beyond these are skipped. */
static const unsigned model_planes[] = { 1, 2, 3, 4, 4, 5 };

static const struct { const char *name; int model; } tuple_types[] = {
    { "BLACKANDWHITE", GRAY }, { "GRAYSCALE", GRAY },
    { "BLACKANDWHITE_ALPHA", GRAY_ALPHA }, { "GRAYSCALE_ALPHA", GRAY_ALPHA },
    { "RGB", RGB }, { "RGB_ALPHA", RGB_ALPHA },
    { "CMYK", CMYK }, { "CMYK_ALPHA", CMYK_ALPHA }
};

struct header {
    unsigned long width, height, depth, maxval;
    int kind, model, little;
    size_t sample_bytes, pixel_bytes, start, size;
};

/* Linear values at which the sRGB-encoded 8-bit value steps up:
   cut[i] is where the curve, times 255, reaches i + 0.5. */
static const float srgb_cut[255] = {
    0.000151763492f, 0.000455290475f, 0.000758817459f, 0.00106234444f,
    0.00136587143f, 0.00166939841f, 0.00197292539f, 0.00227645238f,
    0.00257997936f, 0.00288350634f, 0.0031883009f, 0.00350925935f,
    0.00384831493f, 0.00420574803f, 0.00458183274f, 0.00497683725f,
    0.00539102416f, 0.00582465078f, 0.00627796943f, 0.00675122763f,
    0.00724466842f, 0.0077585305f, 0.00829304845f, 0.00884845295f,
    0.00942497089f, 0.0100228256f, 0.0106422369f, 0.0112834213f, 0.0119465921f,
    0.0126319598f, 0.0133397316f, 0.014070112f, 0.0148233028f, 0.0155995031f,
    0.0163989095f, 0.0172217161f, 0.0180681146f, 0.0189382945f, 0.0198324428f,
    0.0207507446f, 0.0216933829f, 0.0226605384f, 0.0236523902f, 0.024669115f,
    0.0257108881f, 0.0267778826f, 0.0278702702f, 0.0289882206f, 0.0301319019f,
    0.0313014806f, 0.0324971216f, 0.0337189882f, 0.0349672424f, 0.0362420443f,
    0.037543553f, 0.0388719259f, 0.0402273192f, 0.0416098877f, 0.0430197848f,
    0.0444571628f, 0.0459221727f, 0.047414964f, 0.0489356854f, 0.0504844842f,
    0.0520615066f, 0.0536668976f, 0.0553008013f, 0.0569633604f, 0.0586547169f,
    0.0603750115f, 0.0621243839f, 0.0639029729f, 0.0657109163f, 0.0675483509f,
    0.0694154125f, 0.0713122362f, 0.0732389559f, 0.0751957047f, 0.077182615f,
    0.0791998181f, 0.0812474446f, 0.0833256241f, 0.0854344855f, 0.087574157f,
    0.0897447658f, 0.0919464383f, 0.0941793004f, 0.096443477f, 0.0987390924f,
    0.10106627f, 0.103425133f, 0.105815802f, 0.108238401f, 0.110693048f,
    0.113179865f, 0.11569897f, 0.118250482f, 0.12083452f, 0.1234512f,
    0.12610064f, 0.128782955f, 0.131498261f, 0.134246673f, 0.137028306f,
    0.139843272f, 0.142691686f, 0.14557366f, 0.148489305f, 0.151438734f,
    0.154422057f, 0.157439385f, 0.160490827f, 0.163576493f, 0.166696492f,
    0.169850932f, 0.17303992f, 0.176263564f, 0.179521971f, 0.182815248f,
    0.186143498f, 0.189506829f, 0.192905345f, 0.196339151f, 0.19980835f,
    0.203313045f, 0.20685334f, 0.210429338f, 0.21404114f, 0.217688849f,
    0.221372565f, 0.225092389f, 0.228848422f, 0.232640764f, 0.236469515f,
    0.240334772f, 0.244236636f, 0.248175205f, 0.252150577f, 0.256162849f,
    0.260212118f, 0.264298482f, 0.268422037f, 0.272582879f, 0.276781103f,
    0.281016805f, 0.285290081f, 0.289601024f, 0.293949728f, 0.298336289f,
    0.302760799f, 0.307223352f, 0.31172404f, 0.316262956f, 0.320840192f,
    0.325455841f, 0.330109993f, 0.33480274f, 0.339534173f, 0.344304382f,
    0.349113458f, 0.353961491f, 0.35884857f, 0.363774785f, 0.368740224f,
    0.373744977f, 0.378789131f, 0.383872775f, 0.388995998f, 0.394158885f,
    0.399361525f, 0.404604005f, 0.409886411f, 0.41520883f, 0.420571347f,
    0.42597405f, 0.431417022f, 0.43690035f, 0.442424119f, 0.447988412f,
    0.453593316f, 0.459238914f, 0.46492529f, 0.470652528f, 0.476420711f,
    0.482229923f, 0.488080246f, 0.493971763f, 0.499904557f, 0.505878709f,
    0.511894303f, 0.517951419f, 0.524050139f, 0.530190544f, 0.536372716f,
    0.542596734f, 0.54886268f, 0.555170635f, 0.561520677f, 0.567912887f,
    0.574347344f, 0.580824128f, 0.587343319f, 0.593904994f, 0.600509233f,
    0.607156115f, 0.613845717f, 0.620578117f, 0.627353395f, 0.634171626f,
    0.641032889f, 0.647937261f, 0.654884819f, 0.66187564f, 0.668909801f,
    0.675987377f, 0.683108445f, 0.690273081f, 0.697481362f, 0.704733362f,
    0.712029156f, 0.719368822f, 0.726752432f, 0.734180063f, 0.741651788f,
    0.749167683f, 0.756727821f, 0.764332277f, 0.771981125f, 0.779674438f,
    0.787412289f, 0.795194753f, 0.803021903f, 0.810893811f, 0.81881055f,
    0.826772194f, 0.834778813f, 0.842830482f, 0.850927271f, 0.859069253f,
    0.867256499f, 0.875489082f, 0.883767073f, 0.892090542f, 0.900459561f,
    0.908874202f, 0.917334534f, 0.925840628f, 0.934392556f, 0.942990386f,
    0.95163419f, 0.960324036f, 0.969059996f, 0.977842139f, 0.986670534f,
    0.99554525f
};

static int is_space(uint8_t c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' ||
           c == '\r';
}

static int is_digit(uint8_t c)
{
    return c >= '0' && c <= '9';
}

static int token_is(const uint8_t *token, size_t length, const char *name)
{
    return length == strlen(name) && memcmp(token, name, length) == 0;
}

/* A whole decimal number. Large values saturate; the caller rejects them. */
static int parse_number(const uint8_t *text, size_t length, unsigned long *value)
{
    unsigned long v = 0;
    size_t i;
    if (length == 0)
        return 0;
    for (i = 0; i < length; i++) {
        if (!is_digit(text[i]))
            return 0;
        if (v < 100000000ul)
            v = v * 10u + (unsigned long)(text[i] - '0');
    }
    *value = v;
    return 1;
}

/* A decimal real such as -1, 1.0 or -1.5e0. */
static int is_real(const uint8_t *text, size_t length)
{
    size_t i = 0, digits = 0;
    int nonzero = 0;
    if (i < length && (text[i] == '+' || text[i] == '-'))
        i++;
    for (; i < length && is_digit(text[i]); i++) {
        digits++;
        nonzero |= text[i] != '0';
    }
    if (i < length && text[i] == '.')
        for (i++; i < length && is_digit(text[i]); i++) {
            digits++;
            nonzero |= text[i] != '0';
        }
    if (digits == 0)
        return 0;
    if (i < length && (text[i] == 'e' || text[i] == 'E')) {
        i++;
        if (i < length && (text[i] == '+' || text[i] == '-'))
            i++;
        if (i == length || !is_digit(text[i]))
            return 0;
        while (i < length && is_digit(text[i]))
            i++;
    }
    return i == length && nonzero;
}

static enum codec_result check_size(const struct header *h)
{
    if (h->width == 0 || h->height == 0)
        return CODEC_INVALID;
    if (h->width > PAM_MAX_SIDE || h->height > PAM_MAX_SIDE ||
        h->width * h->height > PAM_MAX_PIXELS)
        return CODEC_TOO_LARGE;
    return CODEC_OK;
}

/* Header lines after "P7": keyword, value, ending with ENDHDR. */
static enum codec_result parse_pam(const uint8_t *data, size_t length,
                                   size_t pos, struct header *h)
{
    char type[PAM_MAX_TYPE + 1];
    size_t type_length = 0, key, key_length, value, value_length, end, i;
    unsigned seen = 0;
    int long_type = 0;
    enum codec_result result;

    if (pos == length)
        return CODEC_TRUNCATED;
    if (!is_space(data[pos]))
        return CODEC_INVALID;
    for (;;) {
        while (pos < length && is_space(data[pos]))
            pos++;
        for (end = pos; end < length && data[end] != '\n'; end++)
            ;
        if (end == length)
            return CODEC_TRUNCATED;
        if (data[pos] == '#') {
            pos = end + 1;
            continue;
        }
        for (key = pos; pos < end && !is_space(data[pos]); pos++)
            ;
        key_length = pos - key;
        while (pos < end && is_space(data[pos]))
            pos++;
        value = pos;
        value_length = end - pos;
        while (value_length > 0 && is_space(data[value + value_length - 1]))
            value_length--;
        pos = end + 1;
        if (token_is(data + key, key_length, "ENDHDR")) {
            if (value_length != 0)
                return CODEC_INVALID;
            break;
        }
        if (token_is(data + key, key_length, "TUPLTYPE")) {
            /* Repeated lines join with a space, as netpbm does. */
            if (seen & 16u) {
                if (type_length == PAM_MAX_TYPE)
                    long_type = 1;
                else
                    type[type_length++] = ' ';
            }
            seen |= 16u;
            if (value_length > PAM_MAX_TYPE - type_length)
                long_type = 1;
            else {
                memcpy(type + type_length, data + value, value_length);
                type_length += value_length;
            }
            continue;
        }
        if (token_is(data + key, key_length, "WIDTH")) {
            seen |= 1u;
            if (!parse_number(data + value, value_length, &h->width))
                return CODEC_INVALID;
        } else if (token_is(data + key, key_length, "HEIGHT")) {
            seen |= 2u;
            if (!parse_number(data + value, value_length, &h->height))
                return CODEC_INVALID;
        } else if (token_is(data + key, key_length, "DEPTH")) {
            seen |= 4u;
            if (!parse_number(data + value, value_length, &h->depth))
                return CODEC_INVALID;
        } else if (token_is(data + key, key_length, "MAXVAL")) {
            seen |= 8u;
            if (!parse_number(data + value, value_length, &h->maxval))
                return CODEC_INVALID;
        }
        /* Other keywords are ignored. */
    }
    if ((seen & 15u) != 15u || h->depth == 0 || h->maxval == 0 ||
        h->maxval > 65535ul)
        return CODEC_INVALID;
    result = check_size(h);
    if (result != CODEC_OK)
        return result;
    if (h->depth > 65535ul)
        return CODEC_TOO_LARGE;

    /* Without a known tuple type, the depth decides; alpha is never assumed. */
    h->model = h->depth < 3 ? GRAY : RGB;
    if (!long_type)
        for (i = 0; i < sizeof tuple_types / sizeof tuple_types[0]; i++)
            if (token_is((const uint8_t *)type, type_length, tuple_types[i].name)) {
                h->model = tuple_types[i].model;
                if (h->depth < model_planes[h->model])
                    return CODEC_INVALID;
            }
    h->kind = KIND_PAM;
    h->little = 0;
    h->sample_bytes = h->maxval > 255u ? 2u : 1u;
    h->pixel_bytes = (size_t)h->depth * h->sample_bytes;
    h->start = pos;
    return CODEC_OK;
}

/* A float map after its magic: width, height, scale, one whitespace byte. */
static enum codec_result parse_float(const uint8_t *data, size_t length,
                                     size_t pos, struct header *h)
{
    size_t token[3], token_length[3], i;
    enum codec_result result;

    if (pos == length)
        return CODEC_TRUNCATED;
    if (!is_space(data[pos]))
        return CODEC_INVALID;
    for (i = 0; i < 3; i++) {
        while (pos < length && is_space(data[pos]))
            pos++;
        for (token[i] = pos; pos < length && !is_space(data[pos]); pos++)
            ;
        if (pos == length)
            return CODEC_TRUNCATED;
        token_length[i] = pos - token[i];
    }
    if (!parse_number(data + token[0], token_length[0], &h->width) ||
        !parse_number(data + token[1], token_length[1], &h->height) ||
        !is_real(data + token[2], token_length[2]))
        return CODEC_INVALID;
    result = check_size(h);
    if (result != CODEC_OK)
        return result;
    /* The sign of the scale gives the byte order; its size is not used. */
    h->little = data[token[2]] == '-';
    h->maxval = 1;
    h->sample_bytes = h->kind == KIND_HALF ? 2u : 4u;
    h->pixel_bytes = (size_t)h->depth * h->sample_bytes;
    h->start = pos + 1;
    return CODEC_OK;
}

/* Parse the image header at pos and check its raster is all there. */
static enum codec_result parse(const uint8_t *data, size_t length, size_t pos,
                               struct header *h)
{
    enum codec_result result;
    size_t pixels;

    memset(h, 0, sizeof *h);
    if (length - pos < 2)
        return CODEC_TRUNCATED;
    if (data[pos] != 'P')
        return CODEC_INVALID;
    pos += 2;
    switch (data[pos - 1]) {
    case '7':
        result = parse_pam(data, length, pos, h);
        break;
    case 'F': case 'f': case 'H': case 'h':
        h->kind = (data[pos - 1] == 'F' || data[pos - 1] == 'f') ?
                  KIND_FLOAT : KIND_HALF;
        h->depth = 3;
        h->model = RGB;
        if (data[pos - 1] == 'f' || data[pos - 1] == 'h') {
            h->depth = 1;
            h->model = GRAY;
        } else if (data[pos - 1] == 'F' && pos < length && data[pos] == '4') {
            h->depth = 4;
            h->model = RGB_ALPHA;
            pos++;
        }
        result = parse_float(data, length, pos, h);
        break;
    default:
        return CODEC_INVALID;
    }
    if (result != CODEC_OK)
        return result;
    pixels = (size_t)h->width * h->height;
    if ((length - h->start) / h->pixel_bytes < pixels)
        return CODEC_TRUNCATED;
    h->size = pixels * h->pixel_bytes;
    return CODEC_OK;
}

static uint8_t scale_sample(unsigned long value, unsigned long maxval)
{
    if (value > maxval)
        value = maxval;
    return (uint8_t)((value * 255u + maxval / 2u) / maxval);
}

static float read_float(const uint8_t *p, int little)
{
    uint32_t bits;
    float value;
    if (little)
        bits = (uint32_t)p[0] | (uint32_t)p[1] << 8 |
               (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
    else
        bits = (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 |
               (uint32_t)p[2] << 8 | (uint32_t)p[3];
    memcpy(&value, &bits, sizeof value);
    return value;
}

static float read_half(const uint8_t *p, int little)
{
    unsigned bits = little ? (unsigned)p[0] | (unsigned)p[1] << 8
                           : (unsigned)p[0] << 8 | (unsigned)p[1];
    unsigned exponent = (bits >> 10) & 31u, mantissa = bits & 1023u;
    uint32_t single;
    float value;

    if (exponent == 31u)
        value = mantissa != 0 ? 0.0f : 2.0f; /* NaN is dark, infinity bright. */
    else if (exponent == 0)
        value = (float)mantissa / 16777216.0f;
    else {
        single = (uint32_t)(exponent + 112u) << 23 | (uint32_t)mantissa << 13;
        memcpy(&value, &single, sizeof value);
    }
    return (bits & 0x8000u) ? -value : value;
}

/* Clamp linear light to 0-1 and encode it with the sRGB curve. */
static uint8_t encode_srgb(float value)
{
    unsigned low = 0, high = 255, middle;
    if (!(value > 0.0f))
        return 0;
    while (low < high) {
        middle = (low + high) / 2u;
        if (srgb_cut[middle] <= value)
            low = middle + 1u;
        else
            high = middle;
    }
    return (uint8_t)low;
}

static uint8_t encode_alpha(float value)
{
    if (!(value > 0.0f))
        return 0;
    if (value >= 1.0f)
        return 255;
    return (uint8_t)(value * 255.0f + 0.5f);
}

static uint8_t from_cmyk(unsigned colour, unsigned black)
{
    return (uint8_t)(((255u - colour) * (255u - black) + 127u) / 255u);
}

static enum codec_result decode_image(const uint8_t *data, const struct header *h,
                                      struct pam_image *image)
{
    size_t x, y, i, row_bytes = (size_t)h->width * h->pixel_bytes;
    unsigned planes = model_planes[h->model];
    uint8_t *rgba, *q, c[5];
    const uint8_t *p;
    float value;

    rgba = malloc((size_t)h->width * h->height * 4u);
    if (rgba == NULL)
        return CODEC_NO_MEMORY;
    q = rgba;
    for (y = 0; y < h->height; y++) {
        /* Float maps store the bottom row first. */
        p = data + h->start +
            (h->kind == KIND_PAM ? y : h->height - 1u - y) * row_bytes;
        for (x = 0; x < h->width; x++, p += h->pixel_bytes, q += 4) {
            for (i = 0; i < planes; i++) {
                if (h->kind == KIND_PAM) {
                    c[i] = scale_sample(h->sample_bytes == 1 ? p[i] :
                                        (unsigned long)p[2 * i] << 8 | p[2 * i + 1],
                                        h->maxval);
                    continue;
                }
                value = h->kind == KIND_FLOAT ? read_float(p + 4 * i, h->little)
                                              : read_half(p + 2 * i, h->little);
                c[i] = i == 3 ? encode_alpha(value) : encode_srgb(value);
            }
            switch (h->model) {
            case GRAY: case GRAY_ALPHA:
                q[0] = q[1] = q[2] = c[0];
                q[3] = h->model == GRAY_ALPHA ? c[1] : 255;
                break;
            case RGB: case RGB_ALPHA:
                q[0] = c[0]; q[1] = c[1]; q[2] = c[2];
                q[3] = h->model == RGB_ALPHA ? c[3] : 255;
                break;
            default:
                q[0] = from_cmyk(c[0], c[3]);
                q[1] = from_cmyk(c[1], c[3]);
                q[2] = from_cmyk(c[2], c[3]);
                q[3] = h->model == CMYK_ALPHA ? c[4] : 255;
                break;
            }
        }
    }
    image->width = (unsigned)h->width;
    image->height = (unsigned)h->height;
    image->rgba = rgba;
    return CODEC_OK;
}

static size_t skip_space(const uint8_t *data, size_t length, size_t pos)
{
    while (pos < length && is_space(data[pos]))
        pos++;
    return pos;
}

unsigned pam_count(const uint8_t *data, size_t length)
{
    struct header h;
    size_t pos = 0;
    unsigned count = 0;

    if (data == NULL)
        return 0;
    /* Whitespace between images is tolerated. */
    while (count < 65535u && pos < length &&
           parse(data, length, pos, &h) == CODEC_OK) {
        count++;
        pos = skip_space(data, length, h.start + h.size);
    }
    return count;
}

enum codec_result pam_decode(const uint8_t *data, size_t length, unsigned index,
                             struct pam_image *image)
{
    struct header h;
    enum codec_result result;
    size_t pos = 0;
    unsigned i;

    if (image == NULL)
        return CODEC_INVALID;
    image->width = image->height = 0;
    image->rgba = NULL;
    if (data == NULL)
        return CODEC_TRUNCATED;
    for (i = 0;; i++) {
        if (i > 0 && pos == length)
            return CODEC_INVALID; /* No such image. */
        result = parse(data, length, pos, &h);
        if (result != CODEC_OK)
            return result;
        if (i == index)
            return decode_image(data, &h, image);
        pos = skip_space(data, length, h.start + h.size);
    }
}

void pam_free(struct pam_image *image)
{
    free(image->rgba);
    image->rgba = NULL;
    image->width = image->height = 0;
}
