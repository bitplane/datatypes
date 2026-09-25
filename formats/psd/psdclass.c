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

ADD2LIBS((const UBYTE *)"datatypes/picture.datatype", 0, struct Library *, PictureBase);

/* Most of a Photoshop file is layer data, so the decoder reads the parts it
   needs from the file instead of loading all of it. */
static size_t read_file(void *context, uint64_t offset, void *buffer,
                        size_t length)
{
    BPTR file = *(BPTR *)context;
    LONG got;

    if (offset > 0x7fffffffUL || length > 0x7fffffffUL ||
        Seek(file, (LONG)offset, OFFSET_BEGINNING) == -1)
        return 0;
    got = Read(file, buffer, (LONG)length);
    return got < 0 ? 0 : (size_t)got;
}

static LONG load_psd(Class *cl, Object *obj)
{
    IPTR source_type = 0;
    BPTR file = BNULL;
    struct psd_source source;
    struct psd_image image;
    LONG size, error;

    GetDTAttrs(obj, DTA_SourceType, &source_type, DTA_Handle, &file, TAG_END);
    if (source_type == DTST_RAM)
        return 0;
    if (source_type != DTST_FILE || file == BNULL)
        return ERROR_OBJECT_WRONG_TYPE;
    /* Seek returns the previous position, so the second call reports the end. */
    if (Seek(file, 0, OFFSET_END) == -1)
        return DTERROR_COULDNT_OPEN;
    size = Seek(file, 0, OFFSET_BEGINNING);
    if (size == -1)
        return DTERROR_COULDNT_OPEN;
    if (size < 0)
        return ERROR_OBJECT_TOO_LARGE;
    source.read = read_file;
    source.context = &file;
    source.size = (uint64_t)size;
    error = dt_error(psd_decode(&source, &image));
    if (error != 0)
        return error;
    error = dt_put_rgba(cl, obj, image.rgba, image.width, image.height);
    psd_free(&image);
    if (error == 0)
        dt_set_name(obj);
    return error;
}

IPTR Psd__OM_NEW(Class *cl, Object *obj, struct opSet *msg)
{
    return dt_new(cl, obj, msg, load_psd);
}
