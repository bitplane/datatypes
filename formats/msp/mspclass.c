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

/* 16M pixels unpack to 2M bytes. Even packed at three bytes per byte, with the
   header and row map, that stays under this. */
#define MAX_MSP_FILE (32L * 1024L * 1024L)

ADD2LIBS((const UBYTE *)"datatypes/picture.datatype", 0, struct Library *, PictureBase);

struct writer {
    BPTR file;
    UBYTE *out;
    size_t capacity;
};

/* Make obj a one-plane picture: pen 0 black, pen 1 white, as in the file. */
static LONG put_bitmap(Class *cl, Object *obj, const struct msp_image *image)
{
    static const UBYTE shades[2] = { 0x00, 0xff };
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

static LONG load_msp(Class *cl, Object *obj)
{
    struct msp_image image;
    UBYTE *input;
    LONG size, error;

    error = dt_read_file(obj, 32, MAX_MSP_FILE, &input, &size);
    if (error != 0 || input == NULL)
        return error;
    error = dt_error(msp_decode(input, (size_t)size, &image));
    FreeVec(input);
    if (error != 0)
        return error;
    error = put_bitmap(cl, obj, &image);
    msp_free(&image);
    if (error == 0)
        dt_set_name(obj);
    return error;
}

static BOOL write_row(void *state, const UBYTE *rgba, ULONG width)
{
    struct writer *w = state;
    size_t size = msp_encode_row(rgba, width, w->out, w->capacity);
    return size != 0 && dt_write(w->file, w->out, (LONG)size);
}

IPTR MSP__OM_NEW(Class *cl, Object *obj, struct opSet *msg)
{
    return dt_new(cl, obj, msg, load_msp);
}

IPTR MSP__DTM_WRITE(Class *cl, Object *obj, struct dtWrite *msg)
{
    UBYTE header[MSP_HEADER_SIZE];
    struct writer w;
    ULONG width, height;
    IPTR success;

    if (msg->dtw_Mode != DTWM_RAW)
        return DoSuperMethodA(cl, obj, (Msg)msg);
    /* MultiView probes RAW support without opening an output file. */
    if (msg->dtw_FileHandle == BNULL)
        return TRUE;
    if (!dt_picture_size(obj, &width, &height) ||
        !msp_make_header(width, height, header))
        return FALSE;
    w.file = msg->dtw_FileHandle;
    w.capacity = (width + 7u) / 8u;
    w.out = AllocVec(w.capacity, MEMF_ANY);
    if (w.out == NULL)
        return FALSE;
    success = dt_write(w.file, header, sizeof header) &&
              dt_each_row(cl, obj, write_row, &w);
    FreeVec(w.out);
    return success;
}
