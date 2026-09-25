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

#define MAX_VIFF_FILE (256L * 1024L * 1024L)

ADD2LIBS((const UBYTE *)"datatypes/picture.datatype", 0, struct Library *, PictureBase);

struct writer {
    BPTR file;
    UBYTE *out;
    size_t capacity;
    unsigned band;
    BOOL alpha;
};

/* Load image index; count, if given, receives the number of images. */
static LONG load_viff(Class *cl, Object *obj, ULONG index, ULONG *count)
{
    struct viff_image image;
    UBYTE *input;
    LONG size, error;
    unsigned images;

    error = dt_read_file(obj, 1, MAX_VIFF_FILE, &input, &size);
    if (error != 0 || input == NULL)
        return error;
    error = dt_error(viff_count(input, (size_t)size, &images));
    if (error == 0) {
        if (count != NULL)
            *count = images;
        /* The superclass kept the count pointer from the tags; store the count. */
        SetDTAttrs(obj, NULL, NULL, PDTA_GetNumPictures, (ULONG)images, TAG_END);
        error = dt_error(viff_decode(input, (size_t)size, (unsigned)index, &image));
    }
    FreeVec(input);
    if (error != 0)
        return error;
    error = dt_put_rgba(cl, obj, image.rgba, image.width, image.height);
    viff_free(&image);
    if (error == 0)
        dt_set_name(obj);
    return error;
}

static BOOL find_alpha(void *state, const UBYTE *rgba, ULONG width)
{
    struct writer *w = state;
    if (viff_row_has_alpha(rgba, width))
        w->alpha = TRUE;
    return TRUE;
}

static BOOL write_band(void *state, const UBYTE *rgba, ULONG width)
{
    struct writer *w = state;
    size_t size = viff_encode_band(rgba, width, w->band, w->out, w->capacity);
    return size != 0 && dt_write(w->file, w->out, (LONG)size);
}

IPTR VIFF__OM_NEW(Class *cl, Object *obj, struct opSet *msg)
{
    ULONG index = GetTagData(PDTA_WhichPicture, 0, msg->ops_AttrList);
    ULONG *count = (ULONG *)GetTagData(PDTA_GetNumPictures, 0,
                                       msg->ops_AttrList);
    IPTR created = DoSuperMethodA(cl, obj, (Msg)msg);
    LONG error;

    if (created == 0)
        return 0;
    error = load_viff(cl, (Object *)created, index, count);
    if (error != 0) {
        CoerceMethod(cl, (Object *)created, OM_DISPOSE);
        SetIoErr(error);
        return 0;
    }
    return created;
}

IPTR VIFF__DTM_WRITE(Class *cl, Object *obj, struct dtWrite *msg)
{
    UBYTE header[VIFF_WRITE_HEADER];
    struct writer w;
    ULONG width, height;
    unsigned bands;
    IPTR success;

    if (msg->dtw_Mode != DTWM_RAW)
        return DoSuperMethodA(cl, obj, (Msg)msg);
    /* MultiView probes RAW support without opening an output file. */
    if (msg->dtw_FileHandle == BNULL)
        return TRUE;
    if (!dt_picture_size(obj, &width, &height))
        return FALSE;
    w.file = msg->dtw_FileHandle;
    w.capacity = width;
    w.band = 0;
    w.alpha = FALSE;
    /* Bands are stored whole, one after another: one pass over the rows each. */
    if (!dt_each_row(cl, obj, find_alpha, &w))
        return FALSE;
    bands = w.alpha ? 4u : 3u;
    if (!viff_make_header(width, height, bands, header))
        return FALSE;
    w.out = AllocVec(w.capacity, MEMF_ANY);
    if (w.out == NULL)
        return FALSE;
    success = dt_write(w.file, header, sizeof header);
    for (w.band = 0; success && w.band < bands; w.band++)
        success = dt_each_row(cl, obj, write_band, &w);
    FreeVec(w.out);
    return success;
}
