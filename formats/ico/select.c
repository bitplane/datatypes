#include "select.h"
#include <string.h>

enum codec_result ico_checked_entry(const uint8_t *data, size_t length,
                                     unsigned index, struct ico_entry *entry)
{
    enum codec_result result = ico_entry(data, length, index, entry);
    const uint8_t *p;
    unsigned bits, type;

    if (result != CODEC_OK || !entry->png)
        return result;
    /* ico_entry has already checked the signature, 33-byte header, dimensions
       and basic colour/depth values. Check the remaining IHDR constraints here
       without changing the shared helper's compatibility behaviour. */
    p = data + entry->offset;
    bits = p[24];
    type = p[25];
    if (memcmp(p + 8, "\0\0\0\15", 4) != 0 ||
        p[26] != 0 || p[27] != 0 || p[28] > 1 ||
        (type == 3 && bits == 16) ||
        (type != 0 && type != 3 && bits != 8 && bits != 16)) {
        memset(entry, 0, sizeof *entry);
        return CODEC_INVALID;
    }
    return CODEC_OK;
}

enum codec_result ico_checked_best(const uint8_t *data, size_t length,
                                    unsigned *index)
{
    struct ico_entry entry;
    unsigned count, i, best_depth = 0;
    uint64_t best_area = 0, area;
    int cursor, found = 0;
    enum codec_result result, first = CODEC_OK;

    *index = 0;
    result = ico_directory(data, length, &count, &cursor);
    if (result != CODEC_OK)
        return result;
    for (i = 0; i < count; i++) {
        result = ico_checked_entry(data, length, i, &entry);
        if (result != CODEC_OK) {
            if (first == CODEC_OK)
                first = result;
            continue;
        }
        area = (uint64_t)entry.width * entry.height;
        if (!found || area > best_area ||
            (area == best_area && entry.depth > best_depth)) {
            found = 1;
            best_area = area;
            best_depth = entry.depth;
            *index = i;
        }
    }
    return found ? CODEC_OK : first;
}
