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
#include "decode.h"
#include "encode.h"

#define MAX_PCX_FILE (128L * 1024L * 1024L)

ADD2LIBS((const UBYTE *)"datatypes/picture.datatype", 0, struct Library *, PictureBase);

static LONG load_pcx(Class *cl, Object *obj)
{
    struct BitMapHeader *header = NULL;
    struct pcx_image image;
    IPTR source_type = 0;
    BPTR file = BNULL;
    STRPTR name = NULL;
    UBYTE *input;
    LONG size, count, error = 0;
    enum pcx_result result;

    GetDTAttrs(obj,
               DTA_SourceType, &source_type,
               DTA_Handle, &file,
               DTA_Name, &name,
               PDTA_BitMapHeader, &header,
               TAG_END);
    if (source_type == DTST_RAM)
        return 0;
    if (source_type != DTST_FILE || file == BNULL || header == NULL)
        return ERROR_OBJECT_WRONG_TYPE;
    if (Seek(file, 0, OFFSET_END) == -1)
        return DTERROR_COULDNT_OPEN;
    size = Seek(file, 0, OFFSET_CURRENT);
    if (size == -1 || Seek(file, 0, OFFSET_BEGINNING) == -1)
        return DTERROR_COULDNT_OPEN;
    if (size < 128)
        return DTERROR_NOT_ENOUGH_DATA;
    if (size > MAX_PCX_FILE)
        return ERROR_OBJECT_TOO_LARGE;
    input = AllocVec((ULONG)size, MEMF_ANY);
    if (input == NULL)
        return ERROR_NO_FREE_STORE;
    count = Read(file, input, size);
    if (count != size) {
        error = DTERROR_NOT_ENOUGH_DATA;
        goto done;
    }
    result = pcx_decode(input, (size_t)size, &image);
    if (result != PCX_OK) {
        switch (result) {
        case PCX_TRUNCATED: error = DTERROR_NOT_ENOUGH_DATA; break;
        case PCX_TOO_LARGE: error = ERROR_OBJECT_TOO_LARGE; break;
        case PCX_NO_MEMORY: error = ERROR_NO_FREE_STORE; break;
        default: error = DTERROR_INVALID_DATA; break;
        }
        goto done;
    }
    header->bmh_Width = (UWORD)image.width;
    header->bmh_Height = (UWORD)image.height;
    header->bmh_Depth = 32;
    header->bmh_Masking = mskHasAlpha;
    SetDTAttrs(obj, NULL, NULL,
               DTA_NominalHoriz, image.width,
               DTA_NominalVert, image.height,
               PDTA_SourceMode, PMODE_V43,
               TAG_END);
    if (!DoSuperMethod(cl, obj, PDTM_WRITEPIXELARRAY,
                       image.rgba, PBPAFMT_RGBA, image.width * 4u,
                       0, 0, image.width, image.height))
        error = DTERROR_INVALID_DATA;
    if (!error && name != NULL)
        SetDTAttrs(obj, NULL, NULL, DTA_ObjName, FilePart(name), TAG_END);
    pcx_free(&image);
done:
    FreeVec(input);
    return error;
}

IPTR PCX__OM_NEW(Class *cl, Object *obj, struct opSet *msg)
{
    IPTR created = DoSuperMethodA(cl, obj, (Msg)msg);
    LONG error;
    if (created == 0)
        return 0;
    error = load_pcx(cl, (Object *)created);
    if (error != 0) {
        CoerceMethod(cl, (Object *)created, OM_DISPOSE);
        SetIoErr(error);
        return 0;
    }
    return created;
}

IPTR PCX__DTM_WRITE(Class *cl, Object *obj, struct dtWrite *msg)
{
    UBYTE header[128];
    struct BitMapHeader *bitmap = NULL;
    UBYTE *row, *encoded;
    ULONG width, height, y;
    size_t capacity, encoded_size;
    IPTR success = FALSE;

    if (msg->dtw_Mode != DTWM_RAW)
        return DoSuperMethodA(cl, obj, (Msg)msg);
    if (msg->dtw_FileHandle == BNULL)
        return TRUE;
    GetDTAttrs(obj, PDTA_BitMapHeader, &bitmap, TAG_END);
    if (bitmap == NULL || !pcx_make_header(bitmap->bmh_Width,
                                           bitmap->bmh_Height, header))
        return FALSE;
    width = bitmap->bmh_Width;
    height = bitmap->bmh_Height;
    capacity = (size_t)((width + 1u) & ~1u) * 6u;
    row = AllocVec(width * 4u, MEMF_ANY);
    if (row == NULL)
        return FALSE;
    encoded = AllocVec(capacity, MEMF_ANY);
    if (encoded == NULL)
        goto done;
    if (Write(msg->dtw_FileHandle, header, sizeof header) != sizeof header)
        goto free_encoded;
    for (y = 0; y < height; y++) {
        if (!DoSuperMethod(cl, obj, PDTM_READPIXELARRAY,
                           row, PBPAFMT_RGBA, width * 4u,
                           0, y, width, 1))
            goto free_encoded;
        encoded_size = pcx_encode_row(row, width, encoded, capacity);
        if (encoded_size == 0 ||
            Write(msg->dtw_FileHandle, encoded, encoded_size) != (LONG)encoded_size)
            goto free_encoded;
    }
    success = TRUE;
free_encoded:
    FreeVec(encoded);
done:
    FreeVec(row);
    return success;
}
