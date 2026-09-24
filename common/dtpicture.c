#include <datatypes/datatypes.h>
#include <datatypes/datatypesclass.h>
#include <datatypes/pictureclass.h>
#include <dos/dos.h>
#include <exec/memory.h>
#include <proto/datatypes.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/utility.h>

#include "common/dtpicture.h"

LONG dt_put_rgba(Class *cl, Object *obj, const UBYTE *rgba,
                 ULONG width, ULONG height)
{
    struct BitMapHeader *header = NULL;

    GetDTAttrs(obj, PDTA_BitMapHeader, &header, TAG_END);
    if (header == NULL)
        return ERROR_OBJECT_WRONG_TYPE;
    header->bmh_Width = (UWORD)width;
    header->bmh_Height = (UWORD)height;
    header->bmh_Depth = 32;
    header->bmh_Masking = mskHasAlpha;
    SetDTAttrs(obj, NULL, NULL,
               DTA_NominalHoriz, width,
               DTA_NominalVert, height,
               PDTA_SourceMode, PMODE_V43,
               TAG_END);
    if (!DoSuperMethod(cl, obj, PDTM_WRITEPIXELARRAY,
                       rgba, PBPAFMT_RGBA, width * 4u,
                       0, 0, width, height))
        return DTERROR_INVALID_DATA;
    return 0;
}

BOOL dt_picture_size(Object *obj, ULONG *width, ULONG *height)
{
    struct BitMapHeader *header = NULL;

    GetDTAttrs(obj, PDTA_BitMapHeader, &header, TAG_END);
    if (header == NULL || header->bmh_Width == 0 || header->bmh_Height == 0)
        return FALSE;
    *width = header->bmh_Width;
    *height = header->bmh_Height;
    return TRUE;
}

BOOL dt_each_row(Class *cl, Object *obj, dt_row_fn *fn, void *state)
{
    ULONG width, height, y;
    UBYTE *row;

    if (!dt_picture_size(obj, &width, &height))
        return FALSE;
    row = AllocVec(width * 4u, MEMF_ANY);
    if (row == NULL)
        return FALSE;
    for (y = 0; y < height; y++) {
        if (!DoSuperMethod(cl, obj, PDTM_READPIXELARRAY,
                           row, PBPAFMT_RGBA, width * 4u,
                           0, y, width, 1) ||
            !fn(state, row, width))
            break;
    }
    FreeVec(row);
    return y == height;
}
