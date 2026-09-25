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

/* Several images of up to 16M pixels each, 3 bytes per pixel. */
#define MAX_MTV_FILE (256L * 1024L * 1024L)

ADD2LIBS((const UBYTE *)"datatypes/picture.datatype", 0, struct Library *, PictureBase);

struct writer {
    BPTR file;
    UBYTE *out;
};

/* Load image index; count, if given, receives the number of images. */
static LONG load_mtv(Class *cl, Object *obj, ULONG index, ULONG *count)
{
    struct mtv_image image;
    UBYTE *input;
    LONG size, error;
    ULONG images;

    error = dt_read_file(obj, 4, MAX_MTV_FILE, &input, &size);
    if (error != 0 || input == NULL)
        return error;
    images = mtv_count(input, (size_t)size);
    if (count != NULL)
        *count = images;
    /* The superclass kept the count pointer from the tags; store the count. */
    SetDTAttrs(obj, NULL, NULL, PDTA_GetNumPictures, images, TAG_END);
    error = dt_error(mtv_decode(input, (size_t)size, (unsigned)index, &image));
    FreeVec(input);
    if (error != 0)
        return error;
    error = dt_put_rgba(cl, obj, image.rgba, image.width, image.height);
    mtv_free(&image);
    if (error == 0)
        dt_set_name(obj);
    return error;
}

static BOOL write_row(void *state, const UBYTE *rgba, ULONG width)
{
    struct writer *w = state;
    mtv_encode_row(rgba, width, w->out);
    return dt_write(w->file, w->out, (LONG)(width * 3u));
}

IPTR Mtv__OM_NEW(Class *cl, Object *obj, struct opSet *msg)
{
    ULONG index = GetTagData(PDTA_WhichPicture, 0, msg->ops_AttrList);
    ULONG *count = (ULONG *)GetTagData(PDTA_GetNumPictures, 0,
                                       msg->ops_AttrList);
    IPTR created = DoSuperMethodA(cl, obj, (Msg)msg);
    LONG error;

    if (created == 0)
        return 0;
    error = load_mtv(cl, (Object *)created, index, count);
    if (error != 0) {
        CoerceMethod(cl, (Object *)created, OM_DISPOSE);
        SetIoErr(error);
        return 0;
    }
    return created;
}

IPTR Mtv__DTM_WRITE(Class *cl, Object *obj, struct dtWrite *msg)
{
    UBYTE header[MTV_HEADER_CAPACITY];
    struct writer w;
    ULONG width, height;
    size_t header_size;
    IPTR success;

    if (msg->dtw_Mode != DTWM_RAW)
        return DoSuperMethodA(cl, obj, (Msg)msg);
    /* MultiView probes RAW support without opening an output file. */
    if (msg->dtw_FileHandle == BNULL)
        return TRUE;
    if (!dt_picture_size(obj, &width, &height))
        return FALSE;
    header_size = mtv_make_header(width, height, header);
    if (header_size == 0)
        return FALSE;
    w.file = msg->dtw_FileHandle;
    w.out = AllocVec(width * 3u, MEMF_ANY);
    if (w.out == NULL)
        return FALSE;
    success = dt_write(w.file, header, (LONG)header_size) &&
              dt_each_row(cl, obj, write_row, &w);
    FreeVec(w.out);
    return success;
}
