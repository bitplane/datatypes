#include "encode.h"
#include "decode.h"
#include <string.h>

static void put_le32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

void dcx_make_directory(uint8_t directory[DCX_DIRECTORY_SIZE])
{
    memset(directory, 0, DCX_DIRECTORY_SIZE);
    put_le32(directory, DCX_MAGIC);
    put_le32(directory + 4, DCX_DIRECTORY_SIZE);
}
