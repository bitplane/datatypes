#ifndef BITPLANE_COMMON_DTEMBED_H
#define BITPLANE_COMMON_DTEMBED_H

#include <exec/types.h>

/* Decode a picture embedded in another file, such as a PNG icon entry, with
   whichever datatype recognises it. AROS datatypes only load files, so the data
   goes through a temporary file in T:, as AROS's amigaguide class does. suffix
   names that file's extension, without the dot. On success *rgba is AllocVec'd
   RGBA, top down, at most 65535 on a side and 16M pixels; the caller FreeVecs it.
   Returns 0 or an AROS error. */
LONG dt_load_embedded(const UBYTE *data, ULONG size, const char *suffix,
                      UBYTE **rgba, ULONG *width, ULONG *height);

#endif
