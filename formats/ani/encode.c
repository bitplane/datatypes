#include "encode.h"
#include "common/icoenc.h"
#include <string.h>

/* RIFF header, anih chunk, then LIST fram and the icon chunk's header. */
#define ANIH_SIZE 36u
#define HEADER_SIZE (12u + 8u + ANIH_SIZE + 12u + 8u)

enum { AF_ICON = 1 };

static void put32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

size_t ani_encode_capacity(unsigned width, unsigned height)
{
    size_t ico = ico_encode_capacity(width, height);
    return ico == 0 ? 0 : HEADER_SIZE + ico + 1u;
}

size_t ani_encode(const uint8_t *rgba, unsigned width, unsigned height,
                  unsigned hot_x, unsigned hot_y, uint8_t *output, size_t capacity)
{
    size_t need = ani_encode_capacity(width, height), ico, padded;
    uint8_t *p = output;

    if (need == 0 || capacity < need)
        return 0;
    ico = ico_encode(rgba, width, height, 1, hot_x, hot_y,
                     output + HEADER_SIZE, capacity - HEADER_SIZE);
    if (ico == 0)
        return 0;
    padded = ico + (ico & 1u);
    if (ico & 1u)
        output[HEADER_SIZE + ico] = 0;
    memcpy(p, "RIFF", 4);
    put32(p + 4, (uint32_t)(HEADER_SIZE - 8u + padded));
    memcpy(p + 8, "ACON", 4);
    p += 12;
    memcpy(p, "anih", 4);
    put32(p + 4, ANIH_SIZE);
    memset(p + 8, 0, ANIH_SIZE);
    put32(p + 8, ANIH_SIZE);
    put32(p + 12, 1);               /* frames */
    put32(p + 16, 1);               /* steps */
    put32(p + 36, 10);              /* jiffies per step, 1/6 s */
    put32(p + 40, AF_ICON);
    p += 8 + ANIH_SIZE;
    memcpy(p, "LIST", 4);
    put32(p + 4, (uint32_t)(4u + 8u + padded));
    memcpy(p + 8, "fram", 4);
    memcpy(p + 12, "icon", 4);
    put32(p + 16, (uint32_t)ico);
    return HEADER_SIZE + padded;
}
