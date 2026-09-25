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
#include "common/icoenc.h"

#define MAX_ICO_FILE (128L * 1024L * 1024L)

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

static LONG load_ico(Class *cl, Object *obj, const struct request *request)
{
    struct ico_entry entry;
    struct ico_image image;
    Point grab;
    UBYTE *input;
    ULONG width, height;
    LONG size, error;
    unsigned count = 0, index = 0;
    int cursor = 0;

    memset(&entry, 0, sizeof entry);
    error = dt_read_file(obj, 6, MAX_ICO_FILE, &input, &size);
    if (error != 0 || input == NULL)
        return error;
    error = dt_error(ico_directory(input, (size_t)size, &count, &cursor));
    if (error == 0 && request->count != NULL)
        *request->count = count;
    if (error == 0) {
        if (request->has_index) {
            index = request->index;
            error = dt_error(ico_entry(input, (size_t)size, index, &entry));
        } else {
            error = dt_error(ico_best(input, (size_t)size, &index));
            if (error == 0)
                error = dt_error(ico_entry(input, (size_t)size, index, &entry));
        }
    }
    if (error == 0) {
        if (entry.png) {
            error = dt_load_embedded(input + entry.offset, (ULONG)entry.size, "png",
                                     &image.rgba, &width, &height);
            if (error == 0) {
                error = dt_put_rgba(cl, obj, image.rgba, width, height);
                FreeVec(image.rgba);
            }
        } else {
            error = dt_error(ico_decode_bmp(input, (size_t)size, &entry, &image));
            if (error == 0) {
                error = dt_put_rgba(cl, obj, image.rgba, image.width, image.height);
                ico_free(&image);
            }
        }
    }
    FreeVec(input);
    if (error != 0)
        return error;
    SetDTAttrs(obj, NULL, NULL, PDTA_WhichPicture, index,
               PDTA_GetNumPictures, count, TAG_END);
    /* A cursor's hotspot is what IFF calls the grab point. */
    if (cursor && entry.hot_x <= 0x7fff && entry.hot_y <= 0x7fff) {
        grab.x = (WORD)entry.hot_x;
        grab.y = (WORD)entry.hot_y;
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

IPTR ICO__OM_NEW(Class *cl, Object *obj, struct opSet *msg)
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
    error = load_ico(cl, (Object *)created, &request);
    if (error != 0) {
        CoerceMethod(cl, (Object *)created, OM_DISPOSE);
        SetIoErr(error);
        return 0;
    }
    return created;
}

IPTR ICO__DTM_WRITE(Class *cl, Object *obj, struct dtWrite *msg)
{
    struct collector c;
    Point *grab = NULL;
    ULONG width, height;
    UBYTE *out;
    size_t capacity, size;
    BOOL cursor = FALSE;
    IPTR success = FALSE;

    if (msg->dtw_Mode != DTWM_RAW)
        return DoSuperMethodA(cl, obj, (Msg)msg);
    if (msg->dtw_FileHandle == BNULL)
        return TRUE;
    if (!dt_picture_size(obj, &width, &height))
        return FALSE;
    capacity = ico_encode_capacity(width, height);
    if (capacity == 0) {
        SetIoErr(ERROR_OBJECT_TOO_LARGE);
        return FALSE;
    }
    /* A grab point other than 0,0 is saved as a cursor's hotspot. */
    GetDTAttrs(obj, PDTA_Grab, &grab, TAG_END);
    if (grab != NULL && (grab->x != 0 || grab->y != 0) &&
        grab->x >= 0 && grab->y >= 0 &&
        (ULONG)grab->x < width && (ULONG)grab->y < height)
        cursor = TRUE;
    c.rgba = AllocVec(width * height * 4u, MEMF_ANY);
    out = AllocVec(capacity, MEMF_ANY);
    c.y = 0;
    if (c.rgba != NULL && out != NULL && dt_each_row(cl, obj, collect_row, &c)) {
        size = ico_encode(c.rgba, width, height, cursor,
                          cursor ? (unsigned)grab->x : 0,
                          cursor ? (unsigned)grab->y : 0, out, capacity);
        success = size != 0 && dt_write(msg->dtw_FileHandle, out, (LONG)size);
    }
    FreeVec(out);
    FreeVec(c.rgba);
    return success;
}
