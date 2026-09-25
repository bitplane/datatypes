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

/* 16M pixels in four separations, plus the row padding and headers. */
#define MAX_SCT_FILE (80L * 1024L * 1024L)

ADD2LIBS((const UBYTE *)"datatypes/picture.datatype", 0, struct Library *, PictureBase);

struct writer {
    BPTR file;
    UBYTE *out;
    int gray;
};

static LONG load_sct(Class *cl, Object *obj)
{
    struct sct_image image;
    UBYTE *input;
    LONG size, error;

    error = dt_read_file(obj, 82, MAX_SCT_FILE, &input, &size);
    if (error != 0 || input == NULL)
        return error;
    error = dt_error(sct_decode(input, (size_t)size, &image));
    FreeVec(input);
    if (error != 0)
        return error;
    error = dt_put_rgba(cl, obj, image.rgba, image.width, image.height);
    sct_free(&image);
    if (error == 0)
        dt_set_name(obj);
    return error;
}

/* Stops at the first row with colour in it. */
static BOOL check_gray(void *state, const UBYTE *rgba, ULONG width)
{
    struct writer *w = state;
    w->gray = sct_row_is_gray(rgba, width);
    return w->gray;
}

static BOOL write_row(void *state, const UBYTE *rgba, ULONG width)
{
    struct writer *w = state;
    sct_encode_row(rgba, width, w->gray, w->out);
    return dt_write(w->file, w->out, (LONG)sct_line_size(width, w->gray));
}

IPTR Sct__OM_NEW(Class *cl, Object *obj, struct opSet *msg)
{
    return dt_new(cl, obj, msg, load_sct);
}

IPTR Sct__DTM_WRITE(Class *cl, Object *obj, struct dtWrite *msg)
{
    UBYTE *header;
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
    w.file = msg->dtw_FileHandle;
    w.gray = 1;
    dt_each_row(cl, obj, check_gray, &w);
    header = AllocVec(SCT_HEADER_SIZE + sct_line_size(width, w.gray),
                      MEMF_ANY);
    if (header == NULL)
        return FALSE;
    w.out = header + SCT_HEADER_SIZE;
    success = sct_make_header(width, height, w.gray, header) &&
              dt_write(w.file, header, SCT_HEADER_SIZE) &&
              dt_each_row(cl, obj, write_row, &w);
    FreeVec(header);
    return success;
}
