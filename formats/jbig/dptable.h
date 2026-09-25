#ifndef BITPLANE_JBIG_DPTABLE_H
#define BITPLANE_JBIG_DPTABLE_H
#include <stdint.h>

/* 2 bits for each of 256 + 512 + 2048 + 4096 entries (T.82 6.6.3). */
#define JBIG_DP_TABLE_SIZE 1728

extern const uint8_t jbig_default_dp[JBIG_DP_TABLE_SIZE];
#endif
