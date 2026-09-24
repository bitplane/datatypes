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

#define MAX_XBM_FILE (64L * 1024L * 1024L)

ADD2LIBS((const UBYTE *)"datatypes/picture.datatype", 0, struct Library *, PictureBase);

struct writer {
    BPTR file;
    struct xbm_encoder encoder;
    UBYTE *out;
    size_t capacity;
};

/* Make obj a one-plane picture: pen 0 is the white background, pen 1 black. */
static LONG put_bitmap(Class *cl, Object *obj, const struct xbm_image *image)
{
    static const UBYTE shades[2] = { 0xff, 0x00 };
    struct BitMapHeader *header = NULL;
    struct ColorRegister *colormap = NULL;
    ULONG *cregs = NULL;
    Point grab;
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
    /* The hotspot is what IFF calls the grab point. */
    if (image->hot_x >= 0 && image->hot_x <= 0x7fff && image->hot_y <= 0x7fff) {
        grab.x = (WORD)image->hot_x;
        grab.y = (WORD)image->hot_y;
        SetDTAttrs(obj, NULL, NULL, PDTA_Grab, &grab, TAG_END);
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

static LONG load_xbm(Class *cl, Object *obj)
{
    struct xbm_image image;
    UBYTE *input;
    LONG size, error;

    error = dt_read_file(obj, 1, MAX_XBM_FILE, &input, &size);
    if (error != 0 || input == NULL)
        return error;
    error = dt_error(xbm_decode(input, (size_t)size, &image));
    FreeVec(input);
    if (error != 0)
        return error;
    error = put_bitmap(cl, obj, &image);
    xbm_free(&image);
    if (error == 0)
        dt_set_name(obj);
    return error;
}

static BOOL write_row(void *state, const UBYTE *rgba, ULONG width)
{
    struct writer *w = state;
    size_t size = xbm_encode_row(&w->encoder, rgba, width, (char *)w->out, w->capacity);
    return size != SIZE_MAX && dt_write(w->file, w->out, (LONG)size);
}

IPTR XBM__OM_NEW(Class *cl, Object *obj, struct opSet *msg)
{
    return dt_new(cl, obj, msg, load_xbm);
}

IPTR XBM__DTM_WRITE(Class *cl, Object *obj, struct dtWrite *msg)
{
    char path[256], name[XBM_MAX_NAME + 1], header[XBM_HEADER_MAX];
    Point *grab = NULL;
    struct writer w;
    ULONG width, height;
    LONG hot_x = -1, hot_y = -1;
    size_t size;
    IPTR success;

    if (msg->dtw_Mode != DTWM_RAW)
        return DoSuperMethodA(cl, obj, (Msg)msg);
    if (msg->dtw_FileHandle == BNULL)
        return TRUE;
    if (!dt_picture_size(obj, &width, &height))
        return FALSE;
    /* The C identifiers are named after the file being written. */
    if (!NameFromFH(msg->dtw_FileHandle, (STRPTR)path, sizeof path))
        path[0] = '\0';
    xbm_make_name(path, name);
    /* A grab point of 0,0 is the default, so it is not written as a hotspot. */
    GetDTAttrs(obj, PDTA_Grab, &grab, TAG_END);
    if (grab != NULL && (grab->x != 0 || grab->y != 0) &&
        grab->x >= 0 && grab->y >= 0 &&
        (ULONG)grab->x < width && (ULONG)grab->y < height) {
        hot_x = grab->x;
        hot_y = grab->y;
    }
    size = xbm_make_header(name, width, height, hot_x, hot_y, header, sizeof header);
    if (size == 0)
        return FALSE;
    w.file = msg->dtw_FileHandle;
    w.capacity = xbm_row_capacity(width);
    w.out = AllocVec(w.capacity, MEMF_ANY);
    if (w.out == NULL)
        return FALSE;
    xbm_encoder_init(&w.encoder, width, height);
    success = dt_write(w.file, header, (LONG)size) &&
              dt_each_row(cl, obj, write_row, &w);
    FreeVec(w.out);
    return success;
}
