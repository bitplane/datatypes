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

/* 16M pixels and their mips come to about 22M; leave room for odd offsets. */
#define MAX_WAL_FILE (64L * 1024L * 1024L)

ADD2LIBS((const UBYTE *)"datatypes/picture.datatype", 0, struct Library *, PictureBase);

/* Load mip level index; count, if given, receives the number of levels. */
static LONG load_wal(Class *cl, Object *obj, ULONG index, ULONG *count)
{
    struct wal_image image;
    UBYTE *input;
    LONG size, error;
    ULONG levels;

    error = dt_read_file(obj, WAL_HEADER, MAX_WAL_FILE, &input, &size);
    if (error != 0 || input == NULL)
        return error;
    levels = wal_count(input, (size_t)size);
    if (count != NULL)
        *count = levels;
    /* The superclass kept the count pointer from the tags; store the count. */
    SetDTAttrs(obj, NULL, NULL, PDTA_GetNumPictures, levels, TAG_END);
    error = dt_error(wal_decode(input, (size_t)size, (unsigned)index, &image));
    FreeVec(input);
    if (error != 0)
        return error;
    error = dt_put_rgba(cl, obj, image.rgba, image.width, image.height);
    wal_free(&image);
    if (error == 0)
        dt_set_name(obj);
    return error;
}

static BOOL write_row(void *state, const UBYTE *rgba, ULONG width)
{
    (void)width;
    return wal_encoder_row(state, rgba) != 0;
}

IPTR WAL__OM_NEW(Class *cl, Object *obj, struct opSet *msg)
{
    ULONG index = GetTagData(PDTA_WhichPicture, 0, msg->ops_AttrList);
    ULONG *count = (ULONG *)GetTagData(PDTA_GetNumPictures, 0,
                                       msg->ops_AttrList);
    IPTR created = DoSuperMethodA(cl, obj, (Msg)msg);
    LONG error;

    if (created == 0)
        return 0;
    error = load_wal(cl, (Object *)created, index, count);
    if (error != 0) {
        CoerceMethod(cl, (Object *)created, OM_DISPOSE);
        SetIoErr(error);
        return 0;
    }
    return created;
}

IPTR WAL__DTM_WRITE(Class *cl, Object *obj, struct dtWrite *msg)
{
    struct wal_encoder *encoder;
    const UBYTE *file;
    ULONG width, height;
    size_t size;
    IPTR success;

    if (msg->dtw_Mode != DTWM_RAW)
        return DoSuperMethodA(cl, obj, (Msg)msg);
    /* MultiView probes RAW support without opening an output file. */
    if (msg->dtw_FileHandle == BNULL)
        return TRUE;
    if (!dt_picture_size(obj, &width, &height) ||
        (encoder = wal_encoder_new(width, height)) == NULL)
        return FALSE;
    success = dt_each_row(cl, obj, write_row, encoder) &&
              (file = wal_encoder_finish(encoder, &size)) != NULL &&
              dt_write(msg->dtw_FileHandle, file, (LONG)size);
    wal_encoder_free(encoder);
    return success;
}
