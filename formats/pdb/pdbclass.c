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

/* 16M pixels at 4 bits is 8M bytes; RLE can take at most 129 bytes for 128. */
#define MAX_PDB_FILE (16L * 1024L * 1024L)

ADD2LIBS((const UBYTE *)"datatypes/picture.datatype", 0, struct Library *, PictureBase);

struct writer {
    BPTR file;
    unsigned depth;
    UBYTE *out;
    size_t capacity;
};

/* Make obj a picture of depth planes with the file's gray levels as pens. */
static LONG put_bitmap(Class *cl, Object *obj, const struct pdb_image *image)
{
    struct BitMapHeader *header = NULL;
    struct ColorRegister *colormap = NULL;
    ULONG *cregs = NULL;
    unsigned i, colors = 1u << image->depth;

    GetDTAttrs(obj, PDTA_BitMapHeader, &header, TAG_END);
    if (header == NULL)
        return ERROR_OBJECT_WRONG_TYPE;
    header->bmh_Width = (UWORD)image->width;
    header->bmh_Height = (UWORD)image->height;
    header->bmh_Depth = (UBYTE)image->depth;
    header->bmh_Masking = mskNone;
    SetDTAttrs(obj, NULL, NULL, PDTA_NumColors, colors, TAG_END);
    GetDTAttrs(obj, PDTA_ColorRegisters, &colormap, PDTA_CRegs, &cregs, TAG_END);
    if (colormap == NULL || cregs == NULL)
        return ERROR_OBJECT_WRONG_TYPE;
    for (i = 0; i < colors; i++) {
        UBYTE shade = (UBYTE)pdb_shade(image->depth, i);
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

static LONG load_pdb(Class *cl, Object *obj)
{
    struct pdb_image image;
    UBYTE *input;
    LONG size, error;

    error = dt_read_file(obj, 68, MAX_PDB_FILE, &input, &size);
    if (error != 0 || input == NULL)
        return error;
    error = dt_error(pdb_decode(input, (size_t)size, &image));
    FreeVec(input);
    if (error != 0)
        return error;
    error = put_bitmap(cl, obj, &image);
    pdb_free(&image);
    if (error == 0)
        dt_set_name(obj);
    return error;
}

static BOOL find_depth(void *state, const UBYTE *rgba, ULONG width)
{
    struct writer *w = state;
    unsigned depth = pdb_row_depth(rgba, width);
    if (depth > w->depth)
        w->depth = depth;
    return TRUE;
}

static BOOL write_row(void *state, const UBYTE *rgba, ULONG width)
{
    struct writer *w = state;
    size_t size = pdb_encode_row(rgba, width, w->depth, w->out, w->capacity);
    return size != 0 && dt_write(w->file, w->out, (LONG)size);
}

IPTR PDB__OM_NEW(Class *cl, Object *obj, struct opSet *msg)
{
    return dt_new(cl, obj, msg, load_pdb);
}

IPTR PDB__DTM_WRITE(Class *cl, Object *obj, struct dtWrite *msg)
{
    UBYTE header[PDB_HEADER_SIZE];
    struct writer w;
    STRPTR name = NULL;
    ULONG width, height;
    IPTR success;

    if (msg->dtw_Mode != DTWM_RAW)
        return DoSuperMethodA(cl, obj, (Msg)msg);
    /* MultiView probes RAW support without opening an output file. */
    if (msg->dtw_FileHandle == BNULL)
        return TRUE;
    if (!dt_picture_size(obj, &width, &height))
        return FALSE;
    /* The smallest depth that holds every gray exactly, else 16 grays. */
    w.depth = 1;
    if (!dt_each_row(cl, obj, find_depth, &w))
        return FALSE;
    GetDTAttrs(obj, DTA_ObjName, &name, TAG_END);
    if (!pdb_make_header((const char *)name, width, height, w.depth, header))
        return FALSE;
    w.file = msg->dtw_FileHandle;
    w.capacity = pdb_row_bytes(width, w.depth);
    w.out = AllocVec(w.capacity, MEMF_ANY);
    if (w.out == NULL)
        return FALSE;
    success = dt_write(w.file, header, sizeof header) &&
              dt_each_row(cl, obj, write_row, &w);
    FreeVec(w.out);
    return success;
}
