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

/* 4095 rows of 4095 pixels padded to 32 bits, plus room for trailing data. */
#define MAX_MGR_FILE (4L * 1024L * 1024L)

ADD2LIBS((const UBYTE *)"datatypes/picture.datatype", 0, struct Library *, PictureBase);

struct writer {
    BPTR file;
    UBYTE out[MGR_ROW_MAX];
};

static LONG put_bitmap(Class *cl, Object *obj, const struct mgr_image *image)
{
    static const UBYTE shades[2] = { 0xff, 0x00 };
    struct BitMapHeader *header = NULL;
    struct ColorRegister *colormap = NULL;
    ULONG *cregs = NULL;
    int i;

    GetDTAttrs(obj, PDTA_BitMapHeader, &header, TAG_END);
    if (header == NULL)
        return ERROR_OBJECT_WRONG_TYPE;
    header->bmh_Width = (UWORD)image->width;
    header->bmh_Height = (UWORD)image->height;
    header->bmh_Depth = 1;
    header->bmh_Masking = mskNone;
    SetDTAttrs(obj, NULL, NULL, PDTA_NumColors, 2, TAG_END);
    GetDTAttrs(obj, PDTA_ColorRegisters, &colormap, PDTA_CRegs, &cregs, TAG_END);
    if (colormap == NULL || cregs == NULL)
        return ERROR_OBJECT_WRONG_TYPE;
    for (i = 0; i < 2; i++) {
        colormap[i].red = colormap[i].green = colormap[i].blue = shades[i];
        cregs[i * 3] = cregs[i * 3 + 1] = cregs[i * 3 + 2] = shades[i] * 0x01010101UL;
    }
    SetDTAttrs(obj, NULL, NULL,
               DTA_NominalHoriz, image->width,
               DTA_NominalVert, image->height,
               PDTA_SourceMode, PMODE_V43,
               TAG_END);
    if (!DoSuperMethod(cl, obj, PDTM_WRITEPIXELARRAY,
                       image->pixels, PBPAFMT_LUT8, image->width,
                       0, 0, image->width, image->height))
        return DTERROR_INVALID_DATA;
    return 0;
}

static LONG load_mgr(Class *cl, Object *obj)
{
    struct mgr_image image;
    UBYTE *input;
    LONG size, error;

    error = dt_read_file(obj, MGR_OLD_HEADER_SIZE, MAX_MGR_FILE, &input, &size);
    if (error != 0 || input == NULL)
        return error;
    error = dt_error(mgr_decode(input, (size_t)size, &image));
    FreeVec(input);
    if (error != 0)
        return error;
    error = put_bitmap(cl, obj, &image);
    mgr_free(&image);
    if (error == 0)
        dt_set_name(obj);
    return error;
}

static BOOL write_row(void *state, const UBYTE *rgba, ULONG width)
{
    struct writer *w = state;
    size_t size = mgr_encode_row(rgba, width, w->out, sizeof w->out);
    return size != 0 && dt_write(w->file, w->out, (LONG)size);
}

IPTR Mgr__OM_NEW(Class *cl, Object *obj, struct opSet *msg)
{
    return dt_new(cl, obj, msg, load_mgr);
}

IPTR Mgr__DTM_WRITE(Class *cl, Object *obj, struct dtWrite *msg)
{
    UBYTE header[MGR_HEADER_SIZE];
    struct writer w;
    ULONG width, height;

    if (msg->dtw_Mode != DTWM_RAW)
        return DoSuperMethodA(cl, obj, (Msg)msg);
    if (msg->dtw_FileHandle == BNULL)
        return TRUE;
    if (!dt_picture_size(obj, &width, &height) ||
        !mgr_make_header(header, width, height))
        return FALSE;
    w.file = msg->dtw_FileHandle;
    return dt_write(w.file, header, sizeof header) &&
           dt_each_row(cl, obj, write_row, &w);
}
