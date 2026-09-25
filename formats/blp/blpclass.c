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

/* A full mip chain of 16M BGRA pixels, plus header and palette. */
#define MAX_BLP_FILE (96L * 1024L * 1024L)

ADD2LIBS((const UBYTE *)"datatypes/picture.datatype", 0, struct Library *, PictureBase);

struct writer {
    BPTR file;
    UBYTE *out;
    struct blp_palette palette;
};

/* Load mip level index; count, if given, receives the number of levels. */
static LONG load_blp(Class *cl, Object *obj, ULONG index, ULONG *count)
{
    struct blp_image image;
    unsigned long levels = 0;
    UBYTE *input;
    LONG size, error;

    error = dt_read_file(obj, 4, MAX_BLP_FILE, &input, &size);
    if (error != 0 || input == NULL)
        return error;
    blp_count(input, (size_t)size, &levels);
    if (count != NULL)
        *count = levels;
    /* The superclass kept the count pointer from the tags; store the count. */
    SetDTAttrs(obj, NULL, NULL, PDTA_GetNumPictures, levels, TAG_END);
    error = dt_error(blp_decode(input, (size_t)size, index, &image));
    FreeVec(input);
    if (error != 0)
        return error;
    error = dt_put_rgba(cl, obj, image.rgba, image.width, image.height);
    blp_free(&image);
    if (error == 0)
        dt_set_name(obj);
    return error;
}

static BOOL measure_row(void *state, const UBYTE *rgba, ULONG width)
{
    struct writer *w = state;
    blp_palette_add_row(&w->palette, rgba, width);
    return TRUE;
}

static BOOL write_row(void *state, const UBYTE *rgba, ULONG width)
{
    struct writer *w = state;
    blp_encode_row(&w->palette, rgba, width, w->out);
    return dt_write(w->file, w->out, (LONG)blp_row_size(&w->palette, width));
}

static BOOL write_alpha_row(void *state, const UBYTE *rgba, ULONG width)
{
    struct writer *w = state;
    blp_encode_alpha_row(rgba, width, w->out);
    return dt_write(w->file, w->out, (LONG)width);
}

IPTR Blp__OM_NEW(Class *cl, Object *obj, struct opSet *msg)
{
    ULONG index = GetTagData(PDTA_WhichPicture, 0, msg->ops_AttrList);
    ULONG *count = (ULONG *)GetTagData(PDTA_GetNumPictures, 0,
                                       msg->ops_AttrList);
    IPTR created = DoSuperMethodA(cl, obj, (Msg)msg);
    LONG error;

    if (created == 0)
        return 0;
    error = load_blp(cl, (Object *)created, index, count);
    if (error != 0) {
        CoerceMethod(cl, (Object *)created, OM_DISPOSE);
        SetIoErr(error);
        return 0;
    }
    return created;
}

IPTR Blp__DTM_WRITE(Class *cl, Object *obj, struct dtWrite *msg)
{
    UBYTE header[BLP_HEADER_SIZE];
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
    /* The palette, or its absence, goes in the header, so see every pixel first. */
    blp_palette_init(&w.palette);
    if (!dt_each_row(cl, obj, measure_row, &w) ||
        !blp_make_header(width, height, &w.palette, header))
        return FALSE;
    w.file = msg->dtw_FileHandle;
    w.out = AllocVec(width * 4u, MEMF_ANY);
    if (w.out == NULL)
        return FALSE;
    success = dt_write(w.file, header, sizeof header) &&
              dt_each_row(cl, obj, write_row, &w);
    /* A palettized image's alpha follows all of its indices. */
    if (success && !w.palette.full && w.palette.alpha)
        success = dt_each_row(cl, obj, write_alpha_row, &w);
    FreeVec(w.out);
    return success;
}
