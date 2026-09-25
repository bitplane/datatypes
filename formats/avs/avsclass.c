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

/* Several images of up to 16M pixels each, 4 bytes per pixel. */
#define MAX_AVS_FILE (256L * 1024L * 1024L)

ADD2LIBS((const UBYTE *)"datatypes/picture.datatype", 0, struct Library *, PictureBase);

struct writer {
    BPTR file;
    UBYTE *out;
    enum avs_variant variant;
};

/* Load image index; count, if given, receives the number of images. */
static LONG load_avs(Class *cl, Object *obj, ULONG index, ULONG *count)
{
    struct avs_image image;
    UBYTE *input;
    LONG size, error;
    ULONG images;

    error = dt_read_file(obj, 8, MAX_AVS_FILE, &input, &size);
    if (error != 0 || input == NULL)
        return error;
    images = avs_count(input, (size_t)size);
    if (count != NULL)
        *count = images;
    /* The superclass kept the count pointer from the tags; store the count. */
    SetDTAttrs(obj, NULL, NULL, PDTA_GetNumPictures, images, TAG_END);
    error = dt_error(avs_decode(input, (size_t)size, (unsigned)index, &image));
    FreeVec(input);
    if (error != 0)
        return error;
    error = dt_put_rgba(cl, obj, image.rgba, image.width, image.height);
    avs_free(&image);
    if (error == 0)
        dt_set_name(obj);
    return error;
}

/* Save in the variant the object was loaded from: AAI for a .aai file. */
static enum avs_variant save_variant(Object *obj)
{
    STRPTR name = NULL;
    ULONG length;

    GetDTAttrs(obj, DTA_Name, &name, TAG_END);
    if (name != NULL) {
        length = strlen((const char *)name);
        if (length >= 4 && Stricmp(name + length - 4, (STRPTR)".aai") == 0)
            return AVS_VARIANT_AAI;
    }
    return AVS_VARIANT_AVS;
}

static BOOL write_row(void *state, const UBYTE *rgba, ULONG width)
{
    struct writer *w = state;
    avs_encode_row(w->variant, rgba, width, w->out);
    return dt_write(w->file, w->out, (LONG)(width * 4u));
}

IPTR Avs__OM_NEW(Class *cl, Object *obj, struct opSet *msg)
{
    ULONG index = GetTagData(PDTA_WhichPicture, 0, msg->ops_AttrList);
    ULONG *count = (ULONG *)GetTagData(PDTA_GetNumPictures, 0,
                                       msg->ops_AttrList);
    IPTR created = DoSuperMethodA(cl, obj, (Msg)msg);
    LONG error;

    if (created == 0)
        return 0;
    error = load_avs(cl, (Object *)created, index, count);
    if (error != 0) {
        CoerceMethod(cl, (Object *)created, OM_DISPOSE);
        SetIoErr(error);
        return 0;
    }
    return created;
}

IPTR Avs__DTM_WRITE(Class *cl, Object *obj, struct dtWrite *msg)
{
    UBYTE header[8];
    struct writer w;
    ULONG width, height;
    IPTR success;

    if (msg->dtw_Mode != DTWM_RAW)
        return DoSuperMethodA(cl, obj, (Msg)msg);
    /* MultiView probes RAW support without opening an output file. */
    if (msg->dtw_FileHandle == BNULL)
        return TRUE;
    w.variant = save_variant(obj);
    if (!dt_picture_size(obj, &width, &height) ||
        !avs_make_header(w.variant, width, height, header))
        return FALSE;
    w.file = msg->dtw_FileHandle;
    w.out = AllocVec(width * 4u, MEMF_ANY);
    if (w.out == NULL)
        return FALSE;
    success = dt_write(w.file, header, sizeof header) &&
              dt_each_row(cl, obj, write_row, &w);
    FreeVec(w.out);
    return success;
}
