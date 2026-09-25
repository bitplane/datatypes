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

#include "common/dtembed.h"
#include "common/dtfile.h"
#include "common/dtpicture.h"
#include "common/ico.h"
#include "decode.h"
#include "encode.h"

#define MAX_ANI_FILE (128L * 1024L * 1024L)

ADD2LIBS((const UBYTE *)"datatypes/picture.datatype", 0, struct Library *, PictureBase);

struct request {
    ULONG index;
    ULONG *count;
};

struct collector {
    UBYTE *rgba;
    ULONG y;
};

static LONG load_ani(Class *cl, Object *obj, const struct request *request)
{
    struct ani_frame frame;
    struct ico_image image;
    Point grab;
    UBYTE *input, *rgba;
    ULONG width, height;
    LONG size, error;
    unsigned count = 0;

    memset(&frame, 0, sizeof frame);
    error = dt_read_file(obj, 12, MAX_ANI_FILE, &input, &size);
    if (error != 0 || input == NULL)
        return error;
    error = dt_error(ani_count(input, (size_t)size, &count));
    if (error == 0 && request->count != NULL)
        *request->count = count;
    if (error == 0)
        error = dt_error(ani_frame(input, (size_t)size, request->index, &frame));
    if (error == 0) {
        if (frame.entry.png) {
            error = dt_load_embedded(frame.ico + frame.entry.offset, frame.entry.size,
                                     "png", &rgba, &width, &height);
            if (error == 0) {
                error = dt_put_rgba(cl, obj, rgba, width, height);
                FreeVec(rgba);
            }
        } else {
            error = dt_error(ico_decode_bmp(frame.ico, frame.length, &frame.entry, &image));
            if (error == 0) {
                error = dt_put_rgba(cl, obj, image.rgba, image.width, image.height);
                ico_free(&image);
            }
        }
    }
    FreeVec(input);
    if (error != 0)
        return error;
    SetDTAttrs(obj, NULL, NULL, PDTA_WhichPicture, request->index,
               PDTA_GetNumPictures, count, TAG_END);
    /* A cursor's hotspot is what IFF calls the grab point. */
    if (frame.cursor && frame.entry.hot_x <= 0x7fff && frame.entry.hot_y <= 0x7fff) {
        grab.x = (WORD)frame.entry.hot_x;
        grab.y = (WORD)frame.entry.hot_y;
        SetDTAttrs(obj, NULL, NULL, PDTA_Grab, &grab, TAG_END);
    }
    dt_set_name(obj);
    return 0;
}

static BOOL collect_row(void *state, const UBYTE *rgba, ULONG width)
{
    struct collector *c = state;
    CopyMem((APTR)rgba, c->rgba + (size_t)c->y++ * width * 4u, width * 4u);
    return TRUE;
}

IPTR ANI__OM_NEW(Class *cl, Object *obj, struct opSet *msg)
{
    struct request request;
    struct TagItem *tag;
    IPTR created;
    LONG error;

    /* dt_new doesn't pass tags on, so the frame index is read here. */
    tag = FindTagItem(PDTA_WhichPicture, msg->ops_AttrList);
    request.index = tag != NULL ? (ULONG)tag->ti_Data : 0;
    tag = FindTagItem(PDTA_GetNumPictures, msg->ops_AttrList);
    request.count = tag != NULL ? (ULONG *)tag->ti_Data : NULL;
    created = DoSuperMethodA(cl, obj, (Msg)msg);
    if (created == 0)
        return 0;
    error = load_ani(cl, (Object *)created, &request);
    if (error != 0) {
        CoerceMethod(cl, (Object *)created, OM_DISPOSE);
        SetIoErr(error);
        return 0;
    }
    return created;
}

IPTR ANI__DTM_WRITE(Class *cl, Object *obj, struct dtWrite *msg)
{
    struct collector c;
    Point *grab = NULL;
    ULONG width, height, hot_x = 0, hot_y = 0;
    UBYTE *out;
    size_t capacity, size;
    IPTR success = FALSE;

    if (msg->dtw_Mode != DTWM_RAW)
        return DoSuperMethodA(cl, obj, (Msg)msg);
    if (msg->dtw_FileHandle == BNULL)
        return TRUE;
    if (!dt_picture_size(obj, &width, &height))
        return FALSE;
    capacity = ani_encode_capacity(width, height);
    if (capacity == 0) {
        SetIoErr(ERROR_OBJECT_TOO_LARGE);
        return FALSE;
    }
    /* The grab point becomes the hotspot when it lies inside the picture. */
    GetDTAttrs(obj, PDTA_Grab, &grab, TAG_END);
    if (grab != NULL && grab->x >= 0 && grab->y >= 0 &&
        (ULONG)grab->x < width && (ULONG)grab->y < height) {
        hot_x = (ULONG)grab->x;
        hot_y = (ULONG)grab->y;
    }
    c.rgba = AllocVec(width * height * 4u, MEMF_ANY);
    out = AllocVec(capacity, MEMF_ANY);
    c.y = 0;
    if (c.rgba != NULL && out != NULL && dt_each_row(cl, obj, collect_row, &c)) {
        size = ani_encode(c.rgba, width, height, hot_x, hot_y, out, capacity);
        success = size != 0 && dt_write(msg->dtw_FileHandle, out, (LONG)size);
    }
    FreeVec(out);
    FreeVec(c.rgba);
    return success;
}
