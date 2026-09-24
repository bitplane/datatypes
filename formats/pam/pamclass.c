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

#define MAX_PAM_FILE (256L * 1024L * 1024L)

ADD2LIBS((const UBYTE *)"datatypes/picture.datatype", 0, struct Library *, PictureBase);

struct writer {
    BPTR file;
    UBYTE *out;
    size_t capacity;
    unsigned needs, channels;
};

/* Load image index; count, if given, receives the number of images. */
static LONG load_pam(Class *cl, Object *obj, ULONG index, ULONG *count)
{
    struct pam_image image;
    UBYTE *input;
    LONG size, error;
    ULONG images;

    error = dt_read_file(obj, 3, MAX_PAM_FILE, &input, &size);
    if (error != 0 || input == NULL)
        return error;
    images = pam_count(input, (size_t)size);
    if (count != NULL)
        *count = images;
    /* The superclass kept the count pointer from the tags; store the count. */
    SetDTAttrs(obj, NULL, NULL, PDTA_GetNumPictures, images, TAG_END);
    error = dt_error(pam_decode(input, (size_t)size, (unsigned)index, &image));
    FreeVec(input);
    if (error != 0)
        return error;
    error = dt_put_rgba(cl, obj, image.rgba, image.width, image.height);
    pam_free(&image);
    if (error == 0)
        dt_set_name(obj);
    return error;
}

static BOOL measure_row(void *state, const UBYTE *rgba, ULONG width)
{
    struct writer *w = state;
    w->needs |= pam_row_needs(rgba, width);
    return TRUE;
}

static BOOL write_row(void *state, const UBYTE *rgba, ULONG width)
{
    struct writer *w = state;
    size_t size = pam_encode_row(rgba, width, w->channels, w->out, w->capacity);
    return size != 0 && dt_write(w->file, w->out, (LONG)size);
}

IPTR Pam__OM_NEW(Class *cl, Object *obj, struct opSet *msg)
{
    ULONG index = GetTagData(PDTA_WhichPicture, 0, msg->ops_AttrList);
    ULONG *count = (ULONG *)GetTagData(PDTA_GetNumPictures, 0,
                                       msg->ops_AttrList);
    IPTR created = DoSuperMethodA(cl, obj, (Msg)msg);
    LONG error;

    if (created == 0)
        return 0;
    error = load_pam(cl, (Object *)created, index, count);
    if (error != 0) {
        CoerceMethod(cl, (Object *)created, OM_DISPOSE);
        SetIoErr(error);
        return 0;
    }
    return created;
}

IPTR Pam__DTM_WRITE(Class *cl, Object *obj, struct dtWrite *msg)
{
    UBYTE header[PAM_HEADER_CAPACITY];
    struct writer w = { BNULL, NULL, 0, 0, 0 };
    ULONG width, height;
    size_t header_size;
    IPTR success = FALSE;

    if (msg->dtw_Mode != DTWM_RAW)
        return DoSuperMethodA(cl, obj, (Msg)msg);
    /* MultiView probes RAW support without opening an output file. */
    if (msg->dtw_FileHandle == BNULL)
        return TRUE;
    if (!dt_picture_size(obj, &width, &height))
        return FALSE;
    /* The header names the tuple type, so look at every pixel first. */
    if (!dt_each_row(cl, obj, measure_row, &w))
        return FALSE;
    w.channels = pam_channels(w.needs);
    header_size = pam_make_header(width, height, w.channels, header, sizeof header);
    if (header_size == 0)
        return FALSE;
    w.file = msg->dtw_FileHandle;
    w.capacity = (size_t)width * 4u;
    w.out = AllocVec(w.capacity, MEMF_ANY);
    if (w.out == NULL)
        return FALSE;
    success = dt_write(w.file, header, (LONG)header_size) &&
              dt_each_row(cl, obj, write_row, &w);
    FreeVec(w.out);
    return success;
}
