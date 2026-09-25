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

ADD2LIBS((const UBYTE *)"datatypes/picture.datatype", 0, struct Library *, PictureBase);

struct collector {
    UBYTE *rgba;
    ULONG row;
};

static LONG load_c64(Class *cl, Object *obj)
{
    struct c64_image image;
    STRPTR name = NULL;
    UBYTE *input;
    LONG size, error;

    error = dt_read_file(obj, 2, C64_MAX_FILE, &input, &size);
    if (error != 0 || input == NULL)
        return error;
    /* The extension only breaks ties between formats of the same size. */
    GetDTAttrs(obj, DTA_Name, &name, TAG_END);
    error = dt_error(c64_decode(input, (size_t)size,
                                name != NULL ? (const char *)FilePart(name) : NULL, &image));
    FreeVec(input);
    if (error != 0)
        return error;
    error = dt_put_rgba(cl, obj, image.rgba, image.width, image.height);
    c64_free(&image);
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

IPTR C64__OM_NEW(Class *cl, Object *obj, struct opSet *msg)
{
    return dt_new(cl, obj, msg, load_c64);
}

IPTR C64__DTM_WRITE(Class *cl, Object *obj, struct dtWrite *msg)
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
    /* Only a whole 320x200 screen can be saved; check before allocating. */
    if (!dt_picture_size(obj, &width, &height) ||
        width != C64_WIDTH || height != C64_HEIGHT) {
        SetIoErr(DTERROR_INVALID_DATA);
        return FALSE;
    }
    c.rgba = AllocVec(width * height * 4u, MEMF_ANY);
    output = AllocVec(C64_ENCODE_MAX, MEMF_ANY);
    c.row = 0;
    if (c.rgba != NULL && output != NULL && dt_each_row(cl, obj, collect_row, &c)) {
        size_t size;
        LONG error = dt_error(c64_encode(c.rgba, width, height, output, &size));
        if (error == 0)
            success = dt_write(msg->dtw_FileHandle, output, (LONG)size);
        else
            SetIoErr(error);
    }
    FreeVec(output);
    FreeVec(c.rgba);
    return success;
}
