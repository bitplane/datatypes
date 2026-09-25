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

ADD2LIBS((const UBYTE *)"datatypes/picture.datatype", 0, struct Library *, PictureBase);

struct writer {
    BPTR file;
    UBYTE *out;
    LONG size;
    int big_endian;
};

/* Projects can hold hundreds of megabytes of layers and frames, so the
   decoder reads only what it composites, straight from the file. */
static int read_file(void *context, uint64_t offset, void *buffer, size_t length)
{
    BPTR file = (BPTR)context;
    if (offset > 0x7fffffffUL || length > 0x7fffffffUL)
        return 1;
    if (Seek(file, (LONG)offset, OFFSET_BEGINNING) == -1)
        return 1;
    return Read(file, buffer, (LONG)length) != (LONG)length;
}

/* Load frame index; count, if given, receives the number of frames. */
static LONG load_lunapaint(Class *cl, Object *obj, ULONG index, ULONG *count)
{
    struct lunapaint_source source;
    struct lunapaint_image image;
    IPTR source_type = 0;
    BPTR file = BNULL;
    LONG size, error;

    GetDTAttrs(obj, DTA_SourceType, &source_type, DTA_Handle, &file, TAG_END);
    if (source_type == DTST_RAM)
        return 0;
    if (source_type != DTST_FILE || file == BNULL)
        return ERROR_OBJECT_WRONG_TYPE;
    /* Seek returns the previous position, so the second call reports the end. */
    if (Seek(file, 0, OFFSET_END) == -1)
        return DTERROR_COULDNT_OPEN;
    size = Seek(file, 0, OFFSET_CURRENT);
    if (size == -1)
        return DTERROR_COULDNT_OPEN;
    source.context = (void *)file;
    source.size = (uint64_t)size;
    source.read = read_file;
    error = dt_error(lunapaint_decode(&source, (unsigned)index, &image));
    if (image.frames != 0) {
        if (count != NULL)
            *count = image.frames;
        /* The superclass kept the count pointer from the tags; store the count. */
        SetDTAttrs(obj, NULL, NULL, PDTA_GetNumPictures, image.frames, TAG_END);
    }
    if (error != 0)
        return error;
    error = dt_put_rgba(cl, obj, image.rgba, image.width, image.height);
    lunapaint_free(&image);
    if (error == 0)
        dt_set_name(obj);
    return error;
}

static BOOL write_row(void *state, const UBYTE *rgba, ULONG width)
{
    struct writer *w = state;
    lunapaint_encode_row(rgba, width, w->big_endian, w->out);
    return dt_write(w->file, w->out, w->size);
}

IPTR Lunapaint__OM_NEW(Class *cl, Object *obj, struct opSet *msg)
{
    ULONG index = GetTagData(PDTA_WhichPicture, 0, msg->ops_AttrList);
    ULONG *count = (ULONG *)GetTagData(PDTA_GetNumPictures, 0,
                                       msg->ops_AttrList);
    IPTR created = DoSuperMethodA(cl, obj, (Msg)msg);
    LONG error;

    if (created == 0)
        return 0;
    error = load_lunapaint(cl, (Object *)created, index, count);
    if (error != 0) {
        CoerceMethod(cl, (Object *)created, OM_DISPOSE);
        SetIoErr(error);
        return 0;
    }
    return created;
}

IPTR Lunapaint__DTM_WRITE(Class *cl, Object *obj, struct dtWrite *msg)
{
    UBYTE header[LUNAPAINT_WRITE_HEADER];
    UBYTE trailer[LUNAPAINT_WRITE_TRAILER];
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
    w.big_endian = lunapaint_native_big_endian();
    if (lunapaint_make_header(width, height, w.big_endian, header) != CODEC_OK)
        return FALSE;
    lunapaint_make_trailer(w.big_endian, trailer);
    w.file = msg->dtw_FileHandle;
    w.size = (LONG)lunapaint_row_size(width);
    w.out = AllocVec((ULONG)w.size, MEMF_ANY);
    if (w.out == NULL)
        return FALSE;
    success = dt_write(w.file, header, sizeof header) &&
              dt_each_row(cl, obj, write_row, &w) &&
              dt_write(w.file, trailer, sizeof trailer);
    FreeVec(w.out);
    return success;
}
