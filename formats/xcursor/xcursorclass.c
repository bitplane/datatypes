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

#define MAX_XCURSOR_FILE (256L * 1024L * 1024L)

ADD2LIBS((const UBYTE *)"datatypes/picture.datatype", 0, struct Library *, PictureBase);

struct writer {
    BPTR file;
    UBYTE *out;
};

static LONG load_xcursor(Class *cl, Object *obj, long index, ULONG *count_out)
{
    struct xcursor_image image;
    UBYTE *input;
    LONG size, error;
    unsigned count;

    error = dt_read_file(obj, 16, MAX_XCURSOR_FILE, &input, &size);
    if (error != 0 || input == NULL)
        return error;
    error = dt_error(xcursor_decode(input, (size_t)size, index, &image, &count));
    FreeVec(input);
    if (count_out != NULL)
        *count_out = count;
    if (error != 0)
        return error;
    error = dt_put_rgba(cl, obj, image.rgba, image.width, image.height);
    xcursor_free(&image);
    if (error == 0) {
        SetDTAttrs(obj, NULL, NULL, PDTA_GetNumPictures, (IPTR)count, TAG_END);
        dt_set_name(obj);
    }
    return error;
}

static BOOL write_row(void *state, const UBYTE *rgba, ULONG width)
{
    struct writer *w = state;
    xcursor_encode_row(rgba, width, w->out);
    return dt_write(w->file, w->out, (LONG)(width * 4u));
}

/* dt_new doesn't pass OM_NEW's tags on, so pick out the image here. */
IPTR XCURSOR__OM_NEW(Class *cl, Object *obj, struct opSet *msg)
{
    struct TagItem *which = FindTagItem(PDTA_WhichPicture, msg->ops_AttrList);
    ULONG *count = (ULONG *)GetTagData(PDTA_GetNumPictures, 0, msg->ops_AttrList);
    long index = which != NULL ? (long)which->ti_Data : XCURSOR_BEST;
    IPTR created = DoSuperMethodA(cl, obj, (Msg)msg);
    LONG error;

    if (created == 0)
        return 0;
    error = load_xcursor(cl, (Object *)created, index, count);
    if (error != 0) {
        CoerceMethod(cl, (Object *)created, OM_DISPOSE);
        SetIoErr(error);
        return 0;
    }
    return created;
}

IPTR XCURSOR__DTM_WRITE(Class *cl, Object *obj, struct dtWrite *msg)
{
    UBYTE header[XCURSOR_ENCODED_HEADER];
    struct writer w;
    ULONG width, height;
    IPTR success;

    if (msg->dtw_Mode != DTWM_RAW)
        return DoSuperMethodA(cl, obj, (Msg)msg);
    /* MultiView probes RAW support without opening an output file. */
    if (msg->dtw_FileHandle == BNULL)
        return TRUE;
    if (!dt_picture_size(obj, &width, &height) ||
        !xcursor_make_header(width, height, header))
        return FALSE;
    w.file = msg->dtw_FileHandle;
    w.out = AllocVec(width * 4u, MEMF_ANY);
    if (w.out == NULL)
        return FALSE;
    success = dt_write(w.file, header, sizeof header) &&
              dt_each_row(cl, obj, write_row, &w);
    FreeVec(w.out);
    return success;
}
