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
#include "encode.h"

/* Header plus 16M pixels of 8 bytes each. */
#define MAX_FARBFELD_FILE (16L + 128L * 1024L * 1024L)

ADD2LIBS((const UBYTE *)"datatypes/picture.datatype", 0, struct Library *, PictureBase);

struct writer {
    BPTR file;
    UBYTE *out;
};

static LONG load_farbfeld(Class *cl, Object *obj)
{
    struct farbfeld_image image;
    UBYTE *input;
    LONG size, error;

    error = dt_read_file(obj, 16, MAX_FARBFELD_FILE, &input, &size);
    if (error != 0 || input == NULL)
        return error;
    error = dt_error(farbfeld_decode(input, (size_t)size, &image));
    FreeVec(input);
    if (error != 0)
        return error;
    error = dt_put_rgba(cl, obj, image.rgba, image.width, image.height);
    farbfeld_free(&image);
    if (error == 0)
        dt_set_name(obj);
    return error;
}

static BOOL write_row(void *state, const UBYTE *rgba, ULONG width)
{
    struct writer *w = state;
    farbfeld_encode_row(rgba, width, w->out);
    return dt_write(w->file, w->out, (LONG)(width * 8u));
}

IPTR Farbfeld__OM_NEW(Class *cl, Object *obj, struct opSet *msg)
{
    return dt_new(cl, obj, msg, load_farbfeld);
}

IPTR Farbfeld__DTM_WRITE(Class *cl, Object *obj, struct dtWrite *msg)
{
    UBYTE header[16];
    struct writer w;
    ULONG width, height;
    IPTR success;

    if (msg->dtw_Mode != DTWM_RAW)
        return DoSuperMethodA(cl, obj, (Msg)msg);
    /* MultiView probes RAW support without opening an output file. */
    if (msg->dtw_FileHandle == BNULL)
        return TRUE;
    if (!dt_picture_size(obj, &width, &height) ||
        !farbfeld_make_header(width, height, header))
        return FALSE;
    w.file = msg->dtw_FileHandle;
    w.out = AllocVec(width * 8u, MEMF_ANY);
    if (w.out == NULL)
        return FALSE;
    success = dt_write(w.file, header, sizeof header) &&
              dt_each_row(cl, obj, write_row, &w);
    FreeVec(w.out);
    return success;
}
