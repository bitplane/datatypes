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

#define MAX_INFO_FILE (64L * 1024L * 1024L)

ADD2LIBS((const UBYTE *)"datatypes/picture.datatype", 0, struct Library *, PictureBase);

struct request {
    BOOL has_index;
    ULONG index;
    ULONG *count;
};

struct collector {
    UBYTE *rgba;
    ULONG y;
};

static LONG load_info(Class *cl, Object *obj, const struct request *request)
{
    struct info_icon icon;
    struct info_image image;
    UBYTE *input;
    LONG size, error;
    unsigned index = 0;

    error = dt_read_file(obj, 2, MAX_INFO_FILE, &input, &size);
    if (error != 0 || input == NULL)
        return error;
    error = dt_error(info_parse(input, (size_t)size, &icon));
    if (error == 0 && request->count != NULL)
        *request->count = icon.count;
    if (error == 0) {
        index = request->has_index ? request->index : info_best(&icon);
        error = dt_error(info_decode(input, (size_t)size, &icon, index, &image));
    }
    FreeVec(input);
    if (error != 0)
        return error;
    error = dt_put_rgba(cl, obj, image.rgba, image.width, image.height);
    info_free(&image);
    if (error != 0)
        return error;
    SetDTAttrs(obj, NULL, NULL, PDTA_WhichPicture, index,
               PDTA_GetNumPictures, icon.count, TAG_END);
    dt_set_name(obj);
    return 0;
}

static BOOL collect_row(void *state, const UBYTE *rgba, ULONG width)
{
    struct collector *c = state;
    CopyMem((APTR)rgba, c->rgba + (size_t)c->y++ * width * 4u, width * 4u);
    return TRUE;
}

IPTR INFO__OM_NEW(Class *cl, Object *obj, struct opSet *msg)
{
    struct request request;
    struct TagItem *tag;
    IPTR created;
    LONG error;

    /* dt_new doesn't pass tags on, so the picture index is read here. */
    tag = FindTagItem(PDTA_WhichPicture, msg->ops_AttrList);
    request.has_index = tag != NULL;
    request.index = tag != NULL ? (ULONG)tag->ti_Data : 0;
    tag = FindTagItem(PDTA_GetNumPictures, msg->ops_AttrList);
    request.count = tag != NULL ? (ULONG *)tag->ti_Data : NULL;
    created = DoSuperMethodA(cl, obj, (Msg)msg);
    if (created == 0)
        return 0;
    error = load_info(cl, (Object *)created, &request);
    if (error != 0) {
        CoerceMethod(cl, (Object *)created, OM_DISPOSE);
        SetIoErr(error);
        return 0;
    }
    return created;
}

IPTR INFO__DTM_WRITE(Class *cl, Object *obj, struct dtWrite *msg)
{
    struct collector c;
    ULONG width, height;
    UBYTE *out;
    size_t capacity, size;
    IPTR success = FALSE;

    if (msg->dtw_Mode != DTWM_RAW)
        return DoSuperMethodA(cl, obj, (Msg)msg);
    if (msg->dtw_FileHandle == BNULL)
        return TRUE;
    if (!dt_picture_size(obj, &width, &height))
        return FALSE;
    capacity = info_encode_capacity(width, height);
    if (capacity == 0) {
        SetIoErr(ERROR_OBJECT_TOO_LARGE);
        return FALSE;
    }
    c.rgba = AllocVec(width * height * 4u, MEMF_ANY);
    out = AllocVec(capacity, MEMF_ANY);
    c.y = 0;
    if (c.rgba != NULL && out != NULL && dt_each_row(cl, obj, collect_row, &c)) {
        size = info_encode(c.rgba, width, height, out, capacity);
        success = size != 0 && dt_write(msg->dtw_FileHandle, out, (LONG)size);
    }
    FreeVec(out);
    FreeVec(c.rgba);
    return success;
}
