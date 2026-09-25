#include "decode.h"
#include <stdlib.h>
#include <string.h>

#define PDB_HEADER 78u
#define PDB_RECORD 8u
#define PDB_IMAGE_HEADER 58u
#define PDB_MAX_PIXELS (16u * 1024u * 1024u)

static unsigned read16(const uint8_t *p)
{
    return ((unsigned)p[0] << 8) | p[1];
}

static unsigned long read32(const uint8_t *p)
{
    return ((unsigned long)p[0] << 24) | ((unsigned long)p[1] << 16) |
           ((unsigned long)p[2] << 8) | p[3];
}

unsigned pdb_shade(unsigned depth, unsigned value)
{
    unsigned top = (1u << depth) - 1u;
    return 255u - value * 255u / top;
}

/* Fill out[0..size) from RLE codes. Codes above 0x80 repeat the next byte
   code - 127 times; the rest copy code + 1 literal bytes. Runs cross rows,
   and a run past the last byte is cut off. */
static enum codec_result unpack(const uint8_t *in, size_t length, uint8_t *out, size_t size)
{
    size_t pos = 0, done = 0, count;

    while (done < size) {
        unsigned code;
        if (pos >= length)
            return CODEC_TRUNCATED;
        code = in[pos++];
        if (code > 0x80u) {
            if (pos >= length)
                return CODEC_TRUNCATED;
            count = code - 127u;
            if (count > size - done)
                count = size - done;
            memset(out + done, in[pos++], count);
        } else {
            count = code + 1u;
            if (count > size - done)
                count = size - done;
            if (length - pos < count)
                return CODEC_TRUNCATED;
            memcpy(out + done, in + pos, count);
            pos += count;
        }
        done += count;
    }
    return CODEC_OK;
}

void pdb_free(struct pdb_image *image)
{
    free(image->pixels);
    image->pixels = NULL;
    image->width = image->height = image->depth = 0;
}

enum codec_result pdb_decode(const uint8_t *data, size_t length, struct pdb_image *image)
{
    const uint8_t *header, *bits;
    unsigned width, height, depth, x, y;
    unsigned long offset, end;
    size_t stride, size;
    uint8_t *packed = NULL, *out;
    enum codec_result result;

    if (image == NULL)
        return CODEC_INVALID;
    image->width = image->height = image->depth = 0;
    image->pixels = NULL;
    if (data == NULL || length < 68)
        return CODEC_TRUNCATED;
    if (memcmp(data + 60, "vIMGView", 8) != 0)
        return CODEC_INVALID;
    if (length < PDB_HEADER + PDB_RECORD)
        return CODEC_TRUNCATED;
    if (read16(data + 76) == 0)
        return CODEC_INVALID;
    /* The image is the first record. It ends where the note record starts,
       or at the end of the file when there is no note or its offset is bad. */
    offset = read32(data + PDB_HEADER);
    if (offset < PDB_HEADER + PDB_RECORD)
        return CODEC_INVALID;
    if (offset > length || length - offset < PDB_IMAGE_HEADER)
        return CODEC_TRUNCATED;
    end = length;
    if (read16(data + 76) > 1 && offset >= PDB_HEADER + 2u * PDB_RECORD) {
        unsigned long note = read32(data + PDB_HEADER + PDB_RECORD);
        if (note >= offset + PDB_IMAGE_HEADER && note < end)
            end = note;
    }
    header = data + offset;
    switch (header[33]) {
    case 0xff: depth = 1; break;
    case 0x00: depth = 2; break;
    case 0x02: depth = 4; break;
    default: return CODEC_INVALID;
    }
    if ((header[32] & 7u) > 1u)
        return CODEC_INVALID;
    width = read16(header + 54);
    height = read16(header + 56);
    if (width == 0 || height == 0)
        return CODEC_INVALID;
    if ((unsigned long)width * height > PDB_MAX_PIXELS)
        return CODEC_TOO_LARGE;
    stride = ((size_t)width * depth + 7u) / 8u;
    size = stride * height;
    bits = header + PDB_IMAGE_HEADER;
    end -= offset + PDB_IMAGE_HEADER;
    if ((header[32] & 7u) == 0) {
        if (end < size)
            return CODEC_TRUNCATED;
    } else {
        packed = malloc(size);
        if (packed == NULL)
            return CODEC_NO_MEMORY;
        result = unpack(bits, end, packed, size);
        if (result != CODEC_OK) {
            free(packed);
            return result;
        }
        bits = packed;
    }
    image->pixels = malloc((size_t)width * height);
    if (image->pixels == NULL) {
        free(packed);
        return CODEC_NO_MEMORY;
    }
    out = image->pixels;
    for (y = 0; y < height; y++, bits += stride) {
        for (x = 0; x < width; x++) {
            unsigned bit = x * depth;
            *out++ = (uint8_t)((bits[bit / 8u] >> (8u - depth - bit % 8u)) & ((1u << depth) - 1u));
        }
    }
    free(packed);
    image->width = width;
    image->height = height;
    image->depth = depth;
    return CODEC_OK;
}
