#include <datatypes/datatypes.h>
#include <datatypes/datatypesclass.h>
#include <datatypes/pictureclass.h>
#include <dos/dos.h>
#include <exec/memory.h>
#include <proto/alib.h>
#include <proto/datatypes.h>
#include <proto/dos.h>
#include <proto/exec.h>

#include <stdio.h>

#include "common/dtembed.h"
#include "common/dtfile.h"

#define EMBED_MAX_SIDE 65535u
#define EMBED_MAX_PIXELS (16u * 1024u * 1024u)

LONG dt_load_embedded(const UBYTE *data, ULONG size, const char *suffix,
                      UBYTE **rgba, ULONG *width, ULONG *height)
{
    static ULONG serial;
    char name[80];
    struct BitMapHeader *header = NULL;
    Object *picture;
    BPTR file;
    BOOL written;
    LONG error = 0;
    ULONG w, h;

    *rgba = NULL;
    *width = *height = 0;
    /* The task and this class's own counter keep names apart between classes. */
    snprintf(name, sizeof name, "T:dt_%lx_%lx_%lu.%s",
             (unsigned long)(IPTR)FindTask(NULL), (unsigned long)(IPTR)&serial,
             (unsigned long)++serial, suffix);
    file = Open((CONST_STRPTR)name, MODE_NEWFILE);
    if (file == BNULL)
        return IoErr() != 0 ? IoErr() : DTERROR_COULDNT_OPEN;
    written = dt_write(file, data, (LONG)size);
    if (!Close(file))
        written = FALSE;
    picture = written ? NewDTObject((APTR)name,
                                    DTA_SourceType, DTST_FILE,
                                    DTA_GroupID, GID_PICTURE,
                                    PDTA_Remap, FALSE,
                                    PDTA_DestMode, PMODE_V43,
                                    TAG_END)
                      : NULL;
    if (picture == NULL) {
        error = IoErr() != 0 ? IoErr() : DTERROR_INVALID_DATA;
        DeleteFile((CONST_STRPTR)name);
        return error;
    }
    GetDTAttrs(picture, PDTA_BitMapHeader, &header, TAG_END);
    if (header == NULL || header->bmh_Width == 0 || header->bmh_Height == 0) {
        error = DTERROR_INVALID_DATA;
    } else {
        w = header->bmh_Width;
        h = header->bmh_Height;
        if (w > EMBED_MAX_SIDE || h > EMBED_MAX_SIDE || (UQUAD)w * h > EMBED_MAX_PIXELS) {
            error = DTERROR_INVALID_DATA;
        } else {
            *rgba = AllocVec((ULONG)w * h * 4u, MEMF_ANY);
            if (*rgba == NULL)
                error = ERROR_NO_FREE_STORE;
            else if (!DoMethod(picture, PDTM_READPIXELARRAY, (IPTR)*rgba,
                               PBPAFMT_RGBA, w * 4u, 0, 0, w, h))
                error = DTERROR_INVALID_DATA;
            if (error == 0) {
                *width = w;
                *height = h;
            } else if (*rgba != NULL) {
                FreeVec(*rgba);
                *rgba = NULL;
            }
        }
    }
    DisposeDTObject(picture);
    DeleteFile((CONST_STRPTR)name);
    return error;
}
