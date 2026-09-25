#include <aros/symbolsets.h>
#include <datatypes/datatypes.h>
#include <datatypes/datatypesclass.h>
#include <datatypes/pictureclass.h>
#include <dos/dos.h>
#include <exec/memory.h>
#include <graphics/gfx.h>
#include <proto/datatypes.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/utility.h>

#include "common/dtfile.h"
#include "common/dtpicture.h"
#include "decode.h"
#include "encode.h"

#define MAX_SUNICON_FILE (128L * 1024L * 1024L)

ADD2LIBS((const UBYTE *)"datatypes/picture.datatype", 0, struct Library *, PictureBase);

struct writer {
    BPTR file;
    struct sunicon_encoder encoder;
    UBYTE *out;
    size_t capacity;
};

/* Depth 1: pen 0 is the white background, pen 1 black.
   Depth 8: the palette is unknown, so pens are grey levels, as in netpbm. */
static LONG put_bitmap(Class *cl, Object *obj, const struct sunicon_image *image)
{
    struct BitMapHeader *header = NULL;
    struct ColorRegister *colormap = NULL;
    ULONG *cregs = NULL;
    ULONG colours = 1UL << image->depth, i;

    GetDTAttrs(obj, PDTA_BitMapHeader, &header, TAG_END);
    if (header == NULL)
        return ERROR_OBJECT_WRONG_TYPE;
    header->bmh_Width = (UWORD)image->width;
    header->bmh_Height = (UWORD)image->height;
    header->bmh_Depth = (UBYTE)image->depth;
    header->bmh_Masking = mskNone;
    SetDTAttrs(obj, NULL, NULL, PDTA_NumColors, colours, TAG_END);
    GetDTAttrs(obj, PDTA_ColorRegisters, &colormap, PDTA_CRegs, &cregs, TAG_END);
    if (colormap == NULL || cregs == NULL)
        return ERROR_OBJECT_WRONG_TYPE;
    for (i = 0; i < colours; i++) {
        UBYTE shade = image->depth == 1 ? (i == 0 ? 0xff : 0x00) : (UBYTE)i;
        colormap[i].red = colormap[i].green = colormap[i].blue = shade;
        cregs[i * 3] = cregs[i * 3 + 1] = cregs[i * 3 + 2] = shade * 0x01010101UL;
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

static LONG load_sunicon(Class *cl, Object *obj)
{
    struct sunicon_image image;
    UBYTE *input;
    LONG size, error;

    error = dt_read_file(obj, 1, MAX_SUNICON_FILE, &input, &size);
    if (error != 0 || input == NULL)
        return error;
    error = dt_error(sunicon_decode(input, (size_t)size, &image));
    FreeVec(input);
    if (error != 0)
        return error;
    error = put_bitmap(cl, obj, &image);
    sunicon_free(&image);
    if (error == 0)
        dt_set_name(obj);
    return error;
}

static BOOL write_row(void *state, const UBYTE *rgba, ULONG width)
{
    struct writer *w = state;
    size_t size = sunicon_encode_row(&w->encoder, rgba, width, (char *)w->out, w->capacity);
    return size != SIZE_MAX && dt_write(w->file, w->out, (LONG)size);
}

IPTR SUNICON__OM_NEW(Class *cl, Object *obj, struct opSet *msg)
{
    return dt_new(cl, obj, msg, load_sunicon);
}

IPTR SUNICON__DTM_WRITE(Class *cl, Object *obj, struct dtWrite *msg)
{
    char header[SUNICON_HEADER_MAX];
    struct writer w;
    ULONG width, height;
    size_t size;
    IPTR success;

    if (msg->dtw_Mode != DTWM_RAW)
        return DoSuperMethodA(cl, obj, (Msg)msg);
    if (msg->dtw_FileHandle == BNULL)
        return TRUE;
    if (!dt_picture_size(obj, &width, &height))
        return FALSE;
    size = sunicon_make_header(width, height, header, sizeof header);
    if (size == 0)
        return FALSE;
    w.file = msg->dtw_FileHandle;
    w.capacity = sunicon_row_capacity(width);
    w.out = AllocVec(w.capacity, MEMF_ANY);
    if (w.out == NULL)
        return FALSE;
    sunicon_encoder_init(&w.encoder, width, height);
    success = dt_write(w.file, header, (LONG)size) &&
              dt_each_row(cl, obj, write_row, &w);
    FreeVec(w.out);
    return success;
}
