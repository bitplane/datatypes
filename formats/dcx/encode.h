#ifndef BITPLANE_DCX_ENCODE_H
#define BITPLANE_DCX_ENCODE_H
#include <stdint.h>
#include "pcxencode.h"

/* A full 1024-entry directory, as ImageMagick writes it. */
#define DCX_DIRECTORY_SIZE (4u + 1024u * 4u)

/* Directory for one page stored straight after it. */
void dcx_make_directory(uint8_t directory[DCX_DIRECTORY_SIZE]);
#endif
