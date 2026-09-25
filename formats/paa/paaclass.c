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

#include "common/dtfile.h"
#include "common/dtpicture.h"
#include "decode.h"
#include "encode.h"

/* A 16M-pixel ARGB8888 level packs to at most 16MB, plus its mip chain. */
#define MAX_PAA_FILE (64L * 1024L * 1024L)
#define MAX_SAVE_PIXELS (16UL * 1024UL * 1024UL)

ADD2LIBS((const UBYTE *)"datatypes/picture.datatype", 0, struct Library *, PictureBase);

struct gather {
    UBYTE *rgba;
    ULONG row;
};

/* Load mip level index; count, if given, receives the number of levels. */
static LONG load_paa(Class *cl, Object *obj, ULONG index, ULONG *count)
{
    struct paa_image image;
    unsigned long levels = 0;
    UBYTE *input;
    LONG size, error;

    error = dt_read_file(obj, 2, MAX_PAA_FILE, &input, &size);
    if (error != 0 || input == NULL)
        return error;
    paa_count(input, (size_t)size, &levels);
    if (count != NULL)
        *count = levels;
    /* The superclass kept the count pointer from the tags; store the count. */
    SetDTAttrs(obj, NULL, NULL, PDTA_GetNumPictures, levels, TAG_END);
    error = dt_error(paa_decode(input, (size_t)size, index, &image));
    FreeVec(input);
    if (error != 0)
        return error;
    error = dt_put_rgba(cl, obj, image.rgba, image.width, image.height);
    paa_free(&image);
    if (error == 0)
        dt_set_name(obj);
    return error;
}

static BOOL gather_row(void *state, const UBYTE *rgba, ULONG width)
{
    struct gather *g = state;
    CopyMem((APTR)rgba, g->rgba + (size_t)g->row++ * width * 4u, width * 4u);
    return TRUE;
}

IPTR Paa__OM_NEW(Class *cl, Object *obj, struct opSet *msg)
{
    ULONG index = GetTagData(PDTA_WhichPicture, 0, msg->ops_AttrList);
    ULONG *count = (ULONG *)GetTagData(PDTA_GetNumPictures, 0,
                                       msg->ops_AttrList);
    IPTR created = DoSuperMethodA(cl, obj, (Msg)msg);
    LONG error;

    if (created == 0)
        return 0;
    error = load_paa(cl, (Object *)created, index, count);
    if (error != 0) {
        CoerceMethod(cl, (Object *)created, OM_DISPOSE);
        SetIoErr(error);
        return 0;
    }
    return created;
}

IPTR Paa__DTM_WRITE(Class *cl, Object *obj, struct dtWrite *msg)
{
    struct gather g = { NULL, 0 };
    ULONG width, height;
    uint8_t *file = NULL;
    size_t length = 0;
    IPTR success = FALSE;

    if (msg->dtw_Mode != DTWM_RAW)
        return DoSuperMethodA(cl, obj, (Msg)msg);
    /* MultiView probes RAW support without opening an output file. */
    if (msg->dtw_FileHandle == BNULL)
        return TRUE;
    if (!dt_picture_size(obj, &width, &height) ||
        (uint64_t)width * height > MAX_SAVE_PIXELS)
        return FALSE;
    /* The level is packed as a whole, so gather every row first. */
    g.rgba = AllocVec(width * height * 4u, MEMF_ANY);
    if (g.rgba == NULL)
        return FALSE;
    if (dt_each_row(cl, obj, gather_row, &g) &&
        paa_encode(g.rgba, width, height, &file, &length) == CODEC_OK)
        success = dt_write(msg->dtw_FileHandle, file, (LONG)length);
    free(file);
    FreeVec(g.rgba);
    return success;
}
