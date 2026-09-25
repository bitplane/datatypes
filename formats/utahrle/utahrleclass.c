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

#include <string.h>

#include "common/dtfile.h"
#include "common/dtpicture.h"
#include "decode.h"
#include "encode.h"

#define MAX_UTAHRLE_FILE (128L * 1024L * 1024L)

ADD2LIBS((const UBYTE *)"datatypes/picture.datatype", 0, struct Library *, PictureBase);

struct copier {
    UBYTE *pixels;
    ULONG row;
    ULONG needs;
};

/* Load image index; count, if given, receives the number of images. */
static LONG load_utahrle(Class *cl, Object *obj, ULONG index, ULONG *count)
{
    struct utahrle_image image;
    UBYTE *input;
    LONG size, error;
    unsigned images;

    error = dt_read_file(obj, 16, MAX_UTAHRLE_FILE, &input, &size);
    if (error != 0 || input == NULL)
        return error;
    error = dt_error(utahrle_count(input, (size_t)size, &images));
    if (error == 0) {
        if (count != NULL)
            *count = images;
        /* The superclass kept the count pointer from the tags; store the count. */
        SetDTAttrs(obj, NULL, NULL, PDTA_GetNumPictures, (ULONG)images, TAG_END);
        error = dt_error(utahrle_decode(input, (size_t)size, (unsigned)index,
                                        &image));
    }
    FreeVec(input);
    if (error != 0)
        return error;
    error = dt_put_rgba(cl, obj, image.rgba, image.width, image.height);
    utahrle_free(&image);
    if (error == 0)
        dt_set_name(obj);
    return error;
}

static BOOL copy_row(void *state, const UBYTE *rgba, ULONG width)
{
    struct copier *c = state;

    c->needs |= utahrle_row_needs(rgba, width);
    memcpy(c->pixels + (size_t)c->row++ * width * 4u, rgba, (size_t)width * 4u);
    return TRUE;
}

IPTR UtahRLE__OM_NEW(Class *cl, Object *obj, struct opSet *msg)
{
    ULONG index = GetTagData(PDTA_WhichPicture, 0, msg->ops_AttrList);
    ULONG *count = (ULONG *)GetTagData(PDTA_GetNumPictures, 0,
                                       msg->ops_AttrList);
    IPTR created = DoSuperMethodA(cl, obj, (Msg)msg);
    LONG error;

    if (created == 0)
        return 0;
    error = load_utahrle(cl, (Object *)created, index, count);
    if (error != 0) {
        CoerceMethod(cl, (Object *)created, OM_DISPOSE);
        SetIoErr(error);
        return 0;
    }
    return created;
}

IPTR UtahRLE__DTM_WRITE(Class *cl, Object *obj, struct dtWrite *msg)
{
    UBYTE header[UTAHRLE_HEADER_SIZE];
    struct copier c = { NULL, 0, 0 };
    UBYTE *out = NULL;
    ULONG width, height, y;
    size_t capacity, size;
    IPTR success = FALSE;

    if (msg->dtw_Mode != DTWM_RAW)
        return DoSuperMethodA(cl, obj, (Msg)msg);
    /* MultiView probes RAW support without opening an output file. */
    if (msg->dtw_FileHandle == BNULL)
        return TRUE;
    if (!dt_picture_size(obj, &width, &height))
        return FALSE;
    capacity = utahrle_row_capacity(width);
    out = AllocVec(capacity, MEMF_ANY);
    /* Rows are stored bottom up, so take the whole picture first. The pixel
       content also picks the channels: gray, RGB, or RGB with alpha. */
    c.pixels = AllocVec((size_t)width * height * 4u, MEMF_ANY);
    if (out == NULL || c.pixels == NULL ||
        !dt_each_row(cl, obj, copy_row, &c) ||
        !utahrle_make_header(width, height, c.needs, header) ||
        !dt_write(msg->dtw_FileHandle, header, sizeof header))
        goto done;
    for (y = height; y-- > 0;) {
        size = utahrle_encode_row(c.pixels + (size_t)y * width * 4u, width,
                                  c.needs, y == height - 1u, out, capacity);
        if (size == 0 || !dt_write(msg->dtw_FileHandle, out, (LONG)size))
            goto done;
    }
    success = dt_write(msg->dtw_FileHandle, utahrle_end, sizeof utahrle_end);
done:
    FreeVec(c.pixels);
    FreeVec(out);
    return success;
}
