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

#define MAX_PALM_FILE (128L * 1024L * 1024L)

ADD2LIBS((const UBYTE *)"datatypes/picture.datatype", 0, struct Library *, PictureBase);

struct request {
    BOOL has_index;
    ULONG index;
    ULONG *count;
};

struct writer {
    struct palm_encoder *encoder;
    BPTR file;
    UBYTE *out;
    size_t capacity;
};

static LONG load_palm(Class *cl, Object *obj, const struct request *request)
{
    struct palm_image image;
    UBYTE *input;
    LONG size, error;
    unsigned count, index = 0;

    error = dt_read_file(obj, 16, MAX_PALM_FILE, &input, &size);
    if (error != 0 || input == NULL)
        return error;
    error = dt_error(palm_count(input, (size_t)size, &count));
    if (error == 0 && request->count != NULL)
        *request->count = count;
    if (error == 0) {
        if (request->has_index)
            index = request->index;
        else
            error = dt_error(palm_best(input, (size_t)size, &index));
    }
    if (error == 0)
        error = dt_error(palm_decode(input, (size_t)size, index, &image));
    FreeVec(input);
    if (error != 0)
        return error;
    error = dt_put_rgba(cl, obj, image.rgba, image.width, image.height);
    palm_free(&image);
    if (error != 0)
        return error;
    SetDTAttrs(obj, NULL, NULL, PDTA_WhichPicture, index,
               PDTA_GetNumPictures, count, TAG_END);
    dt_set_name(obj);
    return 0;
}

static BOOL scan_row(void *state, const UBYTE *rgba, ULONG width)
{
    (void)width;
    palm_encode_scan(state, rgba);
    return TRUE;
}

static BOOL write_row(void *state, const UBYTE *rgba, ULONG width)
{
    struct writer *w = state;
    size_t size;

    (void)width;
    size = palm_encode_row(w->encoder, rgba, w->out, w->capacity);
    return size != 0 && dt_write(w->file, w->out, (LONG)size);
}

IPTR Palm__OM_NEW(Class *cl, Object *obj, struct opSet *msg)
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
    error = load_palm(cl, (Object *)created, &request);
    if (error != 0) {
        CoerceMethod(cl, (Object *)created, OM_DISPOSE);
        SetIoErr(error);
        return 0;
    }
    return created;
}

IPTR Palm__DTM_WRITE(Class *cl, Object *obj, struct dtWrite *msg)
{
    struct writer w;
    ULONG width, height;
    size_t size;
    IPTR success = FALSE;

    if (msg->dtw_Mode != DTWM_RAW)
        return DoSuperMethodA(cl, obj, (Msg)msg);
    if (msg->dtw_FileHandle == BNULL)
        return TRUE;
    if (!dt_picture_size(obj, &width, &height))
        return FALSE;
    /* The encoder holds a 64K-entry colour bitmap, too big for the stack.
       The header is built in the row buffer. */
    w.capacity = (size_t)width * 2u + 2u;
    if (w.capacity < PALM_HEADER_MAX)
        w.capacity = PALM_HEADER_MAX;
    w.encoder = AllocVec(sizeof *w.encoder, MEMF_ANY);
    w.out = AllocVec(w.capacity, MEMF_ANY);
    w.file = msg->dtw_FileHandle;
    if (w.encoder != NULL && w.out != NULL) {
        palm_encode_begin(w.encoder, width, height);
        if (dt_each_row(cl, obj, scan_row, w.encoder)) {
            size = palm_encode_header(w.encoder, w.out, w.capacity);
            success = size != 0 && dt_write(w.file, w.out, (LONG)size) &&
                      dt_each_row(cl, obj, write_row, &w);
        }
    }
    FreeVec(w.out);
    FreeVec(w.encoder);
    return success;
}
