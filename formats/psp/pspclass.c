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
#include "psp.h"

/* Layered files can be large; the canvas itself is capped by the codec. */
#define MAX_PSP_FILE (512L * 1024L * 1024L)

ADD2LIBS((const UBYTE *)"datatypes/picture.datatype", 0, struct Library *, PictureBase);

static LONG load_psp(Class *cl, Object *obj)
{
    struct psp_image image;
    UBYTE *input;
    LONG size, error;

    error = dt_read_file(obj, 36, MAX_PSP_FILE, &input, &size);
    if (error != 0 || input == NULL)
        return error;
    error = dt_error(psp_decode(input, (size_t)size, &image));
    FreeVec(input);
    if (error != 0)
        return error;
    error = dt_put_rgba(cl, obj, image.rgba, image.width, image.height);
    psp_free(&image);
    if (error == 0)
        dt_set_name(obj);
    return error;
}

IPTR Psp__OM_NEW(Class *cl, Object *obj, struct opSet *msg)
{
    return dt_new(cl, obj, msg, load_psp);
}

struct gather { UBYTE *rgba; ULONG y; };

static BOOL gather_row(void *state, const UBYTE *rgba, ULONG width)
{
    struct gather *g = state;
    CopyMem((APTR)rgba, g->rgba + (size_t)g->y * width * 4u, width * 4u);
    g->y++;
    return TRUE;
}

IPTR Psp__DTM_WRITE(Class *cl, Object *obj, struct dtWrite *msg)
{
    struct gather g;
    ULONG width, height;
    uint8_t *file = NULL;
    size_t size = 0;
    IPTR success = FALSE;

    if (msg->dtw_Mode != DTWM_RAW)
        return DoSuperMethodA(cl, obj, (Msg)msg);
    /* MultiView probes RAW support without opening an output file. */
    if (msg->dtw_FileHandle == BNULL)
        return TRUE;
    if (!dt_picture_size(obj, &width, &height) ||
        (uint64_t)width * height > PSP_MAX_PIXELS)
        return FALSE;
    /* Each channel is compressed as one stream, so gather the picture. */
    g.y = 0;
    g.rgba = AllocVec(width * height * 4u, MEMF_ANY);
    if (g.rgba == NULL)
        return FALSE;
    if (dt_each_row(cl, obj, gather_row, &g) &&
        psp_encode(g.rgba, width, height, &file, &size) == CODEC_OK)
        success = dt_write(msg->dtw_FileHandle, file, (LONG)size);
    free(file);
    FreeVec(g.rgba);
    return success;
}
