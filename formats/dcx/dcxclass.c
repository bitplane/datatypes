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

#define MAX_DCX_FILE (256L * 1024L * 1024L)

ADD2LIBS((const UBYTE *)"datatypes/picture.datatype", 0, struct Library *, PictureBase);

struct writer {
    BPTR file;
    UBYTE *out;
    size_t capacity;
};

/* Load page index; count, if given, receives the number of pages. */
static LONG load_dcx(Class *cl, Object *obj, ULONG index, ULONG *count)
{
    struct pcx_image image;
    UBYTE *input;
    LONG size, error;
    unsigned pages;

    error = dt_read_file(obj, 12, MAX_DCX_FILE, &input, &size);
    if (error != 0 || input == NULL)
        return error;
    error = dt_error(dcx_count(input, (size_t)size, &pages));
    if (error == 0) {
        if (count != NULL)
            *count = pages;
        /* The superclass kept the count pointer from the tags; store the count. */
        SetDTAttrs(obj, NULL, NULL, PDTA_GetNumPictures, (ULONG)pages, TAG_END);
        error = dt_error(dcx_decode(input, (size_t)size, (unsigned)index, &image));
    }
    FreeVec(input);
    if (error != 0)
        return error;
    error = dt_put_rgba(cl, obj, image.rgba, image.width, image.height);
    pcx_free(&image);
    if (error == 0)
        dt_set_name(obj);
    return error;
}

static BOOL write_row(void *state, const UBYTE *rgba, ULONG width)
{
    struct writer *w = state;
    size_t size = pcx_encode_row(rgba, width, w->out, w->capacity);
    return size != 0 && dt_write(w->file, w->out, (LONG)size);
}

IPTR DCX__OM_NEW(Class *cl, Object *obj, struct opSet *msg)
{
    ULONG index = GetTagData(PDTA_WhichPicture, 0, msg->ops_AttrList);
    ULONG *count = (ULONG *)GetTagData(PDTA_GetNumPictures, 0,
                                       msg->ops_AttrList);
    IPTR created = DoSuperMethodA(cl, obj, (Msg)msg);
    LONG error;

    if (created == 0)
        return 0;
    error = load_dcx(cl, (Object *)created, index, count);
    if (error != 0) {
        CoerceMethod(cl, (Object *)created, OM_DISPOSE);
        SetIoErr(error);
        return 0;
    }
    return created;
}

IPTR DCX__DTM_WRITE(Class *cl, Object *obj, struct dtWrite *msg)
{
    UBYTE directory[DCX_DIRECTORY_SIZE];
    UBYTE header[128];
    struct writer w;
    ULONG width, height;
    IPTR success;

    if (msg->dtw_Mode != DTWM_RAW)
        return DoSuperMethodA(cl, obj, (Msg)msg);
    /* MultiView probes RAW support without opening an output file. */
    if (msg->dtw_FileHandle == BNULL)
        return TRUE;
    if (!dt_picture_size(obj, &width, &height) ||
        !pcx_make_header(width, height, header))
        return FALSE;
    dcx_make_directory(directory);
    w.file = msg->dtw_FileHandle;
    w.capacity = (size_t)((width + 1u) & ~1u) * 6u;
    w.out = AllocVec(w.capacity, MEMF_ANY);
    if (w.out == NULL)
        return FALSE;
    success = dt_write(w.file, directory, sizeof directory) &&
              dt_write(w.file, header, sizeof header) &&
              dt_each_row(cl, obj, write_row, &w);
    FreeVec(w.out);
    return success;
}
