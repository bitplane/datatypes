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
#include "decode.h"
#include "encode.h"

#define MAX_ICNS_FILE (128L * 1024L * 1024L)

ADD2LIBS((const UBYTE *)"datatypes/picture.datatype", 0, struct Library *, PictureBase);

struct gather {
    UBYTE *rgba;
    ULONG row;
};

static LONG load_icns(Class *cl, Object *obj, long index, ULONG *count_out)
{
    struct icns_image image;
    UBYTE *input;
    LONG size, error;
    unsigned count;

    error = dt_read_file(obj, 8, MAX_ICNS_FILE, &input, &size);
    if (error != 0 || input == NULL)
        return error;
    error = dt_error(icns_decode(input, (size_t)size, index, &image, &count));
    FreeVec(input);
    if (count_out != NULL)
        *count_out = count;
    if (error != 0)
        return error;
    error = dt_put_rgba(cl, obj, image.rgba, image.width, image.height);
    icns_free(&image);
    if (error == 0) {
        SetDTAttrs(obj, NULL, NULL, PDTA_GetNumPictures, (IPTR)count, TAG_END);
        dt_set_name(obj);
    }
    return error;
}

static BOOL gather_row(void *state, const UBYTE *rgba, ULONG width)
{
    struct gather *g = state;
    memcpy(g->rgba + (size_t)g->row * width * 4u, rgba, (size_t)width * 4u);
    g->row++;
    return TRUE;
}

/* dt_new doesn't pass OM_NEW's tags on, so pick out the image here. */
IPTR ICNS__OM_NEW(Class *cl, Object *obj, struct opSet *msg)
{
    struct TagItem *which = FindTagItem(PDTA_WhichPicture, msg->ops_AttrList);
    ULONG *count = (ULONG *)GetTagData(PDTA_GetNumPictures, 0, msg->ops_AttrList);
    long index = which != NULL ? (long)which->ti_Data : ICNS_BEST;
    IPTR created = DoSuperMethodA(cl, obj, (Msg)msg);
    LONG error;

    if (created == 0)
        return 0;
    error = load_icns(cl, (Object *)created, index, count);
    if (error != 0) {
        CoerceMethod(cl, (Object *)created, OM_DISPOSE);
        SetIoErr(error);
        return 0;
    }
    return created;
}

IPTR ICNS__DTM_WRITE(Class *cl, Object *obj, struct dtWrite *msg)
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
    /* ICNS only has slots for certain square sizes. */
    if (!dt_picture_size(obj, &width, &height) || !icns_can_encode(width, height))
        return FALSE;
    g.rgba = AllocVec(width * height * 4u, MEMF_ANY);
    if (g.rgba == NULL)
        return FALSE;
    if (dt_each_row(cl, obj, gather_row, &g) &&
        icns_encode(g.rgba, width, height, &out, &length) == CODEC_OK) {
        success = length <= 0x7fffffffu && dt_write(msg->dtw_FileHandle, out, (LONG)length);
        free(out);
    }
    FreeVec(g.rgba);
    return success;
}
