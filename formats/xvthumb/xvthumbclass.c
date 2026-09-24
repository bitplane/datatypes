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

/* 16M pixels of one byte each, plus room for the header's comment lines. */
#define MAX_XVTHUMB_FILE (16L * 1024L * 1024L + 1024L * 1024L)

ADD2LIBS((const UBYTE *)"datatypes/picture.datatype", 0, struct Library *, PictureBase);

struct writer {
    BPTR file;
    UBYTE *out;
};

static LONG load_xvthumb(Class *cl, Object *obj)
{
    struct xvthumb_image image;
    UBYTE *input;
    LONG size, error;

    error = dt_read_file(obj, 6, MAX_XVTHUMB_FILE, &input, &size);
    if (error != 0 || input == NULL)
        return error;
    error = dt_error(xvthumb_decode(input, (size_t)size, &image));
    FreeVec(input);
    if (error != 0)
        return error;
    error = dt_put_rgba(cl, obj, image.rgba, image.width, image.height);
    xvthumb_free(&image);
    if (error == 0)
        dt_set_name(obj);
    return error;
}

static BOOL write_row(void *state, const UBYTE *rgba, ULONG width)
{
    struct writer *w = state;
    xvthumb_encode_row(rgba, width, w->out);
    return dt_write(w->file, w->out, (LONG)width);
}

IPTR XVThumb__OM_NEW(Class *cl, Object *obj, struct opSet *msg)
{
    return dt_new(cl, obj, msg, load_xvthumb);
}

IPTR XVThumb__DTM_WRITE(Class *cl, Object *obj, struct dtWrite *msg)
{
    char header[XVTHUMB_HEADER_MAX];
    struct writer w;
    ULONG width, height;
    size_t length;
    IPTR success;

    if (msg->dtw_Mode != DTWM_RAW)
        return DoSuperMethodA(cl, obj, (Msg)msg);
    /* MultiView probes RAW support without opening an output file. */
    if (msg->dtw_FileHandle == BNULL)
        return TRUE;
    if (!dt_picture_size(obj, &width, &height))
        return FALSE;
    length = xvthumb_make_header(width, height, header);
    if (length == 0)
        return FALSE;
    w.file = msg->dtw_FileHandle;
    w.out = AllocVec(width, MEMF_ANY);
    if (w.out == NULL)
        return FALSE;
    success = dt_write(w.file, header, (LONG)length) &&
              dt_each_row(cl, obj, write_row, &w);
    FreeVec(w.out);
    return success;
}
