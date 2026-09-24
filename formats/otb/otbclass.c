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

/* Sixteen frames of the largest padded bitmap, plus the header. */
#define MAX_OTB_FILE (36L * 1024L * 1024L)

ADD2LIBS((const UBYTE *)"datatypes/picture.datatype", 0, struct Library *, PictureBase);

struct writer {
    BPTR file;
    UBYTE *out;
    size_t capacity;
};

/* Load picture index; count, if given, receives the number of pictures. */
static LONG load_otb(Class *cl, Object *obj, ULONG index, ULONG *count)
{
    struct otb_image image;
    UBYTE *input;
    LONG size, error;
    unsigned pictures = 0;
    enum codec_result result;

    error = dt_read_file(obj, 5, MAX_OTB_FILE, &input, &size);
    if (error != 0 || input == NULL)
        return error;
    result = otb_decode(input, (size_t)size,
                        index < OTB_MAX_PICTURES ? (unsigned)index : OTB_MAX_PICTURES,
                        &pictures, &image);
    FreeVec(input);
    if (pictures != 0) {
        if (count != NULL)
            *count = pictures;
        /* The superclass kept the count pointer from the tags; store the count. */
        SetDTAttrs(obj, NULL, NULL, PDTA_GetNumPictures, pictures, TAG_END);
    }
    error = dt_error(result);
    if (error != 0)
        return error;
    error = dt_put_rgba(cl, obj, image.rgba, image.width, image.height);
    otb_free(&image);
    if (error == 0)
        dt_set_name(obj);
    return error;
}

static BOOL write_row(void *state, const UBYTE *rgba, ULONG width)
{
    struct writer *w = state;
    size_t size = otb_encode_row(rgba, width, w->out, w->capacity);
    return size != 0 && dt_write(w->file, w->out, (LONG)size);
}

IPTR OTB__OM_NEW(Class *cl, Object *obj, struct opSet *msg)
{
    ULONG index = GetTagData(PDTA_WhichPicture, 0, msg->ops_AttrList);
    ULONG *count = (ULONG *)GetTagData(PDTA_GetNumPictures, 0,
                                       msg->ops_AttrList);
    IPTR created = DoSuperMethodA(cl, obj, (Msg)msg);
    LONG error;

    if (created == 0)
        return 0;
    error = load_otb(cl, (Object *)created, index, count);
    if (error != 0) {
        CoerceMethod(cl, (Object *)created, OM_DISPOSE);
        SetIoErr(error);
        return 0;
    }
    return created;
}

IPTR OTB__DTM_WRITE(Class *cl, Object *obj, struct dtWrite *msg)
{
    UBYTE header[OTB_HEADER_MAX];
    struct writer w;
    ULONG width, height;
    size_t header_size;
    IPTR success;

    if (msg->dtw_Mode != DTWM_RAW)
        return DoSuperMethodA(cl, obj, (Msg)msg);
    /* MultiView probes RAW support without opening an output file. */
    if (msg->dtw_FileHandle == BNULL)
        return TRUE;
    if (!dt_picture_size(obj, &width, &height) ||
        (header_size = otb_make_header(width, height, header)) == 0)
        return FALSE;
    w.file = msg->dtw_FileHandle;
    w.capacity = (width + 7u) / 8u;
    w.out = AllocVec(w.capacity, MEMF_ANY);
    if (w.out == NULL)
        return FALSE;
    success = dt_write(w.file, header, (LONG)header_size) &&
              dt_each_row(cl, obj, write_row, &w);
    FreeVec(w.out);
    return success;
}
