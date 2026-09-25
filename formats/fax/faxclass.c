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

#define MAX_FAX_FILE (32L * 1024L * 1024L)
#define BAND_BYTES 65536UL

ADD2LIBS((const UBYTE *)"datatypes/picture.datatype", 0, struct Library *, PictureBase);

struct writer {
    BPTR file;
    struct fax_encoder encoder;
    UBYTE *out;
    size_t capacity;
};

/* Pass the packed pixels on as pens, a band of rows at a time, so a large
   drawing needs no second whole-page copy here. */
static LONG put_pixels(Class *cl, Object *obj, const struct fax_image *image)
{
    ULONG band = BAND_BYTES / image->width, y, rows, x, r;
    UBYTE *pens;
    LONG error = 0;

    if (band == 0)
        band = 1;
    if (band > image->height)
        band = image->height;
    pens = AllocVec(band * image->width, MEMF_ANY);
    if (pens == NULL)
        return ERROR_NO_FREE_STORE;
    for (y = 0; y < image->height && error == 0; y += rows) {
        rows = image->height - y < band ? image->height - y : band;
        for (r = 0; r < rows; r++)
            for (x = 0; x < image->width; x++)
                pens[r * image->width + x] = (UBYTE)fax_pixel(image, x, y + r);
        if (!DoSuperMethod(cl, obj, PDTM_WRITEPIXELARRAY,
                           pens, PBPAFMT_LUT8, image->width,
                           0, y, image->width, rows))
            error = DTERROR_INVALID_DATA;
    }
    FreeVec(pens);
    return error;
}

/* Make obj a one-plane picture: pen 0 is the white paper, pen 1 black. */
static LONG put_bitmap(Class *cl, Object *obj, const struct fax_image *image)
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
    return put_pixels(cl, obj, image);
}

static LONG load_fax(Class *cl, Object *obj)
{
    struct fax_image image;
    UBYTE *input;
    LONG size, error;

    error = dt_read_file(obj, 2, MAX_FAX_FILE, &input, &size);
    if (error != 0 || input == NULL)
        return error;
    error = dt_error(fax_decode(input, (size_t)size, &image));
    FreeVec(input);
    if (error != 0)
        return error;
    error = put_bitmap(cl, obj, &image);
    fax_free(&image);
    if (error == 0)
        dt_set_name(obj);
    return error;
}

static BOOL write_row(void *state, const UBYTE *rgba, ULONG width)
{
    struct writer *w = state;
    size_t size = fax_encode_row(&w->encoder, rgba, width, w->out, w->capacity);
    return size != SIZE_MAX && dt_write(w->file, w->out, (LONG)size);
}

IPTR FAX__OM_NEW(Class *cl, Object *obj, struct opSet *msg)
{
    return dt_new(cl, obj, msg, load_fax);
}

IPTR FAX__DTM_WRITE(Class *cl, Object *obj, struct dtWrite *msg)
{
    UBYTE end[FAX_END_MAX];
    struct writer w;
    ULONG width, height;
    IPTR success;

    if (msg->dtw_Mode != DTWM_RAW)
        return DoSuperMethodA(cl, obj, (Msg)msg);
    if (msg->dtw_FileHandle == BNULL)
        return TRUE;
    if (!dt_picture_size(obj, &width, &height) || width > 65535)
        return FALSE;
    w.file = msg->dtw_FileHandle;
    w.capacity = fax_row_capacity(width);
    w.out = AllocVec(w.capacity, MEMF_ANY);
    if (w.out == NULL)
        return FALSE;
    fax_encoder_init(&w.encoder);
    success = dt_each_row(cl, obj, write_row, &w) &&
              dt_write(w.file, end, (LONG)fax_encode_end(&w.encoder, end));
    FreeVec(w.out);
    return success;
}
