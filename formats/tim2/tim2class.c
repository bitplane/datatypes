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

/* Room for a 16M-pixel 32-bit picture with its mipmaps, and more pictures. */
#define MAX_TIM2_FILE (256L * 1024L * 1024L)

ADD2LIBS((const UBYTE *)"datatypes/picture.datatype", 0, struct Library *, PictureBase);

struct writer {
    BPTR file;
    UBYTE *out;
    LONG size;
    BOOL alpha;
};

/* Load image index; count, if given, receives the number of images. */
static LONG load_tim2(Class *cl, Object *obj, ULONG index, ULONG *count)
{
    struct tim2_image image;
    UBYTE *input;
    LONG size, error;
    ULONG images;

    error = dt_read_file(obj, 16, MAX_TIM2_FILE, &input, &size);
    if (error != 0 || input == NULL)
        return error;
    images = tim2_count(input, (size_t)size);
    if (count != NULL)
        *count = images;
    /* The superclass kept the count pointer from the tags; store the count. */
    SetDTAttrs(obj, NULL, NULL, PDTA_GetNumPictures, images, TAG_END);
    error = dt_error(tim2_decode(input, (size_t)size, (unsigned)index, &image));
    FreeVec(input);
    if (error != 0)
        return error;
    error = dt_put_rgba(cl, obj, image.rgba, image.width, image.height);
    tim2_free(&image);
    if (error == 0)
        dt_set_name(obj);
    return error;
}

static BOOL find_alpha(void *state, const UBYTE *rgba, ULONG width)
{
    BOOL *alpha = state;
    ULONG x;
    for (x = 0; x < width; x++)
        if (rgba[x * 4 + 3] != 255)
            *alpha = TRUE;
    /* One translucent pixel decides it; stop reading rows. */
    return !*alpha;
}

static BOOL write_row(void *state, const UBYTE *rgba, ULONG width)
{
    struct writer *w = state;
    tim2_encode_row(rgba, width, w->alpha, w->out);
    return dt_write(w->file, w->out, w->size);
}

IPTR Tim2__OM_NEW(Class *cl, Object *obj, struct opSet *msg)
{
    ULONG index = GetTagData(PDTA_WhichPicture, 0, msg->ops_AttrList);
    ULONG *count = (ULONG *)GetTagData(PDTA_GetNumPictures, 0,
                                       msg->ops_AttrList);
    IPTR created = DoSuperMethodA(cl, obj, (Msg)msg);
    LONG error;

    if (created == 0)
        return 0;
    error = load_tim2(cl, (Object *)created, index, count);
    if (error != 0) {
        CoerceMethod(cl, (Object *)created, OM_DISPOSE);
        SetIoErr(error);
        return 0;
    }
    return created;
}

IPTR Tim2__DTM_WRITE(Class *cl, Object *obj, struct dtWrite *msg)
{
    UBYTE header[TIM2_WRITE_HEADER];
    struct writer w;
    ULONG width, height;
    IPTR success;

    if (msg->dtw_Mode != DTWM_RAW)
        return DoSuperMethodA(cl, obj, (Msg)msg);
    /* MultiView probes RAW support without opening an output file. */
    if (msg->dtw_FileHandle == BNULL)
        return TRUE;
    if (!dt_picture_size(obj, &width, &height))
        return FALSE;
    /* Opaque pictures save as 24-bit; any translucency needs 32-bit. */
    w.alpha = FALSE;
    dt_each_row(cl, obj, find_alpha, &w.alpha);
    if (!tim2_make_header(width, height, w.alpha, header))
        return FALSE;
    w.file = msg->dtw_FileHandle;
    w.size = (LONG)tim2_row_size(width, w.alpha);
    w.out = AllocVec((ULONG)w.size, MEMF_ANY);
    if (w.out == NULL)
        return FALSE;
    success = dt_write(w.file, header, sizeof header) &&
              dt_each_row(cl, obj, write_row, &w);
    FreeVec(w.out);
    return success;
}
