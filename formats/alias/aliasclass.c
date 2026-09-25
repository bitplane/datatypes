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

/* RLA files chain several images; allow a few 16M-pixel frames. */
#define MAX_ALIAS_FILE (256L * 1024L * 1024L)
#define MAX_ALIAS_PIXELS (16u * 1024u * 1024u)

ADD2LIBS((const UBYTE *)"datatypes/picture.datatype", 0, struct Library *, PictureBase);

struct copier {
    UBYTE *pixels;
    ULONG row;
    int alpha;
};

/* Load image index; count, if given, receives the number of images. */
static LONG load_alias(Class *cl, Object *obj, ULONG index, ULONG *count)
{
    struct alias_image image;
    UBYTE *input;
    LONG size, error;
    unsigned images;

    error = dt_read_file(obj, 10, MAX_ALIAS_FILE, &input, &size);
    if (error != 0 || input == NULL)
        return error;
    error = dt_error(alias_count(input, (size_t)size, &images));
    if (error == 0) {
        if (count != NULL)
            *count = images;
        /* The superclass kept the count pointer from the tags; store the count. */
        SetDTAttrs(obj, NULL, NULL, PDTA_GetNumPictures, (ULONG)images, TAG_END);
        error = dt_error(alias_decode(input, (size_t)size, (unsigned)index,
                                      &image));
    }
    FreeVec(input);
    if (error != 0)
        return error;
    error = dt_put_rgba(cl, obj, image.rgba, image.width, image.height);
    alias_free(&image);
    if (error == 0)
        dt_set_name(obj);
    return error;
}

static BOOL copy_row(void *state, const UBYTE *rgba, ULONG width)
{
    struct copier *c = state;

    c->alpha |= alias_row_has_alpha(rgba, width);
    memcpy(c->pixels + (size_t)c->row++ * width * 4u, rgba, (size_t)width * 4u);
    return TRUE;
}

IPTR Alias__OM_NEW(Class *cl, Object *obj, struct opSet *msg)
{
    ULONG index = GetTagData(PDTA_WhichPicture, 0, msg->ops_AttrList);
    ULONG *count = (ULONG *)GetTagData(PDTA_GetNumPictures, 0,
                                       msg->ops_AttrList);
    IPTR created = DoSuperMethodA(cl, obj, (Msg)msg);
    LONG error;

    if (created == 0)
        return 0;
    error = load_alias(cl, (Object *)created, index, count);
    if (error != 0) {
        CoerceMethod(cl, (Object *)created, OM_DISPOSE);
        SetIoErr(error);
        return 0;
    }
    return created;
}

/* Saves an RLA file. */
IPTR Alias__DTM_WRITE(Class *cl, Object *obj, struct dtWrite *msg)
{
    UBYTE header[RLA_HEADER_SIZE];
    struct copier c = { NULL, 0, 0 };
    UBYTE *out = NULL, *table = NULL;
    ULONG width, height, y;
    unsigned long offset;
    size_t size;
    IPTR success = FALSE;

    if (msg->dtw_Mode != DTWM_RAW)
        return DoSuperMethodA(cl, obj, (Msg)msg);
    /* MultiView probes RAW support without opening an output file. */
    if (msg->dtw_FileHandle == BNULL)
        return TRUE;
    if (!dt_picture_size(obj, &width, &height) ||
        (uint64_t)width * height > MAX_ALIAS_PIXELS)
        return FALSE;
    out = AllocVec(alias_row_capacity(width), MEMF_ANY);
    table = AllocVec((size_t)height * 4u, MEMF_ANY);
    /* Scanlines are stored bottom up after a table of their offsets, so
       take the whole picture first; its content also decides the matte. */
    c.pixels = AllocVec((size_t)width * height * 4u, MEMF_ANY);
    if (out == NULL || table == NULL || c.pixels == NULL ||
        !dt_each_row(cl, obj, copy_row, &c) ||
        !alias_make_header(width, height, c.alpha, header))
        goto done;
    offset = RLA_HEADER_SIZE + (unsigned long)height * 4u;
    for (y = 0; y < height; y++) {
        alias_put32(table + (size_t)y * 4u, offset);
        offset += alias_encode_row(c.pixels + (size_t)(height - 1u - y) *
                                   width * 4u, width, c.alpha, out);
    }
    if (!dt_write(msg->dtw_FileHandle, header, sizeof header) ||
        !dt_write(msg->dtw_FileHandle, table, (LONG)height * 4))
        goto done;
    for (y = 0; y < height; y++) {
        size = alias_encode_row(c.pixels + (size_t)(height - 1u - y) *
                                width * 4u, width, c.alpha, out);
        if (!dt_write(msg->dtw_FileHandle, out, (LONG)size))
            goto done;
    }
    success = TRUE;
done:
    FreeVec(c.pixels);
    FreeVec(table);
    FreeVec(out);
    return success;
}
