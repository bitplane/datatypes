#include <stdlib.h>
#include <string.h>

#include "common/zlib.h"
#include "zip.h"

#define EOCD_SIZE 22u
#define CENTRAL_SIZE 46u
#define LOCAL_SIZE 30u

static unsigned le16(const uint8_t *p)
{
    return (unsigned)p[0] | (unsigned)p[1] << 8;
}

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint64_t le64(const uint8_t *p)
{
    return (uint64_t)le32(p) | (uint64_t)le32(p + 4) << 32;
}

static void put16(uint8_t *p, unsigned v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, uint32_t v)
{
    put16(p, v & 0xffffu);
    put16(p + 2, v >> 16);
}

uint32_t zip_crc32(uint32_t crc, const uint8_t *data, size_t length)
{
    static const uint32_t nibble[16] = {
        0x00000000, 0x1db71064, 0x3b6e20c8, 0x26d930ac,
        0x76dc4190, 0x6b6b51f4, 0x4db26158, 0x5005713c,
        0xedb88320, 0xf00f9344, 0xd6d6a3e8, 0xcb61b38c,
        0x9b64c2b0, 0x86d3d2d4, 0xa00ae278, 0xbdbdf21c
    };
    size_t i;
    crc = ~crc;
    for (i = 0; i < length; i++) {
        crc ^= data[i];
        crc = (crc >> 4) ^ nibble[crc & 15u];
        crc = (crc >> 4) ^ nibble[crc & 15u];
    }
    return ~crc;
}

/* The end of central directory record: the last one whose comment length
   reaches the end of the file, or failing that the last one at all. */
static const uint8_t *find_end(const uint8_t *zip, size_t length)
{
    const uint8_t *found = NULL;
    size_t pos, lowest;

    if (length < EOCD_SIZE)
        return NULL;
    lowest = length - EOCD_SIZE > 0xffffu ? length - EOCD_SIZE - 0xffffu : 0;
    for (pos = length - EOCD_SIZE + 1u; pos-- > lowest;) {
        if (memcmp(zip + pos, "PK\5\6", 4) != 0)
            continue;
        if (pos + EOCD_SIZE + le16(zip + pos + 20) == length)
            return zip + pos;
        if (found == NULL)
            found = zip + pos;
    }
    return found;
}

/* Replace 0xffffffff fields from a zip64 extra field, in the spec's order. */
static int zip64_fields(const uint8_t *extra, size_t extra_length,
                        uint64_t *size, uint64_t *compressed, uint64_t *offset)
{
    uint64_t *fields[3];
    unsigned count = 0, i;
    size_t pos = 0;

    if (*size == 0xffffffffu)
        fields[count++] = size;
    if (*compressed == 0xffffffffu)
        fields[count++] = compressed;
    if (*offset == 0xffffffffu)
        fields[count++] = offset;
    if (count == 0)
        return 1;
    while (extra_length - pos >= 4) {
        unsigned id = le16(extra + pos), n = le16(extra + pos + 2);
        if (n > extra_length - pos - 4)
            break;
        if (id == 1) {
            if (n < count * 8u)
                return 0;
            for (i = 0; i < count; i++)
                *fields[i] = le64(extra + pos + 4 + i * 8u);
            return 1;
        }
        pos += 4u + n;
    }
    return 0;
}

enum codec_result zip_find(const uint8_t *zip, size_t length, const char *name,
                           struct zip_entry *entry)
{
    const uint8_t *end = find_end(zip, length);
    size_t name_length = strlen(name), pos, directory_end;
    uint64_t directory, directory_size, entries, i;

    memset(entry, 0, sizeof *entry);
    if (end == NULL)
        return length >= 4 && memcmp(zip, "PK\3\4", 4) == 0 ? CODEC_TRUNCATED : CODEC_INVALID;
    entries = le16(end + 10);
    directory_size = le32(end + 12);
    directory = le32(end + 16);
    /* A zip64 archive keeps the real values in a record the locator points to. */
    if ((entries == 0xffffu || directory_size == 0xffffffffu || directory == 0xffffffffu) &&
        (size_t)(end - zip) >= 20 && memcmp(end - 20, "PK\6\7", 4) == 0) {
        uint64_t record = le64(end - 20 + 8);
        if (record > length || length - record < 56 || memcmp(zip + record, "PK\6\6", 4) != 0)
            return CODEC_INVALID;
        entries = le64(zip + record + 32);
        directory_size = le64(zip + record + 40);
        directory = le64(zip + record + 48);
    }
    if (directory > length || directory_size > length - directory)
        return directory > (uint64_t)(end - zip) ? CODEC_TRUNCATED : CODEC_INVALID;
    directory_end = (size_t)(directory + directory_size);

    for (i = 0, pos = (size_t)directory; i < entries; i++) {
        const uint8_t *c = zip + pos, *local;
        unsigned flags, method, n, extra, comment;
        uint64_t size, compressed, offset;
        size_t data;

        if (directory_end - pos < CENTRAL_SIZE || memcmp(c, "PK\1\2", 4) != 0)
            return CODEC_INVALID;
        flags = le16(c + 8);
        method = le16(c + 10);
        compressed = le32(c + 20);
        size = le32(c + 24);
        n = le16(c + 28);
        extra = le16(c + 30);
        comment = le16(c + 32);
        offset = le32(c + 42);
        if ((size_t)n + extra + comment > directory_end - pos - CENTRAL_SIZE)
            return CODEC_INVALID;
        pos += CENTRAL_SIZE + n + extra + comment;
        if (n != name_length || memcmp(c + CENTRAL_SIZE, name, n) != 0)
            continue;

        if (!zip64_fields(c + CENTRAL_SIZE + n, extra, &size, &compressed, &offset))
            return CODEC_INVALID;
        /* Encrypted members, and methods other than store and deflate. */
        if ((flags & 1u) != 0 || (method != ZIP_STORED && method != ZIP_DEFLATED))
            return CODEC_INVALID;
        if (offset > length || length - offset < LOCAL_SIZE)
            return CODEC_TRUNCATED;
        local = zip + offset;
        if (memcmp(local, "PK\3\4", 4) != 0)
            return CODEC_INVALID;
        data = (size_t)offset + LOCAL_SIZE;
        if ((size_t)le16(local + 26) + le16(local + 28) > length - data)
            return CODEC_TRUNCATED;
        data += (size_t)le16(local + 26) + le16(local + 28);
        if (compressed > length - data)
            return CODEC_TRUNCATED;
        if (method == ZIP_STORED && compressed != size)
            return CODEC_INVALID;
        if (size > (size_t)-1)
            return CODEC_TOO_LARGE;
        entry->data = zip + data;
        entry->compressed = (size_t)compressed;
        entry->size = (size_t)size;
        entry->method = method;
        entry->crc = le32(c + 16);
        return CODEC_OK;
    }
    return CODEC_INVALID;
}

enum codec_result zip_extract(const struct zip_entry *entry, size_t max,
                              uint8_t **out)
{
    enum codec_result result = CODEC_OK;
    uint8_t *data;
    size_t written;

    *out = NULL;
    if (entry->size > max)
        return CODEC_TOO_LARGE;
    /* One spare byte, so a stream that runs long is caught. */
    data = malloc(entry->size + 1u);
    if (data == NULL)
        return CODEC_NO_MEMORY;
    if (entry->method == ZIP_STORED) {
        memcpy(data, entry->data, entry->size);
    } else {
        result = zlib_inflate_raw(entry->data, entry->compressed,
                                  data, entry->size + 1u, &written);
        if (result == CODEC_OK && written != entry->size)
            result = written > entry->size ? CODEC_INVALID : CODEC_TRUNCATED;
        else if (result == CODEC_TOO_LARGE)
            result = CODEC_INVALID;
    }
    if (result == CODEC_OK && zip_crc32(0, data, entry->size) != entry->crc)
        result = CODEC_INVALID;
    if (result != CODEC_OK) {
        free(data);
        return result;
    }
    *out = data;
    return CODEC_OK;
}

size_t zip_writer_bound(unsigned count, size_t names, size_t data)
{
    size_t fixed = (size_t)count * (LOCAL_SIZE + CENTRAL_SIZE) + EOCD_SIZE;
    if (names > ((size_t)-1 - fixed) / 2u || data > (size_t)-1 - fixed - names * 2u)
        return 0;
    return fixed + names * 2u + data;
}

void zip_writer_init(struct zip_writer *w, uint8_t *out, size_t capacity)
{
    memset(w, 0, sizeof *w);
    w->out = out;
    w->capacity = capacity;
}

/* Fixed MS-DOS timestamp 1980-01-01 00:00, so output is reproducible. */
#define DOS_DATE ((0u << 9) | (1u << 5) | 1u)

enum codec_result zip_add(struct zip_writer *w, const char *name,
                          const uint8_t *data, size_t length)
{
    size_t n = strlen(name);
    uint8_t *p;

    if (w->count >= sizeof w->offsets / sizeof w->offsets[0] || n > 0xffffu ||
        length > 0xfffffffeu || w->length > 0xfffffffeu)
        return CODEC_TOO_LARGE;
    if (w->capacity - w->length < LOCAL_SIZE + n || w->capacity - w->length - LOCAL_SIZE - n < length)
        return CODEC_TOO_LARGE;
    p = w->out + w->length;
    memcpy(p, "PK\3\4", 4);
    put16(p + 4, 10);
    put16(p + 6, 0);
    put16(p + 8, ZIP_STORED);
    put16(p + 10, 0);
    put16(p + 12, DOS_DATE);
    w->crcs[w->count] = zip_crc32(0, data, length);
    w->sizes[w->count] = (uint32_t)length;
    put32(p + 14, w->crcs[w->count]);
    put32(p + 18, (uint32_t)length);
    put32(p + 22, (uint32_t)length);
    put16(p + 26, (unsigned)n);
    put16(p + 28, 0);
    memcpy(p + LOCAL_SIZE, name, n);
    if (length != 0)
        memcpy(p + LOCAL_SIZE + n, data, length);
    w->offsets[w->count] = w->length;
    w->names[w->count] = name;
    w->count++;
    w->length += LOCAL_SIZE + n + length;
    return CODEC_OK;
}

enum codec_result zip_finish(struct zip_writer *w)
{
    size_t start = w->length;
    unsigned i;
    uint8_t *p;

    for (i = 0; i < w->count; i++) {
        size_t n = strlen(w->names[i]);
        if (w->capacity - w->length < CENTRAL_SIZE + n || w->offsets[i] > 0xfffffffeu)
            return CODEC_TOO_LARGE;
        p = w->out + w->length;
        memset(p, 0, CENTRAL_SIZE);
        memcpy(p, "PK\1\2", 4);
        put16(p + 4, 20);
        put16(p + 6, 10);
        put16(p + 14, DOS_DATE);
        put32(p + 16, w->crcs[i]);
        put32(p + 20, w->sizes[i]);
        put32(p + 24, w->sizes[i]);
        put16(p + 28, (unsigned)n);
        put32(p + 42, (uint32_t)w->offsets[i]);
        memcpy(p + CENTRAL_SIZE, w->names[i], n);
        w->length += CENTRAL_SIZE + n;
    }
    if (w->capacity - w->length < EOCD_SIZE || w->length > 0xfffffffeu)
        return CODEC_TOO_LARGE;
    p = w->out + w->length;
    memset(p, 0, EOCD_SIZE);
    memcpy(p, "PK\5\6", 4);
    put16(p + 8, w->count);
    put16(p + 10, w->count);
    put32(p + 12, (uint32_t)(w->length - start));
    put32(p + 16, (uint32_t)start);
    w->length += EOCD_SIZE;
    return CODEC_OK;
}
