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
#include <stdlib.h>
#include <string.h>

#include "common/dtfile.h"
#include "common/dtpicture.h"
#include "ora.h"

/* Layered documents carry every layer as well as the composite. */
#define MAX_ORA_FILE (512L * 1024L * 1024L)

ADD2LIBS((const UBYTE *)"datatypes/picture.datatype", 0, struct Library *, PictureBase);

struct gather {
    UBYTE *rgba;
    ULONG row;
};

static LONG load_ora(Class *cl, Object *obj)
{
    struct ora_image image;
    UBYTE *input;
    LONG size, error;

    error = dt_read_file(obj, 22, MAX_ORA_FILE, &input, &size);
    if (error != 0 || input == NULL)
        return error;
    error = dt_error(ora_decode(input, (size_t)size, &image));
    FreeVec(input);
    if (error != 0)
        return error;
    error = dt_put_rgba(cl, obj, image.rgba, image.width, image.height);
    ora_free(&image);
    if (error == 0)
        dt_set_name(obj);
    return error;
}

static BOOL gather_row(void *state, const UBYTE *rgba, ULONG width)
{
    struct gather *g = state;
    memcpy(g->rgba + (size_t)g->row * width * 4u, rgba, (size_t)width * 4u);
    g->row++;
    return TRUE;
}

IPTR ORA__OM_NEW(Class *cl, Object *obj, struct opSet *msg)
{
    return dt_new(cl, obj, msg, load_ora);
}

IPTR ORA__DTM_WRITE(Class *cl, Object *obj, struct dtWrite *msg)
{
    struct gather g = { NULL, 0 };
    ULONG width, height;
    uint8_t *out;
    size_t length;
    IPTR success = FALSE;

    if (msg->dtw_Mode != DTWM_RAW)
        return DoSuperMethodA(cl, obj, (Msg)msg);
    /* MultiView probes RAW support without opening an output file. */
    if (msg->dtw_FileHandle == BNULL)
        return TRUE;
    if (!dt_picture_size(obj, &width, &height))
        return FALSE;
    g.rgba = AllocVec(width * height * 4u, MEMF_ANY);
    if (g.rgba == NULL)
        return FALSE;
    if (dt_each_row(cl, obj, gather_row, &g) &&
        ora_encode(g.rgba, width, height, &out, &length) == CODEC_OK) {
        success = length <= 0x7fffffffu && dt_write(msg->dtw_FileHandle, out, (LONG)length);
        free(out);
    }
    FreeVec(g.rgba);
    return success;
}
