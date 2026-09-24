#include <datatypes/datatypes.h>
#include <datatypes/datatypesclass.h>
#include <dos/dos.h>
#include <exec/memory.h>
#include <proto/datatypes.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/utility.h>

#include "common/dtfile.h"

IPTR dt_new(Class *cl, Object *obj, struct opSet *msg,
            LONG (*load)(Class *cl, Object *obj))
{
    IPTR created = DoSuperMethodA(cl, obj, (Msg)msg);
    LONG error;
    if (created == 0)
        return 0;
    error = load(cl, (Object *)created);
    if (error != 0) {
        CoerceMethod(cl, (Object *)created, OM_DISPOSE);
        SetIoErr(error);
        return 0;
    }
    return created;
}

LONG dt_read_file(Object *obj, LONG min, LONG max, UBYTE **data, LONG *size)
{
    IPTR source_type = 0;
    BPTR file = BNULL;
    UBYTE *input;
    LONG length;

    *data = NULL;
    *size = 0;
    GetDTAttrs(obj, DTA_SourceType, &source_type, DTA_Handle, &file, TAG_END);
    if (source_type == DTST_RAM)
        return 0;
    if (source_type != DTST_FILE || file == BNULL)
        return ERROR_OBJECT_WRONG_TYPE;
    /* Seek returns the previous position, so the second call reports the end. */
    if (Seek(file, 0, OFFSET_END) == -1)
        return DTERROR_COULDNT_OPEN;
    length = Seek(file, 0, OFFSET_CURRENT);
    if (length == -1 || Seek(file, 0, OFFSET_BEGINNING) == -1)
        return DTERROR_COULDNT_OPEN;
    if (length < min)
        return DTERROR_NOT_ENOUGH_DATA;
    if (length > max)
        return ERROR_OBJECT_TOO_LARGE;
    input = AllocVec((ULONG)length, MEMF_ANY);
    if (input == NULL)
        return ERROR_NO_FREE_STORE;
    if (Read(file, input, length) != length) {
        FreeVec(input);
        return DTERROR_NOT_ENOUGH_DATA;
    }
    *data = input;
    *size = length;
    return 0;
}

LONG dt_error(enum codec_result result)
{
    switch (result) {
    case CODEC_OK: return 0;
    case CODEC_TRUNCATED: return DTERROR_NOT_ENOUGH_DATA;
    case CODEC_TOO_LARGE: return ERROR_OBJECT_TOO_LARGE;
    case CODEC_NO_MEMORY: return ERROR_NO_FREE_STORE;
    default: return DTERROR_INVALID_DATA;
    }
}

void dt_set_name(Object *obj)
{
    STRPTR name = NULL;
    GetDTAttrs(obj, DTA_Name, &name, TAG_END);
    if (name != NULL)
        SetDTAttrs(obj, NULL, NULL, DTA_ObjName, FilePart(name), TAG_END);
}

BOOL dt_write(BPTR file, const void *data, LONG size)
{
    return size == 0 || Write(file, (APTR)data, size) == size;
}
