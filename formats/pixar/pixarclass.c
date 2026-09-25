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

/* 16M RGBA pixels run-length encoded one at a time, plus block padding. */
#define MAX_PIXAR_FILE (160L * 1024L * 1024L)

ADD2LIBS((const UBYTE *)"datatypes/picture.datatype", 0, struct Library *, PictureBase);

struct writer {
    BPTR file;
    UBYTE *out;
    size_t capacity;
    int alpha;
};

static LONG load_pixar(Class *cl, Object *obj)
{
    struct pixar_image image;
    UBYTE *input;
    LONG size, error;

    error = dt_read_file(obj, 512, MAX_PIXAR_FILE, &input, &size);
    if (error != 0 || input == NULL)
        return error;
    error = dt_error(pixar_decode(input, (size_t)size, &image));
    FreeVec(input);
    if (error != 0)
        return error;
    error = dt_put_rgba(cl, obj, image.rgba, image.width, image.height);
    pixar_free(&image);
    if (error == 0)
        dt_set_name(obj);
    return error;
}

static BOOL opaque_row(void *state, const UBYTE *rgba, ULONG width)
{
    struct writer *w = state;
    ULONG x;
    for (x = 0; x < width; x++) {
        if (rgba[x * 4u + 3u] != 255) {
            w->alpha = 1;
            return FALSE;
        }
    }
    return TRUE;
}

static BOOL write_row(void *state, const UBYTE *rgba, ULONG width)
{
    struct writer *w = state;
    size_t size = pixar_encode_row(rgba, width, w->alpha, w->out, w->capacity);
    return size != 0 && dt_write(w->file, w->out, (LONG)size);
}

IPTR Pixar__OM_NEW(Class *cl, Object *obj, struct opSet *msg)
{
    return dt_new(cl, obj, msg, load_pixar);
}

IPTR Pixar__DTM_WRITE(Class *cl, Object *obj, struct dtWrite *msg)
{
    UBYTE *header;
    struct writer w = { BNULL, NULL, 0, 0 };
    ULONG width, height;
    IPTR success;

    if (msg->dtw_Mode != DTWM_RAW)
        return DoSuperMethodA(cl, obj, (Msg)msg);
    /* MultiView probes RAW support without opening an output file. */
    if (msg->dtw_FileHandle == BNULL)
        return TRUE;
    if (!dt_picture_size(obj, &width, &height))
        return FALSE;
    /* Pixel content decides whether alpha is needed.
       A stopped scan without alpha means a pixel read failed. */
    if (!dt_each_row(cl, obj, opaque_row, &w) && !w.alpha)
        return FALSE;
    w.file = msg->dtw_FileHandle;
    w.capacity = width * 4u;
    header = AllocVec(PIXAR_ENCODE_HEADER + w.capacity, MEMF_ANY);
    if (header == NULL)
        return FALSE;
    w.out = header + PIXAR_ENCODE_HEADER;
    success = pixar_make_header(width, height, w.alpha, header) &&
              dt_write(w.file, header, PIXAR_ENCODE_HEADER) &&
              dt_each_row(cl, obj, write_row, &w);
    FreeVec(header);
    return success;
}
