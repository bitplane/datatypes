#ifndef BITPLANE_COMMON_DTFILE_H
#define BITPLANE_COMMON_DTFILE_H

#include <dos/dos.h>
#include <intuition/classes.h>
#include <intuition/classusr.h>

#include "common/result.h"

/* Superclass OM_NEW, then load; a load error disposes the object and sets IoErr. */
IPTR dt_new(Class *cl, Object *obj, struct opSet *msg,
            LONG (*load)(Class *cl, Object *obj));
/* Read the whole source file into AllocVec memory; the caller FreeVecs it.
   Returns 0 or an AROS error. A DTST_RAM object has no file: 0 and NULL. */
LONG dt_read_file(Object *obj, LONG min, LONG max, UBYTE **data, LONG *size);
LONG dt_error(enum codec_result result);
/* Name the object after its source file. */
void dt_set_name(Object *obj);
BOOL dt_write(BPTR file, const void *data, LONG size);

#endif
