#include "decode.h"
#include "codes.h"
#include <stdlib.h>
#include <string.h>

/* Four times the usual limit: CALS drawings run to E size at 200 dpi (8800 x
   6800), and one bit per pixel keeps even that at 8MB. */
#define FAX_MAX_PIXELS (64u * 1024u * 1024u)
#define FAX_MAX_SIDE 65535u
#define EOL_ZEROS 11u  /* an EOL is at least 11 zero bits and a one */
#define RTC_EOLS 6u

enum run_result { RUN_OK, RUN_BAD, RUN_SHORT };

struct bits {
    const uint8_t *data;
    size_t total, pos;  /* in bits */
    int reversed;       /* least significant bit first */
};

static unsigned byte_at(const struct bits *b, size_t index)
{
    static const uint8_t flip[16] = {0, 8, 4, 12, 2, 10, 6, 14, 1, 9, 5, 13, 3, 11, 7, 15};
    unsigned value;

    if (index >= b->total / 8u)
        return 0;
    value = b->data[index];
    return b->reversed ? (unsigned)(flip[value & 15u] << 4 | flip[value >> 4]) : value;
}

/* The next count (1 to 24) bits, reading zeros past the end. */
static uint32_t peek(const struct bits *b, unsigned count)
{
    size_t index = b->pos / 8u;
    uint32_t window = (uint32_t)byte_at(b, index) << 24 | (uint32_t)byte_at(b, index + 1) << 16 |
                      (uint32_t)byte_at(b, index + 2) << 8 | (uint32_t)byte_at(b, index + 3);
    return (window << (b->pos % 8u)) >> (32u - count);
}

static size_t bits_left(const struct bits *b)
{
    return b->total - b->pos;
}

static unsigned bit_at(const struct bits *b, size_t pos)
{
    return (byte_at(b, pos / 8u) >> (7u - pos % 8u)) & 1u;
}

/* Zero bits from the current position, up to the end of the data. */
static size_t count_zeros(const struct bits *b)
{
    size_t count = 0;
    while (b->pos + count < b->total && bit_at(b, b->pos + count) == 0)
        count++;
    return count;
}

/* Zero bits just before the current position, up to an EOL's worth. */
static size_t zeros_behind(const struct bits *b)
{
    size_t count = 0;
    while (count < EOL_ZEROS && count < b->pos && bit_at(b, b->pos - count - 1u) == 0)
        count++;
    return count;
}

/* Move past the next EOL, adding the one bits skipped before it to *ones.
   Zero if there is none. */
static int skip_to_eol(struct bits *b, size_t *ones)
{
    size_t zeros = zeros_behind(b);
    while (b->pos < b->total) {
        if (bit_at(b, b->pos++) == 0) {
            zeros++;
        } else if (zeros >= EOL_ZEROS) {
            return 1;
        } else {
            zeros = 0;
            ++*ones;
        }
    }
    return 0;
}

static enum run_result read_code(struct bits *b, const struct fax_lookup *lookup, unsigned *run)
{
    uint32_t index = peek(b, FAX_LOOKUP_BITS);
    unsigned length = lookup->length[index];

    if (length == 0)
        return bits_left(b) < FAX_LOOKUP_BITS ? RUN_SHORT : RUN_BAD;
    if (length > bits_left(b))
        return RUN_SHORT;
    b->pos += length;
    *run = lookup->run[index];
    return RUN_OK;
}

/* Make-up codes, then the terminating code that ends every run. */
static enum run_result read_run(struct bits *b, const struct fax_lookup *lookup, unsigned *run)
{
    unsigned total = 0, part;
    enum run_result result;

    for (;;) {
        if ((result = read_code(b, lookup, &part)) != RUN_OK)
            return result;
        total += part;
        if (total > FAX_MAX_SIDE)
            return RUN_BAD;
        if (part < 64u)
            break;
    }
    *run = total;
    return RUN_OK;
}

/* Set the bits for pixels from to to, not including to. */
static void paint(uint8_t *row, unsigned width, unsigned from, unsigned to)
{
    unsigned first, last;

    if (row == NULL || from >= width)
        return;
    if (to > width)
        to = width;
    if (from >= to)
        return;
    first = from / 8u;
    last = (to - 1u) / 8u;
    if (first == last) {
        row[first] |= (uint8_t)((0xffu >> from % 8u) & (0xffu << (7u - (to - 1u) % 8u)));
        return;
    }
    row[first] |= (uint8_t)(0xffu >> from % 8u);
    memset(row + first + 1u, 0xff, last - first - 1u);
    row[last] |= (uint8_t)(0xffu << (7u - (to - 1u) % 8u));
}

static struct fax_lookup *make_lookups(void)
{
    struct fax_lookup *lookups = malloc(2 * sizeof *lookups);
    if (lookups != NULL) {
        fax_build_lookup(&lookups[0], 0);
        fax_build_lookup(&lookups[1], 1);
    }
    return lookups;
}

static enum codec_result allocate(struct fax_image *image, unsigned width, unsigned height)
{
    if (width == 0 || height == 0)
        return CODEC_INVALID;
    if (width > FAX_MAX_SIDE || height > FAX_MAX_SIDE ||
        (size_t)width * height > FAX_MAX_PIXELS)
        return CODEC_TOO_LARGE;
    image->stride = (width + 7u) / 8u;
    image->pixels = calloc(image->stride * height, 1);
    if (image->pixels == NULL)
        return CODEC_NO_MEMORY;
    image->width = width;
    image->height = height;
    return CODEC_OK;
}

void fax_free(struct fax_image *image)
{
    free(image->pixels);
    image->pixels = NULL;
    image->width = image->height = 0;
    image->stride = 0;
}

/* ---- Group 3, one-dimensional ---- */

/* Codes up to the next EOL, fill or the end of the data. Like g3topbm, an
   EOL is any 11 zero bits and a one, even where the zeros begin inside the
   last code, and a make-up code counts as soon as it's read. No two valid
   codes together hold more than 10 zeros, so clean data never ends early. */
static enum run_result g3_line(struct bits *b, const struct fax_lookup *lookups,
                               uint8_t *row, unsigned width, unsigned *length)
{
    unsigned x = 0, run = 0, colour = 0;
    size_t ahead;
    enum run_result result;

    *length = 0;
    for (;;) {
        ahead = count_zeros(b);
        /* The data may end after a line, but not between a make-up code and its run. */
        if (ahead == bits_left(b))
            return run < 64u ? RUN_OK : RUN_SHORT;
        if (ahead + zeros_behind(b) >= EOL_ZEROS)
            return RUN_OK;
        if ((result = read_code(b, &lookups[colour], &run)) != RUN_OK)
            return result;
        if (run > FAX_MAX_SIDE - x)
            return RUN_BAD;
        if (colour)
            paint(row, width, x, x + run);
        x += run;
        *length = x;
        if (run < 64u)
            colour ^= 1u;
    }
}

struct g3_page {
    enum codec_result result;
    unsigned width, height, good, bad;
    unsigned full;  /* clean lines as wide as the widest */
    size_t junk;    /* one bits outside the page's lines */
};

/* Measure the page, or with image set, draw it. */
static void g3_page(const uint8_t *data, size_t length, int reversed,
                    const struct fax_lookup *lookups, struct fax_image *image,
                    struct g3_page *page)
{
    struct bits b;
    unsigned eols = 1, y = 0, line;
    size_t zeros;
    enum run_result result;

    memset(page, 0, sizeof *page);
    b.data = data;
    b.total = length > SIZE_MAX / 8u ? SIZE_MAX / 8u * 8u : length * 8u;
    b.pos = 0;
    b.reversed = reversed;
    /* A page starts with an EOL; anything before it isn't image data. */
    if (!skip_to_eol(&b, &page->junk)) {
        page->result = CODEC_INVALID;
        return;
    }
    for (;;) {
        zeros = count_zeros(&b);
        if (zeros == bits_left(&b))
            break;
        if (zeros >= EOL_ZEROS) {
            b.pos += zeros + 1u;
            if (++eols == RTC_EOLS) {
                /* Whatever follows, such as another page, isn't this page. */
                while (b.pos < b.total)
                    page->junk += bit_at(&b, b.pos++);
                break;
            }
            continue;
        }
        /* Each EOL after the first ended an empty line. */
        if (eols > FAX_MAX_SIDE - y) {
            page->result = CODEC_TOO_LARGE;
            return;
        }
        y += eols - 1u;
        result = g3_line(&b, lookups,
                         image != NULL ? image->pixels + (size_t)y * image->stride : NULL,
                         image != NULL ? image->width : 0, &line);
        y++;
        if (line > page->width) {
            page->width = line;
            page->full = 0;
        }
        if (result == RUN_SHORT) {
            page->result = CODEC_TRUNCATED;
            return;
        }
        eols = 1;
        if (result == RUN_BAD) {
            /* Keep what decoded, like a fax machine, and carry on at the next line. */
            page->bad++;
            if (!skip_to_eol(&b, &page->junk))
                break;
            continue;
        }
        page->good++;
        if (line == page->width)
            page->full++;
        zeros = count_zeros(&b);
        if (zeros == bits_left(&b)) {
            /* The data ends without an EOL: the line is only whole if it is full width. */
            if (image == NULL && line < page->width)
                page->result = CODEC_TRUNCATED;
            break;
        }
        b.pos += zeros + 1u;
    }
    page->height = y;
}

/* Most lines decoded cleanly to the same width, as a real page's do. The
   wrong bit order gives lines of scattered lengths, if any. */
static int consistent(const struct g3_page *page)
{
    return page->result == CODEC_OK && page->full * 2u > page->good + page->bad;
}

enum codec_result fax_decode_g3(const uint8_t *data, size_t length, struct fax_image *image)
{
    struct fax_lookup *lookups;
    struct g3_page forward, backward, *page = NULL;
    enum codec_result result;

    if (image == NULL)
        return CODEC_INVALID;
    image->width = image->height = 0;
    image->stride = 0;
    image->pixels = NULL;
    if (data == NULL || length == 0)
        return CODEC_TRUNCATED;
    if ((lookups = make_lookups()) == NULL)
        return CODEC_NO_MEMORY;
    /* T.4 sends the first bit of each byte first, but many modems store the
       last bit first. A clean read forwards settles it; otherwise take the
       order whose page is consistent and leaves the fewest bits unexplained,
       since the wrong order skips data or stops early at a false RTC. */
    g3_page(data, length, 0, lookups, NULL, &forward);
    if (forward.result == CODEC_OK && forward.bad == 0 && forward.junk == 0) {
        if (forward.good > 0)
            page = &forward;
    } else {
        g3_page(data, length, 1, lookups, NULL, &backward);
        if (consistent(&backward) &&
            (!consistent(&forward) || backward.junk < forward.junk))
            page = &backward;
        else if (forward.result == CODEC_OK && forward.good > 0)
            page = &forward;
    }
    if (page == NULL) {
        result = forward.result != CODEC_OK ? forward.result : CODEC_INVALID;
    } else if ((result = allocate(image, page->width, page->height)) == CODEC_OK) {
        g3_page(data, length, page == &backward, lookups, image, page);
        result = page->result;
    }
    free(lookups);
    if (result != CODEC_OK)
        fax_free(image);
    return result;
}

/* ---- Group 4 ---- */

/* One coding line. ref holds the reference line's changing elements followed
   by three copies of width; cur receives this line's, *count of them. */
static enum codec_result g4_line(struct bits *b, const struct fax_lookup *lookups,
                                 const unsigned *ref, unsigned *cur, unsigned *count,
                                 unsigned width, uint8_t *row)
{
    long a0 = -1;
    unsigned colour = 0, n = 0, start, b1, b2, r1, r2;
    size_t i = 0;
    uint32_t mode;
    enum run_result result;

    while (a0 < (long)width) {
        start = a0 < 0 ? 0u : (unsigned)a0;
        /* b1: the first change on the reference line right of a0 to the
           opposite colour; even entries are changes to black. */
        while (i > 0 && (long)ref[i - 1] > a0)
            i--;
        while ((long)ref[i] <= a0)
            i++;
        if ((i & 1u) != colour)
            i++;
        b1 = ref[i];
        b2 = ref[i + 1];
        if (bits_left(b) == 0)
            return CODEC_TRUNCATED;
        if (n + 2u > width + 2u)
            return CODEC_INVALID;
        mode = peek(b, 7);
        if (mode >> 3 == 1u) {                    /* 0001: pass */
            if (bits_left(b) < 4)
                return CODEC_TRUNCATED;
            b->pos += 4;
            if (colour)
                paint(row, width, start, b2);
            a0 = (long)b2;
        } else if (mode >> 4 == 1u) {             /* 001: horizontal */
            if (bits_left(b) < 3)
                return CODEC_TRUNCATED;
            b->pos += 3;
            if ((result = read_run(b, &lookups[colour], &r1)) != RUN_OK ||
                (result = read_run(b, &lookups[colour ^ 1u], &r2)) != RUN_OK)
                return result == RUN_SHORT ? CODEC_TRUNCATED : CODEC_INVALID;
            if (r1 > width - start || r2 > width - start - r1)
                return CODEC_INVALID;
            paint(row, width, colour ? start : start + r1,
                  colour ? start + r1 : start + r1 + r2);
            cur[n++] = start + r1;
            cur[n++] = start + r1 + r2;
            a0 = (long)(start + r1 + r2);
        } else {
            static const struct { uint8_t bits, length; int8_t delta; } vertical[] = {
                {0x40, 1, 0}, {0x30, 3, 1}, {0x20, 3, -1}, {0x06, 6, 2},
                {0x04, 6, -2}, {0x03, 7, 3}, {0x02, 7, -3},
            };
            size_t v;
            long a1;
            for (v = 0; v < sizeof vertical / sizeof vertical[0]; v++)
                if (mode >> (7u - vertical[v].length) == (uint32_t)vertical[v].bits >> (7u - vertical[v].length))
                    break;
            if (v == sizeof vertical / sizeof vertical[0]) {
                /* An EOL or EOFB ends the data before the last row;
                   anything else is uncompressed mode or garbage. */
                if (bits_left(b) < 12 || peek(b, 12) == 1u)
                    return CODEC_TRUNCATED;
                return CODEC_INVALID;
            }
            if (bits_left(b) < vertical[v].length)
                return CODEC_TRUNCATED;
            b->pos += vertical[v].length;
            a1 = (long)b1 + vertical[v].delta;
            if (a1 < (long)start || a1 > (long)width)
                return CODEC_INVALID;
            if (colour)
                paint(row, width, start, (unsigned)a1);
            cur[n++] = (unsigned)a1;
            colour ^= 1u;
            a0 = a1;
        }
    }
    cur[n] = cur[n + 1] = cur[n + 2] = width;
    *count = n;
    return CODEC_OK;
}

enum codec_result fax_decode_g4(const uint8_t *data, size_t length,
                                unsigned width, unsigned height, struct fax_image *image)
{
    struct fax_lookup *lookups = NULL;
    unsigned *ref = NULL, *cur = NULL, *swap, count, y;
    struct bits b;
    enum codec_result result;

    if (image == NULL)
        return CODEC_INVALID;
    image->width = image->height = 0;
    image->stride = 0;
    image->pixels = NULL;
    if (data == NULL && length != 0)
        return CODEC_INVALID;
    if ((result = allocate(image, width, height)) != CODEC_OK)
        return result;
    lookups = make_lookups();
    ref = malloc(((size_t)width + 8u) * sizeof *ref);
    cur = malloc(((size_t)width + 8u) * sizeof *cur);
    if (lookups == NULL || ref == NULL || cur == NULL) {
        result = CODEC_NO_MEMORY;
        goto done;
    }
    /* The line above the first is white. */
    ref[0] = ref[1] = ref[2] = width;
    b.data = data;
    b.total = length > SIZE_MAX / 8u ? SIZE_MAX / 8u * 8u : length * 8u;
    b.pos = 0;
    b.reversed = 0;
    for (y = 0; y < height; y++) {
        result = g4_line(&b, lookups, ref, cur, &count, width,
                         image->pixels + (size_t)y * image->stride);
        if (result != CODEC_OK)
            break;
        swap = ref; ref = cur; cur = swap;
    }
done:
    free(lookups);
    free(ref);
    free(cur);
    if (result != CODEC_OK)
        fax_free(image);
    return result;
}

/* ---- CALS type 1 ---- */

#define CALS_RECORD 128u

static int keyword(const uint8_t *record, const char *word)
{
    size_t i;
    for (i = 0; word[i] != '\0'; i++) {
        uint8_t c = record[i];
        if (c >= 'A' && c <= 'Z')
            c = (uint8_t)(c - 'A' + 'a');
        if (c != (uint8_t)word[i])
            return 0;
    }
    return 1;
}

/* Up to count comma-separated decimal numbers after the keyword. */
static int numbers(const uint8_t *record, size_t from, unsigned long *values, unsigned count)
{
    size_t pos = from;
    unsigned i;

    for (i = 0; i < count; i++) {
        unsigned long value = 0;
        size_t digits = 0;
        while (pos < CALS_RECORD && record[pos] == ' ')
            pos++;
        if (i > 0) {
            if (pos >= CALS_RECORD || record[pos] != ',')
                return 0;
            pos++;
            while (pos < CALS_RECORD && record[pos] == ' ')
                pos++;
        }
        while (pos < CALS_RECORD && record[pos] >= '0' && record[pos] <= '9') {
            if (value < 100000000ul)
                value = value * 10u + (unsigned long)(record[pos] - '0');
            pos++;
            digits++;
        }
        if (digits == 0)
            return 0;
        values[i] = value;
    }
    return 1;
}

/* The records of a MIL-PRF-28002 header, in their usual order. */
static const char *const cals_keywords[] = {
    "srcdocid:", "dstdocid:", "txtfilid:", "figid:", "srcgph:", "doccls:",
    "rtype:", "rorient:", "rpelcnt:", "rdensty:", "notes:", "version: mil-std-1840",
};

int fax_is_cals(const uint8_t *data, size_t length)
{
    size_t i;

    if (data == NULL || length < CALS_RECORD)
        return 0;
    /* Raw Group 3 starts with fill bits or an EOL, never text. */
    for (i = 0; i < sizeof cals_keywords / sizeof cals_keywords[0]; i++)
        if (keyword(data, cals_keywords[i]))
            return 1;
    return 0;
}

/* Screen direction of a CALS angle, counter-clockwise from rightwards. */
static int direction(unsigned long angle, int *dx, int *dy)
{
    switch (angle) {
    case 0: *dx = 1; *dy = 0; return 1;
    case 90: *dx = 0; *dy = -1; return 1;
    case 180: *dx = -1; *dy = 0; return 1;
    case 270: *dx = 0; *dy = 1; return 1;
    default: return 0;
    }
}

/* Lay the stored pels along the pel path and the lines along the line
   progression, which is measured counter-clockwise from the pel path. */
static enum codec_result orient(struct fax_image *image, unsigned long pel_path,
                                unsigned long progression)
{
    int px, py, lx, ly;
    unsigned x, y, width = image->width, height = image->height, out_width, out_height;
    size_t ox, oy, out_stride;
    uint8_t *out;

    if ((progression != 90 && progression != 270) || !direction(pel_path, &px, &py) ||
        !direction((pel_path + progression) % 360u, &lx, &ly) || (px == 1 && ly == 1))
        return CODEC_OK;
    out_width = px != 0 ? width : height;
    out_height = px != 0 ? height : width;
    out_stride = (out_width + 7u) / 8u;
    out = calloc(out_stride * out_height, 1);
    if (out == NULL)
        return CODEC_NO_MEMORY;
    ox = (px < 0 ? width - 1u : 0) + (lx < 0 ? height - 1u : 0);
    oy = (py < 0 ? width - 1u : 0) + (ly < 0 ? height - 1u : 0);
    for (y = 0; y < height; y++) {
        const uint8_t *row = image->pixels + (size_t)y * image->stride;
        for (x = 0; x < width; x++) {
            size_t tx, ty;
            if (row[x / 8u] == 0) {
                x |= 7u;
                continue;
            }
            if (!(row[x / 8u] >> (7u - x % 8u) & 1u))
                continue;
            tx = ox + (size_t)((long)x * px + (long)y * lx);
            ty = oy + (size_t)((long)x * py + (long)y * ly);
            out[ty * out_stride + tx / 8u] |= (uint8_t)(0x80u >> tx % 8u);
        }
    }
    free(image->pixels);
    image->pixels = out;
    image->width = out_width;
    image->height = out_height;
    image->stride = out_stride;
    return CODEC_OK;
}

static enum codec_result decode_cals(const uint8_t *data, size_t length, struct fax_image *image)
{
    unsigned long type = 1, size[2] = {0, 0}, angles[2] = {0, 270};
    int have_size = 0;
    size_t r;
    enum codec_result result;

    if (length < CALS_HEADER_SIZE)
        return CODEC_TRUNCATED;
    for (r = 0; r < CALS_HEADER_SIZE / CALS_RECORD; r++) {
        const uint8_t *record = data + r * CALS_RECORD;
        unsigned long values[2];
        if (keyword(record, "rtype:")) {
            if (numbers(record, 6, values, 1))
                type = values[0];
        } else if (keyword(record, "rpelcnt:")) {
            if (numbers(record, 8, values, 2)) {
                size[0] = values[0];
                size[1] = values[1];
                have_size = 1;
            }
        } else if (keyword(record, "rorient:")) {
            /* Orientation is optional to honour: ignore values that don't parse. */
            if (numbers(record, 8, values, 2)) {
                angles[0] = values[0];
                angles[1] = values[1];
            }
        }
    }
    /* Type 2 rasters are tiled and mixed; only type 1 is one G4 image. */
    if (type != 1 || !have_size || size[0] == 0 || size[1] == 0)
        return CODEC_INVALID;
    if (size[0] > FAX_MAX_SIDE || size[1] > FAX_MAX_SIDE ||
        size[0] * size[1] > FAX_MAX_PIXELS)
        return CODEC_TOO_LARGE;
    result = fax_decode_g4(data + CALS_HEADER_SIZE, length - CALS_HEADER_SIZE,
                           (unsigned)size[0], (unsigned)size[1], image);
    if (result == CODEC_OK && (result = orient(image, angles[0], angles[1])) != CODEC_OK)
        fax_free(image);
    return result;
}

enum codec_result fax_decode(const uint8_t *data, size_t length, struct fax_image *image)
{
    if (image == NULL)
        return CODEC_INVALID;
    image->width = image->height = 0;
    image->stride = 0;
    image->pixels = NULL;
    if (fax_is_cals(data, length))
        return decode_cals(data, length, image);
    return fax_decode_g3(data, length, image);
}
