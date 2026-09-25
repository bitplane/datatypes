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

/* The largest screen is a 256022-byte ComputerEyes file; anything after the
   picture is ignored. */
#define MAX_STSCREEN_FILE (1024L * 1024L)

ADD2LIBS((const UBYTE *)"datatypes/picture.datatype", 0, struct Library *, PictureBase);

struct collector {
    UBYTE *rgba;
    ULONG row;
};

static LONG load_stscreen(Class *cl, Object *obj)
{
    struct stscreen_image image;
    STRPTR name = NULL;
    UBYTE *input;
    LONG size, error;

    error = dt_read_file(obj, 2, MAX_STSCREEN_FILE, &input, &size);
    if (error != 0 || input == NULL)
        return error;
    /* Most of these formats have no magic; the extension picks the ones to try. */
    GetDTAttrs(obj, DTA_Name, &name, TAG_END);
    error = dt_error(stscreen_decode(input, (size_t)size, (const char *)name, &image));
    FreeVec(input);
    if (error != 0)
        return error;
    error = dt_put_rgba(cl, obj, image.rgba, image.width, image.height);
    stscreen_free(&image);
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

IPTR Stscreen__OM_NEW(Class *cl, Object *obj, struct opSet *msg)
{
    return dt_new(cl, obj, msg, load_stscreen);
}

IPTR Stscreen__DTM_WRITE(Class *cl, Object *obj, struct dtWrite *msg)
{
    struct collector c;
    UBYTE *output;
    ULONG width, height;
    size_t size = 0;
    IPTR success = FALSE;

    if (msg->dtw_Mode != DTWM_RAW)
        return DoSuperMethodA(cl, obj, (Msg)msg);
    /* MultiView probes RAW support without opening an output file. */
    if (msg->dtw_FileHandle == BNULL)
        return TRUE;
    /* Only Paintworks screen and page sizes can be saved; check before allocating. */
    if (!dt_picture_size(obj, &width, &height) ||
        !((width == 320 && (height == 200 || height == 400)) ||
          (width == 640 && (height == 200 || height == 400 || height == 800)))) {
        SetIoErr(DTERROR_INVALID_DATA);
        return FALSE;
    }
    c.rgba = AllocVec(width * height * 4u, MEMF_ANY);
    output = AllocVec(STSCREEN_MAX_OUTPUT, MEMF_ANY);
    c.row = 0;
    if (c.rgba != NULL && output != NULL && dt_each_row(cl, obj, collect_row, &c)) {
        LONG error = dt_error(stscreen_encode(c.rgba, width, height, output, &size));
        if (error == 0)
            success = dt_write(msg->dtw_FileHandle, output, (LONG)size);
        else
            SetIoErr(error);
    }
    FreeVec(output);
    FreeVec(c.rgba);
    return success;
}
