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

#define MAX_SGI_FILE (128L * 1024L * 1024L)

ADD2LIBS((const UBYTE *)"datatypes/picture.datatype", 0, struct Library *, PictureBase);

struct writer {
    BPTR file;
    UBYTE *out;
    size_t capacity;
    uint32_t *lengths; /* four per row, filled by the measuring pass */
    ULONG row;
    ULONG needs;
    ULONG channels;
};

static LONG load_sgi(Class *cl, Object *obj)
{
    struct sgi_image image;
    UBYTE *input;
    LONG size, error;

    error = dt_read_file(obj, 512, MAX_SGI_FILE, &input, &size);
    if (error != 0 || input == NULL)
        return error;
    error = dt_error(sgi_decode(input, (size_t)size, &image));
    FreeVec(input);
    if (error != 0)
        return error;
    error = dt_put_rgba(cl, obj, image.rgba, image.width, image.height);
    sgi_free(&image);
    if (error == 0)
        dt_set_name(obj);
    return error;
}

static BOOL measure_row(void *state, const UBYTE *rgba, ULONG width)
{
    struct writer *w = state;
    uint32_t *lengths = w->lengths + w->row++ * 4u;
    unsigned channel;

    w->needs |= sgi_row_needs(rgba, width);
    for (channel = 0; channel < 4; channel++) {
        size_t size = sgi_encode_rle(rgba, width, channel, w->out, w->capacity);
        if (size == 0)
            return FALSE;
        lengths[channel] = (uint32_t)size;
    }
    return TRUE;
}

static BOOL write_row(void *state, const UBYTE *rgba, ULONG width)
{
    struct writer *w = state;
    unsigned channel;

    for (channel = 0; channel < w->channels; channel++) {
        size_t size = sgi_encode_rle(rgba, width, channel, w->out, w->capacity);
        if (size == 0 || !dt_write(w->file, w->out, (LONG)size))
            return FALSE;
    }
    return TRUE;
}

IPTR SGI__OM_NEW(Class *cl, Object *obj, struct opSet *msg)
{
    return dt_new(cl, obj, msg, load_sgi);
}

IPTR SGI__DTM_WRITE(Class *cl, Object *obj, struct dtWrite *msg)
{
    UBYTE header[SGI_HEADER_SIZE];
    struct writer w = { BNULL, NULL, 0, NULL, 0, 0, 0 };
    UBYTE *tables = NULL;
    ULONG width, height, table_size;
    IPTR success = FALSE;

    if (msg->dtw_Mode != DTWM_RAW)
        return DoSuperMethodA(cl, obj, (Msg)msg);
    /* MultiView probes RAW support without opening an output file. */
    if (msg->dtw_FileHandle == BNULL)
        return TRUE;
    if (!dt_picture_size(obj, &width, &height))
        return FALSE;
    w.file = msg->dtw_FileHandle;
    w.capacity = sgi_rle_capacity(width);
    w.out = AllocVec(w.capacity, MEMF_ANY);
    w.lengths = AllocVec(height * 4u * sizeof *w.lengths, MEMF_ANY);
    if (w.out == NULL || w.lengths == NULL)
        goto done;

    /* The tables precede the rows, so measure every row before writing.
       Pixel content picks the channels: gray, RGB, or RGBA if alpha is used. */
    if (!dt_each_row(cl, obj, measure_row, &w))
        goto done;
    w.channels = sgi_channels(w.needs);
    table_size = height * w.channels * 8u;
    tables = AllocVec(table_size, MEMF_ANY);
    if (tables == NULL ||
        !sgi_make_header(width, height, w.channels, header) ||
        !sgi_make_tables(w.lengths, height, w.channels, tables))
        goto done;
    success = dt_write(w.file, header, sizeof header) &&
              dt_write(w.file, tables, (LONG)table_size) &&
              dt_each_row(cl, obj, write_row, &w);
done:
    FreeVec(tables);
    FreeVec(w.lengths);
    FreeVec(w.out);
    return success;
}
