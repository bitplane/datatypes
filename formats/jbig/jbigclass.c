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

#define MAX_JBIG_FILE (32L * 1024L * 1024L)
#define OUT_BYTES 4096

ADD2LIBS((const UBYTE *)"datatypes/picture.datatype", 0, struct Library *, PictureBase);

struct writer {
    BPTR file;
    struct jbig_encoder encoder;
    ULONG used;
    UBYTE out[OUT_BYTES];
};

/* Make obj a picture of pens: white paper and black ink for one plane, or
   256 greys for several. The decoder's pixels are those pens already. */
static LONG put_picture(Class *cl, Object *obj, const struct jbig_image *image)
{
    struct BitMapHeader *header = NULL;
    struct ColorRegister *colormap = NULL;
    ULONG *cregs = NULL, colours = image->planes == 1 ? 2 : 256, i;

    GetDTAttrs(obj, PDTA_BitMapHeader, &header, TAG_END);
    if (header == NULL)
        return ERROR_OBJECT_WRONG_TYPE;
    header->bmh_Width = (UWORD)image->width;
    header->bmh_Height = (UWORD)image->height;
    header->bmh_Depth = image->planes == 1 ? 1 : 8;
    header->bmh_Masking = mskNone;
    SetDTAttrs(obj, NULL, NULL, PDTA_NumColors, colours, TAG_END);
    GetDTAttrs(obj, PDTA_ColorRegisters, &colormap, PDTA_CRegs, &cregs, TAG_END);
    if (colormap == NULL || cregs == NULL)
        return ERROR_OBJECT_WRONG_TYPE;
    for (i = 0; i < colours; i++) {
        UBYTE level = image->planes == 1 ? (i == 0 ? 0xff : 0x00) : (UBYTE)i;
        colormap[i].red = colormap[i].green = colormap[i].blue = level;
        cregs[i * 3] = cregs[i * 3 + 1] = cregs[i * 3 + 2] = level * 0x01010101UL;
    }
    SetDTAttrs(obj, NULL, NULL,
               DTA_NominalHoriz, image->width,
               DTA_NominalVert, image->height,
               PDTA_SourceMode, PMODE_V43,
               TAG_END);
    if (!DoSuperMethod(cl, obj, PDTM_WRITEPIXELARRAY, image->pixels, PBPAFMT_LUT8,
                       image->width, 0, 0, image->width, image->height))
        return DTERROR_INVALID_DATA;
    return 0;
}

static LONG load_jbig(Class *cl, Object *obj)
{
    struct jbig_image image;
    UBYTE *input;
    LONG size, error;

    error = dt_read_file(obj, JBIG_HEADER_SIZE, MAX_JBIG_FILE, &input, &size);
    if (error != 0 || input == NULL)
        return error;
    error = dt_error(jbig_decode(input, (size_t)size, &image));
    FreeVec(input);
    if (error != 0)
        return error;
    error = put_picture(cl, obj, &image);
    jbig_free(&image);
    if (error == 0)
        dt_set_name(obj);
    return error;
}

static int put_byte(void *state, uint8_t byte)
{
    struct writer *w = state;
    if (w->used == OUT_BYTES) {
        if (!dt_write(w->file, w->out, OUT_BYTES))
            return 0;
        w->used = 0;
    }
    w->out[w->used++] = byte;
    return 1;
}

static BOOL write_row(void *state, const UBYTE *rgba, ULONG width)
{
    struct writer *w = state;
    (void)width;
    jbig_encode_rgba(&w->encoder, rgba);
    return !w->encoder.qm.failed;
}

IPTR JBIG__OM_NEW(Class *cl, Object *obj, struct opSet *msg)
{
    return dt_new(cl, obj, msg, load_jbig);
}

IPTR JBIG__DTM_WRITE(Class *cl, Object *obj, struct dtWrite *msg)
{
    UBYTE header[JBIG_HEADER_SIZE];
    struct writer *w;
    ULONG width, height;
    IPTR success;

    if (msg->dtw_Mode != DTWM_RAW)
        return DoSuperMethodA(cl, obj, (Msg)msg);
    /* MultiView probes RAW support without opening an output file. */
    if (msg->dtw_FileHandle == BNULL)
        return TRUE;
    if (!dt_picture_size(obj, &width, &height) || width > 65535 || height > 65535)
        return FALSE;
    w = AllocVec(sizeof *w, MEMF_ANY);
    if (w == NULL)
        return FALSE;
    w->file = msg->dtw_FileHandle;
    w->used = 0;
    if (!jbig_encoder_init(&w->encoder, width, height, JBIG_TYPICAL, put_byte, w)) {
        jbig_encoder_free(&w->encoder);
        FreeVec(w);
        return FALSE;
    }
    jbig_make_header(header, width, height, JBIG_TYPICAL);
    success = dt_write(w->file, header, JBIG_HEADER_SIZE) &&
              dt_each_row(cl, obj, write_row, w);
    if (success)
        success = jbig_encoder_end(&w->encoder) && dt_write(w->file, w->out, (LONG)w->used);
    else
        jbig_encoder_free(&w->encoder);
    FreeVec(w);
    return success;
}
