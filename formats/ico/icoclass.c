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

#include <stdio.h>
#include <string.h>

#include "common/dtfile.h"
#include "common/dtpicture.h"
#include "decode.h"
#include "encode.h"

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

/* AROS datatypes can only load files, so a PNG entry goes through a file in T:,
   as AROS's amigaguide class does for embedded objects. */
static LONG load_png(const UBYTE *data, const struct ico_entry *entry,
                     struct ico_image *image)
{
    static ULONG serial;
    char name[64];
    struct BitMapHeader *header = NULL;
    Object *png;
    BPTR file;
    BOOL written;
    LONG error = 0;
    ULONG width, height;

    snprintf(name, sizeof name, "T:ico_%lx_%lu.png",
             (unsigned long)(IPTR)FindTask(NULL), (unsigned long)++serial);
    file = Open((CONST_STRPTR)name, MODE_NEWFILE);
    if (file == BNULL)
        return IoErr() != 0 ? IoErr() : DTERROR_COULDNT_OPEN;
    written = dt_write(file, data + entry->offset, (LONG)entry->size);
    if (!Close(file))
        written = FALSE;
    png = written ? NewDTObject((APTR)name,
                                DTA_SourceType, DTST_FILE,
                                DTA_GroupID, GID_PICTURE,
                                PDTA_Remap, FALSE,
                                PDTA_DestMode, PMODE_V43,
                                TAG_END)
                  : NULL;
    if (png == NULL) {
        error = IoErr() != 0 ? IoErr() : DTERROR_INVALID_DATA;
        DeleteFile((CONST_STRPTR)name);
        return error;
    }
    GetDTAttrs(png, PDTA_BitMapHeader, &header, TAG_END);
    if (header == NULL || header->bmh_Width == 0 || header->bmh_Height == 0) {
        error = DTERROR_INVALID_DATA;
    } else {
        width = header->bmh_Width;
        height = header->bmh_Height;
        image->rgba = AllocVec(width * height * 4u, MEMF_ANY);
        if (image->rgba == NULL)
            error = ERROR_NO_FREE_STORE;
        else if (!DoMethod(png, PDTM_READPIXELARRAY, (IPTR)image->rgba,
                           PBPAFMT_RGBA, width * 4u, 0, 0, width, height))
            error = DTERROR_INVALID_DATA;
        if (error == 0) {
            image->width = width;
            image->height = height;
        } else if (image->rgba != NULL) {
            FreeVec(image->rgba);
            image->rgba = NULL;
        }
    }
    DisposeDTObject(png);
    DeleteFile((CONST_STRPTR)name);
    return error;
}

static LONG load_ico(Class *cl, Object *obj, const struct request *request)
{
    struct ico_entry entry;
    struct ico_image image;
    Point grab;
    UBYTE *input;
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
            error = load_png(input, &entry, &image);
            if (error == 0) {
                error = dt_put_rgba(cl, obj, image.rgba, image.width, image.height);
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
