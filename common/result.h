#ifndef BITPLANE_COMMON_RESULT_H
#define BITPLANE_COMMON_RESULT_H

/* Result of a pure codec call. Append new values; never renumber. */
enum codec_result {
    CODEC_OK = 0,
    CODEC_INVALID,
    CODEC_TRUNCATED,
    CODEC_TOO_LARGE,
    CODEC_NO_MEMORY
};

#endif
