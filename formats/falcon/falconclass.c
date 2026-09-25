#include <aros/symbolsets.h>
#include <datatypes/datatypes.h>
#include <datatypes/datatypesclass.h>
#include <datatypes/pictureclass.h>
#include <dos/dos.h>
#include <exec/memory.h>
#include <proto/alib.h>
#include <proto/datatypes.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/utility.h>

#include <string.h>

#include "common/dtfile.h"
#include "common/dtpicture.h"
#include "decode.h"
#include "encode.h"

/* 16M pixels of 24-bit Prism Paint, the largest variant, plus a palette. */
#define MAX_FALCON_FILE (64L * 1024L * 1024L)

ADD2LIBS((const UBYTE *)"datatypes/picture.datatype", 0, struct Library *, PictureBase);

struct request {
    ULONG index;
    ULONG *count;
};

struct collector {
    UBYTE *rgba;
    ULONG row;
};

static LONG load_falcon(Class *cl, Object *obj, const struct request *request)
{
    struct falcon_image image;
    UBYTE *input;
    LONG size, error;
    unsigned count = 0;

    error = dt_read_file(obj, 4, MAX_FALCON_FILE, &input, &size);
    if (error != 0 || input == NULL)
        return error;
    error = dt_error(falcon_decode(input, (size_t)size, request->index,
                                   &image, &count));
    FreeVec(input);
    if (count != 0 && request->count != NULL)
        *request->count = count;
    if (error != 0)
        return error;
    error = dt_put_rgba(cl, obj, image.rgba, image.width, image.height);
    falcon_free(&image);
    if (error != 0)
        return error;
    SetDTAttrs(obj, NULL, NULL, PDTA_WhichPicture, request->index,
               PDTA_GetNumPictures, count, TAG_END);
    dt_set_name(obj);
    return 0;
}

static BOOL collect_row(void *state, const UBYTE *rgba, ULONG width)
{
    struct collector *c = state;
    memcpy(c->rgba + (size_t)c->row++ * width * 4u, rgba, width * 4u);
    return TRUE;
}

IPTR Falcon__OM_NEW(Class *cl, Object *obj, struct opSet *msg)
{
    struct request request;
    struct TagItem *tag;
    IPTR created;
    LONG error;

    /* dt_new doesn't pass tags on, so the picture index is read here. */
    tag = FindTagItem(PDTA_WhichPicture, msg->ops_AttrList);
    request.index = tag != NULL ? (ULONG)tag->ti_Data : 0;
    tag = FindTagItem(PDTA_GetNumPictures, msg->ops_AttrList);
    request.count = tag != NULL ? (ULONG *)tag->ti_Data : NULL;
    created = DoSuperMethodA(cl, obj, (Msg)msg);
    if (created == 0)
        return 0;
    error = load_falcon(cl, (Object *)created, &request);
    if (error != 0) {
        CoerceMethod(cl, (Object *)created, OM_DISPOSE);
        SetIoErr(error);
        return 0;
    }
    return created;
}

IPTR Falcon__DTM_WRITE(Class *cl, Object *obj, struct dtWrite *msg)
{
    struct collector c;
    UBYTE *output;
    ULONG width, height;
    size_t size;
    IPTR success = FALSE;

    if (msg->dtw_Mode != DTWM_RAW)
        return DoSuperMethodA(cl, obj, (Msg)msg);
    /* MultiView probes RAW support without opening an output file. */
    if (msg->dtw_FileHandle == BNULL)
        return TRUE;
    if (!dt_picture_size(obj, &width, &height))
        return FALSE;
    size = falcon_encode_size(width, height);
    if (size == 0) {
        SetIoErr(ERROR_OBJECT_TOO_LARGE);
        return FALSE;
    }
    c.rgba = AllocVec(width * height * 4u, MEMF_ANY);
    output = AllocVec(size, MEMF_ANY);
    c.row = 0;
    if (c.rgba != NULL && output != NULL && dt_each_row(cl, obj, collect_row, &c)) {
        LONG error = dt_error(falcon_encode(c.rgba, width, height, output, size));
        if (error == 0)
            success = dt_write(msg->dtw_FileHandle, output, (LONG)size);
        else
            SetIoErr(error);
    } else if (c.rgba == NULL || output == NULL) {
        SetIoErr(ERROR_NO_FREE_STORE);
    }
    FreeVec(output);
    FreeVec(c.rgba);
    return success;
}
