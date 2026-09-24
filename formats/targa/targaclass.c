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

#define MAX_TGA_FILE (128L * 1024L * 1024L)

ADD2LIBS((const UBYTE *)"datatypes/picture.datatype", 0, struct Library *, PictureBase);

struct writer {
    BPTR file;
    UBYTE *out;
    size_t capacity;
    ULONG bytes_per_pixel;
};

static LONG load_targa(Class *cl, Object *obj)
{
    struct tga_image image;
    UBYTE *input;
    LONG size, error;

    error = dt_read_file(obj, 18, MAX_TGA_FILE, &input, &size);
    if (error != 0 || input == NULL)
        return error;
    error = dt_error(tga_decode(input, (size_t)size, &image));
    FreeVec(input);
    if (error != 0)
        return error;
    error = dt_put_rgba(cl, obj, image.rgba, image.width, image.height);
    tga_free(&image);
    if (error == 0)
        dt_set_name(obj);
    return error;
}

static BOOL opaque_row(void *state, const UBYTE *rgba, ULONG width)
{
    struct writer *w = state;
    ULONG x;
    for (x = 0; x < width; x++) {
        if (rgba[x * 4u + 3u] != 255) {
            w->bytes_per_pixel = 4;
            return FALSE;
        }
    }
    return TRUE;
}

static BOOL write_row(void *state, const UBYTE *rgba, ULONG width)
{
    struct writer *w = state;
    size_t size = tga_encode_row(rgba, width, w->bytes_per_pixel,
                                 w->out, w->capacity);
    return size != 0 && dt_write(w->file, w->out, (LONG)size);
}

IPTR Targa__OM_NEW(Class *cl, Object *obj, struct opSet *msg)
{
    return dt_new(cl, obj, msg, load_targa);
}

IPTR Targa__DTM_WRITE(Class *cl, Object *obj, struct dtWrite *msg)
{
    static const UBYTE footer[26] = {
        0, 0, 0, 0, 0, 0, 0, 0,
        'T', 'R', 'U', 'E', 'V', 'I', 'S', 'I', 'O', 'N',
        '-', 'X', 'F', 'I', 'L', 'E', '.', 0
    };
    UBYTE header[18] = { 0 };
    struct writer w = { BNULL, NULL, 0, 3 };
    ULONG width, height;
    IPTR success;

    if (msg->dtw_Mode != DTWM_RAW)
        return DoSuperMethodA(cl, obj, (Msg)msg);
    /* MultiView probes RAW support without opening an output file. */
    if (msg->dtw_FileHandle == BNULL)
        return TRUE;
    if (!dt_picture_size(obj, &width, &height))
        return FALSE;
    /* Pixel content, not the source file's depth, decides whether alpha is needed.
       A stopped scan without alpha means a pixel read failed. */
    if (!dt_each_row(cl, obj, opaque_row, &w) && w.bytes_per_pixel == 3)
        return FALSE;
    w.file = msg->dtw_FileHandle;
    w.capacity = width * (w.bytes_per_pixel + 1u);
    w.out = AllocVec(w.capacity, MEMF_ANY);
    if (w.out == NULL)
        return FALSE;

    header[2] = 10;            /* RLE true-color image. */
    header[12] = width & 255u;
    header[13] = width >> 8;
    header[14] = height & 255u;
    header[15] = height >> 8;
    header[16] = w.bytes_per_pixel * 8u;
    header[17] = w.bytes_per_pixel == 4 ? 0x28 : 0x20;
    success = dt_write(w.file, header, sizeof header) &&
              dt_each_row(cl, obj, write_row, &w) &&
              dt_write(w.file, footer, sizeof footer);
    FreeVec(w.out);
    return success;
}
