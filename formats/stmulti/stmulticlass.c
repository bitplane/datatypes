#include <aros/symbolsets.h>
#include <datatypes/datatypes.h>
#include <datatypes/datatypesclass.h>
#include <datatypes/pictureclass.h>
#include <dos/dos.h>
#include <exec/memory.h>
#include <proto/datatypes.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/utility.h>

#include "common/dtfile.h"
#include "common/dtpicture.h"
#include "decode.h"

/* The largest MPP is about 165 KB and PCS files are smaller; anything
   after the picture is ignored. */
#define MAX_STMULTI_FILE (1024L * 1024L)

ADD2LIBS((const UBYTE *)"datatypes/picture.datatype", 0, struct Library *, PictureBase);

static LONG load_stmulti(Class *cl, Object *obj)
{
    struct stmulti_image image;
    UBYTE *input;
    LONG size, error;

    error = dt_read_file(obj, 6, MAX_STMULTI_FILE, &input, &size);
    if (error != 0 || input == NULL)
        return error;
    error = dt_error(stmulti_decode(input, (size_t)size, &image));
    FreeVec(input);
    if (error != 0)
        return error;
    error = dt_put_rgba(cl, obj, image.rgba, image.width, image.height);
    stmulti_free(&image);
    if (error == 0)
        dt_set_name(obj);
    return error;
}

IPTR Stmulti__OM_NEW(Class *cl, Object *obj, struct opSet *msg)
{
    return dt_new(cl, obj, msg, load_stmulti);
}
