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

#define MAX_SIXEL_FILE (64L * 1024L * 1024L)

ADD2LIBS((const UBYTE *)"datatypes/picture.datatype", 0, struct Library *, PictureBase);

struct writer {
    BPTR file;
    struct sixel_encoder encoder;
    char *out;
    size_t capacity;
};

/* Load image index; count, if given, receives the number of images. */
static LONG load_sixel(Class *cl, Object *obj, ULONG index, ULONG *count)
{
    struct sixel_image image;
    UBYTE *input;
    LONG size, error;
    ULONG images;

    error = dt_read_file(obj, 3, MAX_SIXEL_FILE, &input, &size);
    if (error != 0 || input == NULL)
        return error;
    images = sixel_count(input, (size_t)size);
    if (count != NULL)
        *count = images;
    /* The superclass kept the count pointer from the tags; store the count. */
    SetDTAttrs(obj, NULL, NULL, PDTA_GetNumPictures, images, TAG_END);
    error = dt_error(sixel_decode(input, (size_t)size, (unsigned)index, &image));
    FreeVec(input);
    if (error != 0)
        return error;
    error = dt_put_rgba(cl, obj, image.rgba, image.width, image.height);
    sixel_free(&image);
    if (error == 0)
        dt_set_name(obj);
    return error;
}

static BOOL scan_row(void *state, const UBYTE *rgba, ULONG width)
{
    struct writer *w = state;
    (void)width;
    return sixel_encoder_scan(&w->encoder, rgba) == CODEC_OK;
}

static BOOL write_row(void *state, const UBYTE *rgba, ULONG width)
{
    struct writer *w = state;
    size_t size;

    (void)width;
    if (!sixel_encoder_add_row(&w->encoder, rgba))
        return TRUE;
    while ((size = sixel_encoder_next(&w->encoder, w->out, w->capacity)) > 0)
        if (!dt_write(w->file, w->out, (LONG)size))
            return FALSE;
    return TRUE;
}

IPTR SIXEL__OM_NEW(Class *cl, Object *obj, struct opSet *msg)
{
    ULONG index = GetTagData(PDTA_WhichPicture, 0, msg->ops_AttrList);
    ULONG *count = (ULONG *)GetTagData(PDTA_GetNumPictures, 0,
                                       msg->ops_AttrList);
    IPTR created = DoSuperMethodA(cl, obj, (Msg)msg);
    LONG error;

    if (created == 0)
        return 0;
    error = load_sixel(cl, (Object *)created, index, count);
    if (error != 0) {
        CoerceMethod(cl, (Object *)created, OM_DISPOSE);
        SetIoErr(error);
        return 0;
    }
    return created;
}

IPTR SIXEL__DTM_WRITE(Class *cl, Object *obj, struct dtWrite *msg)
{
    struct writer w;
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
    if (sixel_encoder_init(&w.encoder, width, height) != CODEC_OK)
        return FALSE;
    w.file = msg->dtw_FileHandle;
    w.capacity = sixel_line_capacity(width);
    if (w.capacity < SIXEL_HEADER_MAX)
        w.capacity = SIXEL_HEADER_MAX;
    w.out = AllocVec(w.capacity, MEMF_ANY);
    /* First pass: the colours, which choose the registers. */
    if (w.out != NULL && dt_each_row(cl, obj, scan_row, &w) &&
        sixel_encoder_plan(&w.encoder) == CODEC_OK) {
        size = sixel_encoder_header(&w.encoder, w.out, w.capacity);
        success = size != 0 && dt_write(w.file, w.out, (LONG)size) &&
                  dt_each_row(cl, obj, write_row, &w);
        if (success) {
            size = sixel_encoder_end(w.out, w.capacity);
            success = dt_write(w.file, w.out, (LONG)size);
        }
    }
    FreeVec(w.out);
    sixel_encoder_free(&w.encoder);
    return success;
}
