#include <aros/symbolsets.h>
#include <datatypes/datatypes.h>
#include <datatypes/datatypesclass.h>
#include <datatypes/pictureclass.h>
#include <dos/dos.h>
#include <exec/memory.h>
#include <libraries/iffparse.h>
#include <proto/datatypes.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/utility.h>

#include <string.h>

#include "common/dtfile.h"
#include "common/dtpicture.h"
#include "encode.h"
#include "gimp.h"

/* Layered XCF files can be large; the canvas itself is capped by the codec. */
#define MAX_GIMP_FILE (512L * 1024L * 1024L)
#define XCF_BAND 64u

ADD2LIBS((const UBYTE *)"datatypes/picture.datatype", 0, struct Library *, PictureBase);

static LONG load_gimp(Class *cl, Object *obj, long index, ULONG *count_out)
{
    struct gimp_image image;
    UBYTE *input;
    LONG size, error;
    unsigned count;

    error = dt_read_file(obj, 9, MAX_GIMP_FILE, &input, &size);
    if (error != 0 || input == NULL)
        return error;
    error = dt_error(gimp_decode(input, (size_t)size, index, &image, &count));
    FreeVec(input);
    if (count_out != NULL)
        *count_out = count;
    if (error != 0)
        return error;
    error = dt_put_rgba(cl, obj, image.rgba, image.width, image.height);
    gimp_free(&image);
    if (error == 0) {
        SetDTAttrs(obj, NULL, NULL, PDTA_GetNumPictures, (IPTR)count, TAG_END);
        dt_set_name(obj);
    }
    return error;
}

/* dt_new doesn't pass OM_NEW's tags on, so pick out the picture here. */
IPTR GIMP__OM_NEW(Class *cl, Object *obj, struct opSet *msg)
{
    ULONG *count = (ULONG *)GetTagData(PDTA_GetNumPictures, 0, msg->ops_AttrList);
    long index = (long)GetTagData(PDTA_WhichPicture, 0, msg->ops_AttrList);
    IPTR created = DoSuperMethodA(cl, obj, (Msg)msg);
    LONG error;

    if (created == 0)
        return 0;
    error = load_gimp(cl, (Object *)created, index, count);
    if (error != 0) {
        CoerceMethod(cl, (Object *)created, OM_DISPOSE);
        SetIoErr(error);
        return 0;
    }
    return created;
}

/* ---- saving ---- */

struct writer {
    BPTR file;
    UBYTE *out;
    ULONG width, height, rows, y;
    unsigned bytes, flags;
    int pattern;
    UBYTE *band;
    uint32_t *sizes;     /* XCF tile sizes, found on the first pass */
    ULONG tile;          /* the next tile's index */
    BOOL measure;
};

static BOOL scan_row(void *state, const UBYTE *rgba, ULONG width)
{
    struct writer *w = state;
    gimp_scan_row(rgba, width, &w->flags);
    return TRUE;
}

static BOOL write_row(void *state, const UBYTE *rgba, ULONG width)
{
    struct writer *w = state;
    if (w->pattern)
        pat_encode_row(rgba, width, w->bytes, w->out);
    else
        gbr_encode_row(rgba, width, w->bytes, w->out);
    return dt_write(w->file, w->out, (LONG)(width * w->bytes));
}

/* XCF tiles need 64 rows at a time. The first pass measures each tile's
   encoded size for the table, the second writes them. */
static BOOL band_row(void *state, const UBYTE *rgba, ULONG width)
{
    struct writer *w = state;
    size_t length;

    CopyMem((APTR)rgba, w->band + (size_t)w->rows * width * 4u, width * 4u);
    w->rows++;
    w->y++;
    if (w->rows < XCF_BAND && w->y < w->height)
        return TRUE;
    length = xcf_encode_tiles(w->band, width, w->rows, w->bytes,
                              w->measure ? NULL : w->out,
                              w->measure ? w->sizes + w->tile : NULL);
    w->tile += (width + 63u) / 64u;
    w->rows = 0;
    return w->measure || dt_write(w->file, w->out, (LONG)length);
}

/* The format the picture came from: the descriptor that recognised it. */
static ULONG source_format(Object *obj)
{
    struct DataType *type = NULL;
    GetDTAttrs(obj, DTA_DataType, &type, TAG_END);
    if (type == NULL || type->dtn_Header == NULL)
        return 0;
    return type->dtn_Header->dth_ID;
}

/* The object's file name without its extension names brushes and patterns. */
static void object_name(Object *obj, char *name, size_t size)
{
    STRPTR path = NULL;
    const char *base, *dot;
    size_t n;

    name[0] = '\0';
    GetDTAttrs(obj, DTA_Name, &path, TAG_END);
    if (path == NULL)
        return;
    base = (const char *)FilePart(path);
    dot = strrchr(base, '.');
    n = dot != NULL && dot != base ? (size_t)(dot - base) : strlen(base);
    if (n >= size)
        n = size - 1;
    memcpy(name, base, n);
    name[n] = '\0';
}

static BOOL write_xcf(Class *cl, Object *obj, struct writer *w)
{
    size_t size = xcf_header_size(w->width, w->height);
    ULONG tiles = ((w->width + 63u) / 64u) * ((w->height + 63u) / 64u);
    UBYTE *header = AllocVec(size, MEMF_ANY);
    BOOL success = FALSE;

    w->bytes = w->flags & GIMP_SCAN_ALPHA ? 4u : 3u;
    /* The band buffer also holds the trailer. */
    w->band = AllocVec(w->width * XCF_BAND * 4u + XCF_TRAILER(4u), MEMF_ANY);
    w->out = AllocVec(XCF_BAND_BOUND(w->width, w->bytes), MEMF_ANY);
    w->sizes = AllocVec(tiles * sizeof *w->sizes, MEMF_ANY);
    if (header != NULL && w->band != NULL && w->out != NULL && w->sizes != NULL) {
        w->measure = TRUE;
        if (dt_each_row(cl, obj, band_row, w) &&
            xcf_make_header(w->width, w->height, w->bytes, w->sizes, header) == size &&
            dt_write(w->file, header, (LONG)size)) {
            w->measure = FALSE;
            w->y = w->tile = 0;
            if (dt_each_row(cl, obj, band_row, w)) {
                memset(w->band, 0, XCF_TRAILER(w->bytes));
                success = dt_write(w->file, w->band, (LONG)XCF_TRAILER(w->bytes));
            }
        }
    }
    FreeVec(header);
    FreeVec(w->band);
    FreeVec(w->out);
    FreeVec(w->sizes);
    return success;
}

IPTR GIMP__DTM_WRITE(Class *cl, Object *obj, struct dtWrite *msg)
{
    UBYTE header[GIMP_ENCODED_HEADER];
    char name[GIMP_MAX_NAME + 1];
    struct writer w;
    ULONG format;
    size_t length;
    IPTR success;

    if (msg->dtw_Mode != DTWM_RAW)
        return DoSuperMethodA(cl, obj, (Msg)msg);
    /* MultiView probes RAW support without opening an output file. */
    if (msg->dtw_FileHandle == BNULL)
        return TRUE;
    memset(&w, 0, sizeof w);
    if (!dt_picture_size(obj, &w.width, &w.height) ||
        (uint64_t)w.width * w.height > GIMP_MAX_PIXELS)
        return FALSE;
    w.file = msg->dtw_FileHandle;
    /* Pixel content, not the source, decides grey, colour and alpha. */
    if (!dt_each_row(cl, obj, scan_row, &w))
        return FALSE;
    format = source_format(obj);
    object_name(obj, name, sizeof name);
    switch (format) {
    case MAKE_ID('g', 'b', 'r', 'h'):
        w.bytes = gbr_bytes(w.flags);
        length = gbr_make_header(w.width, w.height, w.bytes, name, header);
        break;
    case MAKE_ID('g', 'i', 'h', 'p'):
        w.bytes = gbr_bytes(w.flags);
        length = gih_make_header(w.width, w.height, w.bytes, name, header);
        break;
    case MAKE_ID('g', 'p', 'a', 't'):
        w.bytes = pat_bytes(w.flags);
        w.pattern = 1;
        length = pat_make_header(w.width, w.height, w.bytes, name, header);
        break;
    default:
        /* XCF, and pictures of unknown origin. */
        return write_xcf(cl, obj, &w);
    }
    if (length == 0)
        return FALSE;
    w.out = AllocVec(w.width * w.bytes, MEMF_ANY);
    if (w.out == NULL)
        return FALSE;
    success = dt_write(w.file, header, (LONG)length) &&
              dt_each_row(cl, obj, write_row, &w);
    FreeVec(w.out);
    return success;
}
