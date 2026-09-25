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

/* Mip chains, cube maps and arrays make textures larger than their image. */
#define MAX_DDS_FILE (256L * 1024L * 1024L)

ADD2LIBS((const UBYTE *)"datatypes/picture.datatype", 0, struct Library *, PictureBase);

struct writer {
    BPTR file;
    UBYTE *out;
    int alpha;
};

/* Load image index; count, if given, receives the number of images. */
static LONG load_dds(Class *cl, Object *obj, ULONG index, ULONG *count)
{
    struct dds_image image;
    unsigned long images = 0;
    UBYTE *input;
    LONG size, error;

    error = dt_read_file(obj, 4, MAX_DDS_FILE, &input, &size);
    if (error != 0 || input == NULL)
        return error;
    dds_count(input, (size_t)size, &images);
    if (count != NULL)
        *count = images;
    /* The superclass kept the count pointer from the tags; store the count. */
    SetDTAttrs(obj, NULL, NULL, PDTA_GetNumPictures, images, TAG_END);
    error = dt_error(dds_decode(input, (size_t)size, index, &image));
    FreeVec(input);
    if (error != 0)
        return error;
    error = dt_put_rgba(cl, obj, image.rgba, image.width, image.height);
    dds_free(&image);
    if (error == 0)
        dt_set_name(obj);
    return error;
}

static BOOL measure_row(void *state, const UBYTE *rgba, ULONG width)
{
    struct writer *w = state;
    w->alpha |= dds_row_has_alpha(rgba, width);
    return TRUE;
}

static BOOL write_row(void *state, const UBYTE *rgba, ULONG width)
{
    struct writer *w = state;
    dds_encode_row(rgba, width, w->alpha, w->out);
    return dt_write(w->file, w->out, (LONG)(width * (w->alpha ? 4u : 3u)));
}

IPTR Dds__OM_NEW(Class *cl, Object *obj, struct opSet *msg)
{
    ULONG index = GetTagData(PDTA_WhichPicture, 0, msg->ops_AttrList);
    ULONG *count = (ULONG *)GetTagData(PDTA_GetNumPictures, 0,
                                       msg->ops_AttrList);
    IPTR created = DoSuperMethodA(cl, obj, (Msg)msg);
    LONG error;

    if (created == 0)
        return 0;
    error = load_dds(cl, (Object *)created, index, count);
    if (error != 0) {
        CoerceMethod(cl, (Object *)created, OM_DISPOSE);
        SetIoErr(error);
        return 0;
    }
    return created;
}

IPTR Dds__DTM_WRITE(Class *cl, Object *obj, struct dtWrite *msg)
{
    UBYTE header[DDS_HEADER_SIZE];
    struct writer w = { BNULL, NULL, 0 };
    ULONG width, height;
    IPTR success;

    if (msg->dtw_Mode != DTWM_RAW)
        return DoSuperMethodA(cl, obj, (Msg)msg);
    /* MultiView probes RAW support without opening an output file. */
    if (msg->dtw_FileHandle == BNULL)
        return TRUE;
    if (!dt_picture_size(obj, &width, &height))
        return FALSE;
    /* 32-bit only when some pixel isn't opaque, so look at every pixel first. */
    if (!dt_each_row(cl, obj, measure_row, &w) ||
        !dds_make_header(width, height, w.alpha, header))
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
