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

#include <string.h>

#include "common/dtfile.h"
#include "common/dtpicture.h"
#include "decode.h"
#include "encode.h"

/* Screens are 6912 bytes. Larger files up to this size reach the decoder,
   which rejects them as other screen formats rather than too large. */
#define MAX_ZXSCR_FILE (64L * 1024L)

ADD2LIBS((const UBYTE *)"datatypes/picture.datatype", 0, struct Library *, PictureBase);

struct collector {
    UBYTE *rgba;
    ULONG row;
};

static LONG load_zxscr(Class *cl, Object *obj)
{
    struct zxscr_image image;
    UBYTE *input;
    LONG size, error;

    error = dt_read_file(obj, 1, MAX_ZXSCR_FILE, &input, &size);
    if (error != 0 || input == NULL)
        return error;
    error = dt_error(zxscr_decode(input, (size_t)size, &image));
    FreeVec(input);
    if (error != 0)
        return error;
    error = dt_put_rgba(cl, obj, image.rgba, image.width, image.height);
    zxscr_free(&image);
    if (error == 0)
        dt_set_name(obj);
    return error;
}

static BOOL collect_row(void *state, const UBYTE *rgba, ULONG width)
{
    struct collector *c = state;
    memcpy(c->rgba + (size_t)c->row++ * width * 4u, rgba, width * 4u);
    return TRUE;
}

IPTR Zxscr__OM_NEW(Class *cl, Object *obj, struct opSet *msg)
{
    return dt_new(cl, obj, msg, load_zxscr);
}

IPTR Zxscr__DTM_WRITE(Class *cl, Object *obj, struct dtWrite *msg)
{
    struct collector c;
    UBYTE *output;
    ULONG width, height;
    IPTR success = FALSE;

    if (msg->dtw_Mode != DTWM_RAW)
        return DoSuperMethodA(cl, obj, (Msg)msg);
    /* MultiView probes RAW support without opening an output file. */
    if (msg->dtw_FileHandle == BNULL)
        return TRUE;
    /* Only a whole 256x192 screen can be saved; check before allocating. */
    if (!dt_picture_size(obj, &width, &height) ||
        width != ZXSCR_WIDTH || height != ZXSCR_HEIGHT) {
        SetIoErr(DTERROR_INVALID_DATA);
        return FALSE;
    }
    c.rgba = AllocVec(width * height * 4u, MEMF_ANY);
    output = AllocVec(ZXSCR_FILE_SIZE, MEMF_ANY);
    c.row = 0;
    if (c.rgba != NULL && output != NULL && dt_each_row(cl, obj, collect_row, &c)) {
        LONG error = dt_error(zxscr_encode(c.rgba, width, height, output));
        if (error == 0)
            success = dt_write(msg->dtw_FileHandle, output, ZXSCR_FILE_SIZE);
        else
            SetIoErr(error);
    }
    FreeVec(output);
    FreeVec(c.rgba);
    return success;
}
