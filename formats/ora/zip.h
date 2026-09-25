#ifndef BITPLANE_ORA_ZIP_H
#define BITPLANE_ORA_ZIP_H
#include <stddef.h>
#include <stdint.h>
#include "common/result.h"

#define ZIP_STORED 0u
#define ZIP_DEFLATED 8u

/* One member of a zip archive held in memory. data points into the archive. */
struct zip_entry {
    const uint8_t *data;
    size_t compressed, size;
    unsigned method;
    uint32_t crc;
};

/* Find a member by exact name through the central directory.
   CODEC_INVALID when the archive is fine but has no such member. */
enum codec_result zip_find(const uint8_t *zip, size_t length, const char *name,
                           struct zip_entry *entry);
/* Unpack a member into a new buffer the caller frees, checking its size and CRC.
   max caps the unpacked size. */
enum codec_result zip_extract(const struct zip_entry *entry, size_t max,
                              uint8_t **out);

uint32_t zip_crc32(uint32_t crc, const uint8_t *data, size_t length);

/* A stored-only archive writer over a caller-sized buffer. */
struct zip_writer {
    uint8_t *out;
    size_t capacity, length;
    unsigned count;
    size_t offsets[8];
    const char *names[8];
    uint32_t crcs[8], sizes[8];
};

/* Room needed for count members with these total name and data lengths. */
size_t zip_writer_bound(unsigned count, size_t names, size_t data);
void zip_writer_init(struct zip_writer *w, uint8_t *out, size_t capacity);
enum codec_result zip_add(struct zip_writer *w, const char *name,
                          const uint8_t *data, size_t length);
enum codec_result zip_finish(struct zip_writer *w);

#endif
