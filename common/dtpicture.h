#ifndef BITPLANE_COMMON_DTPICTURE_H
#define BITPLANE_COMMON_DTPICTURE_H

#include <intuition/classes.h>
#include <intuition/classusr.h>

/* Return FALSE to stop dt_each_row. */
typedef BOOL dt_row_fn(void *state, const UBYTE *rgba, ULONG width);

/* Make obj a 32-bit RGBA picture holding these pixels. Returns 0 or an AROS error. */
LONG dt_put_rgba(Class *cl, Object *obj, const UBYTE *rgba,
                 ULONG width, ULONG height);
/* FALSE when obj has no bitmap header or an empty one. */
BOOL dt_picture_size(Object *obj, ULONG *width, ULONG *height);
/* Pass each RGBA row, top down, to fn. TRUE only if every row was passed. */
BOOL dt_each_row(Class *cl, Object *obj, dt_row_fn *fn, void *state);

#endif
