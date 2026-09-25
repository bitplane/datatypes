#include "decode.h"
#include <string.h>

/* What a walk over the chunks is looking for. */
struct search {
    unsigned count;          /* icon chunks seen so far */
    unsigned want;           /* the index to stop at */
    const uint8_t *found;
    size_t found_length;
};

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* Visit the chunks from pos up to end. Chunks inside a LIST are visited too, so
   frames count whether or not they are wrapped in LIST fram. Only running past
   the end of the file is truncation: writers get RIFF and LIST sizes wrong, often
   by the 8-byte header or the 4-byte list type. Returns where the walk stopped. */
static enum codec_result walk(const uint8_t *data, size_t length, size_t pos, size_t end,
                              int nested, struct search *s, size_t *stop)
{
    enum codec_result result;
    size_t size, next, inner;

    while (end - pos >= 8 && s->found == NULL) {
        size = le32(data + pos + 4);
        if (size > length - pos - 8)
            return CODEC_TRUNCATED;
        next = pos + 8 + size;
        if (!nested && memcmp(data + pos, "LIST", 4) == 0 && size >= 4) {
            result = walk(data, length, pos + 12, next, 1, s, &inner);
            if (result != CODEC_OK)
                return result;
            /* A list too short for its last chunk ends where that chunk does. */
            if (inner > next)
                next = inner;
        } else if (memcmp(data + pos, "icon", 4) == 0) {
            if (s->count == s->want) {
                s->found = data + pos + 8;
                s->found_length = size;
            }
            s->count++;
        }
        /* Chunks are padded to an even size; the last one's pad may be missing. */
        if (size & 1u)
            next++;
        if (next > end) {
            pos = next;
            break;
        }
        pos = next;
    }
    *stop = pos;
    return CODEC_OK;
}

static enum codec_result search(const uint8_t *data, size_t length, struct search *s)
{
    size_t end, stop;
    enum codec_result result;

    if (length < 12)
        return CODEC_TRUNCATED;
    if (memcmp(data, "RIFF", 4) != 0 || memcmp(data + 8, "ACON", 4) != 0)
        return CODEC_INVALID;
    /* Most writers count the whole file in the RIFF size, 8 bytes too many;
       anything beyond that means the file was cut short. */
    end = le32(data + 4);
    if (end > length)
        return CODEC_TRUNCATED;
    end = end > length - 8 ? length : end + 8;
    if (end < 12)
        end = 12;
    result = walk(data, length, 12, end, 0, s, &stop);
    if (result != CODEC_OK)
        return result;
    return s->count == 0 && s->found == NULL ? CODEC_INVALID : CODEC_OK;
}

enum codec_result ani_count(const uint8_t *data, size_t length, unsigned *count)
{
    struct search s;
    enum codec_result result;

    memset(&s, 0, sizeof s);
    s.want = ~0u;
    result = search(data, length, &s);
    *count = result == CODEC_OK ? s.count : 0;
    return result;
}

enum codec_result ani_frame(const uint8_t *data, size_t length, unsigned index,
                            struct ani_frame *frame)
{
    struct search s;
    unsigned count, best;
    enum codec_result result;

    memset(frame, 0, sizeof *frame);
    memset(&s, 0, sizeof s);
    s.want = index;
    result = search(data, length, &s);
    if (result != CODEC_OK)
        return result;
    if (s.found == NULL)
        return CODEC_INVALID;
    /* Frames without AF_ICON would be raw bitmaps, which no known writer makes;
       files that leave the flag clear still hold ICO data, so the data decides. */
    result = ico_directory(s.found, s.found_length, &count, &frame->cursor);
    if (result == CODEC_OK)
        result = ico_best(s.found, s.found_length, &best);
    if (result == CODEC_OK)
        result = ico_entry(s.found, s.found_length, best, &frame->entry);
    if (result != CODEC_OK) {
        memset(frame, 0, sizeof *frame);
        return result;
    }
    frame->ico = s.found;
    frame->length = s.found_length;
    return CODEC_OK;
}
