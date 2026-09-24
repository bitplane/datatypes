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

#define MAX_TGA_FILE (128L * 1024L * 1024L)

ADD2LIBS((const UBYTE *)"datatypes/picture.datatype", 0, struct Library *, PictureBase);

static LONG load_targa(Class *cl, Object *obj)
{
    struct BitMapHeader *header = NULL;
    struct tga_image image;
    IPTR source_type = 0;
    BPTR file = BNULL;
    STRPTR name = NULL;
    UBYTE *input;
    LONG size, count, error = 0;
    enum tga_result result;

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
    if (size < 18)
        return DTERROR_NOT_ENOUGH_DATA;
    if (size > MAX_TGA_FILE)
        return ERROR_OBJECT_TOO_LARGE;
    input = AllocVec((ULONG)size, MEMF_ANY);
    if (input == NULL)
        return ERROR_NO_FREE_STORE;
    count = Read(file, input, size);
    if (count != size) {
        error = DTERROR_NOT_ENOUGH_DATA;
        goto done;
    }
    result = tga_decode(input, (size_t)size, &image);
    if (result != TGA_OK) {
        switch (result) {
        case TGA_TRUNCATED: error = DTERROR_NOT_ENOUGH_DATA; break;
        case TGA_TOO_LARGE: error = ERROR_OBJECT_TOO_LARGE; break;
        case TGA_NO_MEMORY: error = ERROR_NO_FREE_STORE; break;
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
    tga_free(&image);
done:
    FreeVec(input);
    return error;
}

IPTR Targa__OM_NEW(Class *cl, Object *obj, struct opSet *msg)
{
    IPTR created = DoSuperMethodA(cl, obj, (Msg)msg);
    LONG error;
    if (created == 0)
        return 0;
    error = load_targa(cl, (Object *)created);
    if (error != 0) {
        CoerceMethod(cl, (Object *)created, OM_DISPOSE);
        SetIoErr(error);
        return 0;
    }
    return created;
}

IPTR Targa__DTM_WRITE(Class *cl, Object *obj, struct dtWrite *msg)
{
    static const UBYTE footer[26] = {
        0, 0, 0, 0, 0, 0, 0, 0,
        'T', 'R', 'U', 'E', 'V', 'I', 'S', 'I', 'O', 'N',
        '-', 'X', 'F', 'I', 'L', 'E', '.', 0
    };
    UBYTE header[18] = { 0 };
    struct BitMapHeader *bitmap = NULL;
    UBYTE *row, *encoded;
    ULONG width, height, y, x;
    ULONG bytes_per_pixel = 3;
    size_t encoded_size;
    IPTR success = FALSE;

    if (msg->dtw_Mode != DTWM_RAW)
        return DoSuperMethodA(cl, obj, (Msg)msg);
    /* MultiView probes RAW support without opening an output file. */
    if (msg->dtw_FileHandle == BNULL)
        return TRUE;
    GetDTAttrs(obj, PDTA_BitMapHeader, &bitmap, TAG_END);
    if (bitmap == NULL || bitmap->bmh_Width == 0 || bitmap->bmh_Height == 0)
        return FALSE;
    width = bitmap->bmh_Width;
    height = bitmap->bmh_Height;
    row = AllocVec(width * 4u, MEMF_ANY);
    if (row == NULL)
        return FALSE;

    /* Pixel content, not the source file's depth, decides whether alpha is needed. */
    for (y = 0; y < height && bytes_per_pixel == 3; y++) {
        if (!DoSuperMethod(cl, obj, PDTM_READPIXELARRAY,
                           row, PBPAFMT_RGBA, width * 4u,
                           0, y, width, 1))
            goto done;
        for (x = 0; x < width; x++) {
            if (row[x * 4u + 3u] != 255) {
                bytes_per_pixel = 4;
                break;
            }
        }
    }
    encoded = AllocVec(width * (bytes_per_pixel + 1u), MEMF_ANY);
    if (encoded == NULL)
        goto done;

    header[2] = 10;            /* RLE true-color image. */
    header[12] = width & 255u;
    header[13] = width >> 8;
    header[14] = height & 255u;
    header[15] = height >> 8;
    header[16] = bytes_per_pixel * 8u;
    header[17] = bytes_per_pixel == 4 ? 0x28 : 0x20;
    if (Write(msg->dtw_FileHandle, header, sizeof header) != sizeof header)
        goto free_encoded;
    for (y = 0; y < height; y++) {
        if (!DoSuperMethod(cl, obj, PDTM_READPIXELARRAY,
                           row, PBPAFMT_RGBA, width * 4u,
                           0, y, width, 1))
            goto free_encoded;
        encoded_size = tga_encode_row(row, width, bytes_per_pixel, encoded,
                                      width * (bytes_per_pixel + 1u));
        if (encoded_size == 0 ||
            Write(msg->dtw_FileHandle, encoded, encoded_size) != (LONG)encoded_size)
            goto free_encoded;
    }
    success = Write(msg->dtw_FileHandle, footer, sizeof footer) == sizeof footer;
free_encoded:
    FreeVec(encoded);
done:
    FreeVec(row);
    return success;
}
