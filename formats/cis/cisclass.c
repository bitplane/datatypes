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

/* An image is at most 49152 runs; allow plenty of line noise and trailing
   junk around them. */
#define MAX_CIS_FILE (1024L * 1024L)

ADD2LIBS((const UBYTE *)"datatypes/picture.datatype", 0, struct Library *, PictureBase);

struct writer {
    UBYTE *frame;
    ULONG frame_width, frame_height, y;
};

static LONG put_bitmap(Class *cl, Object *obj, const struct cis_image *image)
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

static LONG load_cis(Class *cl, Object *obj)
{
    struct cis_image image;
    UBYTE *input;
    LONG size, error;

    error = dt_read_file(obj, 3, MAX_CIS_FILE, &input, &size);
    if (error != 0 || input == NULL)
        return error;
    error = dt_error(cis_decode(input, (size_t)size, &image));
    FreeVec(input);
    if (error != 0)
        return error;
    error = put_bitmap(cl, obj, &image);
    cis_free(&image);
    if (error == 0)
        dt_set_name(obj);
    return error;
}

/* Rows and columns beyond the frame are cropped. */
static BOOL write_row(void *state, const UBYTE *rgba, ULONG width)
{
    struct writer *w = state;
    if (w->y < w->frame_height)
        cis_threshold_row(rgba, width < w->frame_width ? width : w->frame_width,
                          w->frame + w->y * w->frame_width);
    w->y++;
    return TRUE;
}

IPTR Cis__OM_NEW(Class *cl, Object *obj, struct opSet *msg)
{
    return dt_new(cl, obj, msg, load_cis);
}

IPTR Cis__DTM_WRITE(Class *cl, Object *obj, struct dtWrite *msg)
{
    struct writer w;
    ULONG width, height, count;
    size_t capacity, size;
    UBYTE *out;
    IPTR success = FALSE;
    unsigned fw, fh;

    if (msg->dtw_Mode != DTWM_RAW)
        return DoSuperMethodA(cl, obj, (Msg)msg);
    if (msg->dtw_FileHandle == BNULL)
        return TRUE;
    if (!dt_picture_size(obj, &width, &height) || !cis_frame(width, height, &fw, &fh))
        return FALSE;
    w.frame_width = fw;
    w.frame_height = fh;
    w.y = 0;
    count = fw * fh;
    capacity = cis_encode_bound(fw, fh);
    /* The frame starts white, which pads a smaller image. */
    w.frame = AllocVec(count + capacity, MEMF_ANY | MEMF_CLEAR);
    if (w.frame == NULL)
        return FALSE;
    out = w.frame + count;
    if (dt_each_row(cl, obj, write_row, &w)) {
        size = cis_encode(w.frame, fw, fh, out, capacity);
        success = size != 0 && dt_write(msg->dtw_FileHandle, out, (LONG)size);
    }
    FreeVec(w.frame);
    return success;
}
