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

#define MAX_QOI_FILE (128L * 1024L * 1024L)

ADD2LIBS((const UBYTE *)"datatypes/picture.datatype", 0, struct Library *, PictureBase);

struct writer {
    BPTR file;
    struct qoi_encoder encoder;
    UBYTE *out;
    size_t capacity;
};

static LONG load_qoi(Class *cl, Object *obj)
{
    struct qoi_image image;
    UBYTE *input;
    LONG size, error;

    error = dt_read_file(obj, 22, MAX_QOI_FILE, &input, &size);
    if (error != 0 || input == NULL)
        return error;
    error = dt_error(qoi_decode(input, (size_t)size, &image));
    FreeVec(input);
    if (error != 0)
        return error;
    error = dt_put_rgba(cl, obj, image.rgba, image.width, image.height);
    qoi_free(&image);
    if (error == 0)
        dt_set_name(obj);
    return error;
}

static BOOL write_row(void *state, const UBYTE *rgba, ULONG width)
{
    struct writer *w = state;
    size_t size = qoi_encode_row(&w->encoder, rgba, width, w->out, w->capacity);
    return size != SIZE_MAX && dt_write(w->file, w->out, (LONG)size);
}

IPTR QOI__OM_NEW(Class *cl, Object *obj, struct opSet *msg)
{
    return dt_new(cl, obj, msg, load_qoi);
}

IPTR QOI__DTM_WRITE(Class *cl, Object *obj, struct dtWrite *msg)
{
    UBYTE header[14], end[9];
    struct writer w;
    ULONG width, height;
    size_t size;
    IPTR success = FALSE;

    if (msg->dtw_Mode != DTWM_RAW)
        return DoSuperMethodA(cl, obj, (Msg)msg);
    if (msg->dtw_FileHandle == BNULL)
        return TRUE;
    if (!dt_picture_size(obj, &width, &height) ||
        !qoi_make_header(width, height, header))
        return FALSE;
    w.file = msg->dtw_FileHandle;
    w.capacity = (size_t)width * 5u + 1u;
    w.out = AllocVec(w.capacity, MEMF_ANY);
    if (w.out == NULL)
        return FALSE;
    qoi_encoder_init(&w.encoder);
    if (dt_write(w.file, header, sizeof header) &&
        dt_each_row(cl, obj, write_row, &w)) {
        size = qoi_encode_end(&w.encoder, end, sizeof end);
        success = size != SIZE_MAX && dt_write(w.file, end, (LONG)size);
    }
    FreeVec(w.out);
    return success;
}
