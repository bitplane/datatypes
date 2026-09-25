#include "lzss.h"
#include <stdlib.h>
#include <string.h>

#define WINDOW 4096u
#define MIN_MATCH 3u
#define MAX_MATCH 18u
#define HASH_BITS 13u

enum codec_result paa_lzss_expand(const uint8_t *in, size_t in_len,
                                  uint8_t *out, size_t out_len)
{
    size_t ip = 0, op = 0, distance, n;
    uint32_t sum = 0, signed_sum = 0, stored;
    unsigned flags = 0, bit = 8;

    while (op < out_len) {
        if (bit == 8) {
            if (ip >= in_len)
                return CODEC_INVALID;
            flags = in[ip++];
            bit = 0;
        }
        if (flags >> bit++ & 1) {
            if (ip >= in_len)
                return CODEC_INVALID;
            out[op++] = in[ip++];
            continue;
        }
        if (in_len - ip < 2)
            return CODEC_INVALID;
        distance = in[ip] | (size_t)(in[ip + 1] & 0xF0) << 4;
        n = (in[ip + 1] & 0x0F) + MIN_MATCH;
        ip += 2;
        /* Distance 0 would copy the byte being written. */
        if (distance == 0)
            return CODEC_INVALID;
        /* A reference may run past the end; the extra bytes are dropped. */
        if (n > out_len - op)
            n = out_len - op;
        for (; n > 0; n--, op++)
            out[op] = op < distance ? ' ' : out[op - distance];
    }
    if (in_len - ip < 4)
        return CODEC_INVALID;
    stored = (uint32_t)in[ip] | (uint32_t)in[ip + 1] << 8 |
             (uint32_t)in[ip + 2] << 16 | (uint32_t)in[ip + 3] << 24;
    for (op = 0; op < out_len; op++) {
        sum += out[op];
        signed_sum += out[op] < 0x80 ? out[op] : out[op] - 0x100u;
    }
    return stored == sum || stored == signed_sum ? CODEC_OK : CODEC_INVALID;
}

size_t paa_lzss_bound(size_t len)
{
    return len + (len + 7) / 8 + 4;
}

static unsigned hash3(const uint8_t *p)
{
    return ((unsigned)p[0] << 16 | (unsigned)p[1] << 8 | p[2]) * 2654435761u >>
           (32 - HASH_BITS);
}

/* Greedy matching over hash chains; without memory for them, all literals. The decoder's leading spaces are never
   referenced, so any sliding-window LZSS decoder reads the output. */
size_t paa_lzss_pack(const uint8_t *in, size_t len, uint8_t *out)
{
    size_t *head, *prev, ip = 0, op = 0, flag_at = 0, i;
    uint32_t sum = 0;
    unsigned bit = 8;

    head = malloc(((size_t)1 << HASH_BITS) * sizeof *head);
    prev = malloc(WINDOW * sizeof *prev);
    if (head != NULL)
        for (i = 0; i < ((size_t)1 << HASH_BITS); i++)
            head[i] = (size_t)-1;
    while (ip < len) {
        size_t best = 0, best_distance = 0;
        if (bit == 8) {
            flag_at = op++;
            out[flag_at] = 0;
            bit = 0;
        }
        if (head != NULL && prev != NULL && len - ip >= MIN_MATCH) {
            size_t candidate = head[hash3(in + ip)], tries = 64;
            while (candidate != (size_t)-1 && ip - candidate < WINDOW && tries--) {
                size_t n = 0, limit = len - ip < MAX_MATCH ? len - ip : MAX_MATCH;
                while (n < limit && in[candidate + n] == in[ip + n])
                    n++;
                if (n > best) {
                    best = n;
                    best_distance = ip - candidate;
                    if (n == limit)
                        break;
                }
                candidate = prev[candidate % WINDOW];
            }
        }
        if (best < MIN_MATCH)
            best = 1;
        if (best == 1) {
            out[flag_at] |= (uint8_t)(1u << bit);
            out[op++] = in[ip];
        } else {
            out[op++] = (uint8_t)best_distance;
            out[op++] = (uint8_t)((best_distance >> 4 & 0xF0) | (best - MIN_MATCH));
        }
        bit++;
        /* Index every position the item covered. */
        for (i = 0; i < best; i++, ip++) {
            sum += in[ip] < 0x80 ? in[ip] : in[ip] - 0x100u;
            if (head != NULL && prev != NULL && len - ip >= MIN_MATCH) {
                unsigned h = hash3(in + ip);
                prev[ip % WINDOW] = head[h];
                head[h] = ip;
            }
        }
    }
    free(head);
    free(prev);
    out[op++] = (uint8_t)sum;
    out[op++] = (uint8_t)(sum >> 8);
    out[op++] = (uint8_t)(sum >> 16);
    out[op++] = (uint8_t)(sum >> 24);
    return op;
}
