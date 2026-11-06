#ifndef BITPLANE_ICO_SELECT_H
#define BITPLANE_ICO_SELECT_H
#include "common/ico.h"

/* Entry selection with PNG IHDR validation before handing off to a datatype. */
enum codec_result ico_checked_entry(const uint8_t *data, size_t length,
                                     unsigned index, struct ico_entry *entry);
enum codec_result ico_checked_best(const uint8_t *data, size_t length,
                                    unsigned *index);
#endif
