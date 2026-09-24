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

#define MAX_XPM_FILE (64L * 1024L * 1024L)
#define COLOR_BUFFER 4096u

ADD2LIBS((const UBYTE *)"datatypes/picture.datatype", 0, struct Library *, PictureBase);

struct writer {
    BPTR file;
    struct xpm_encoder encoder;
    UBYTE *out;
    size_t capacity;
    BOOL failed;
};

static LONG load_xpm(Class *cl, Object *obj)
{
    struct xpm_image image;
    Point grab;
    UBYTE *input;
    LONG size, error;

    error = dt_read_file(obj, 1, MAX_XPM_FILE, &input, &size);
    if (error != 0 || input == NULL)
        return error;
    error = dt_error(xpm_decode(input, (size_t)size, &image));
    FreeVec(input);
    if (error != 0)
        return error;
    error = dt_put_rgba(cl, obj, image.rgba, image.width, image.height);
    /* The hotspot is what IFF calls the grab point. */
    if (error == 0 && image.hot_x >= 0 && image.hot_x <= 0x7fff && image.hot_y <= 0x7fff) {
        grab.x = (WORD)image.hot_x;
        grab.y = (WORD)image.hot_y;
        SetDTAttrs(obj, NULL, NULL, PDTA_Grab, &grab, TAG_END);
    }
    xpm_free(&image);
    if (error == 0)
        dt_set_name(obj);
    return error;
}

static BOOL add_row(void *state, const UBYTE *rgba, ULONG width)
{
    struct writer *w = state;
    return xpm_encoder_add_row(&w->encoder, rgba, width) == CODEC_OK;
}

static BOOL write_row(void *state, const UBYTE *rgba, ULONG width)
{
    struct writer *w = state;
    size_t size = xpm_encode_row(&w->encoder, rgba, width, (char *)w->out, w->capacity);
    return size != SIZE_MAX && dt_write(w->file, w->out, (LONG)size);
}

/* The colour table, gathered into the output buffer to save on writes. */
static BOOL write_colors(struct writer *w)
{
    size_t index, used = 0, size;

    for (index = 0; index < w->encoder.count; index++) {
        if (w->capacity - used < XPM_COLOR_LINE_MAX) {
            if (!dt_write(w->file, w->out, (LONG)used))
                return FALSE;
            used = 0;
        }
        size = xpm_color_line(&w->encoder, index, (char *)w->out + used, w->capacity - used);
        if (size == 0)
            return FALSE;
        used += size;
    }
    return dt_write(w->file, w->out, (LONG)used);
}

IPTR XPM__OM_NEW(Class *cl, Object *obj, struct opSet *msg)
{
    return dt_new(cl, obj, msg, load_xpm);
}

IPTR XPM__DTM_WRITE(Class *cl, Object *obj, struct dtWrite *msg)
{
    char path[256], name[XPM_MAX_NAME + 1], header[XPM_HEADER_MAX];
    Point *grab = NULL;
    struct writer w;
    ULONG width, height;
    LONG hot_x = -1, hot_y = -1;
    size_t size;
    IPTR success = FALSE;

    if (msg->dtw_Mode != DTWM_RAW)
        return DoSuperMethodA(cl, obj, (Msg)msg);
    if (msg->dtw_FileHandle == BNULL)
        return TRUE;
    if (!dt_picture_size(obj, &width, &height))
        return FALSE;
    /* The array is named after the file being written. */
    if (!NameFromFH(msg->dtw_FileHandle, (STRPTR)path, sizeof path))
        path[0] = '\0';
    xpm_make_name(path, name);
    /* A grab point of 0,0 is the default, so it is not written as a hotspot. */
    GetDTAttrs(obj, PDTA_Grab, &grab, TAG_END);
    if (grab != NULL && (grab->x != 0 || grab->y != 0) &&
        grab->x >= 0 && grab->y >= 0 &&
        (ULONG)grab->x < width && (ULONG)grab->y < height) {
        hot_x = grab->x;
        hot_y = grab->y;
    }

    w.file = msg->dtw_FileHandle;
    xpm_encoder_init(&w.encoder);
    /* First pass: collect the colours, which set the characters per pixel. */
    if (!dt_each_row(cl, obj, add_row, &w)) {
        xpm_encoder_free(&w.encoder);
        return FALSE;
    }
    xpm_encoder_finish(&w.encoder, height);
    w.capacity = xpm_row_capacity(&w.encoder, width);
    if (w.capacity < COLOR_BUFFER)
        w.capacity = COLOR_BUFFER;
    w.out = AllocVec(w.capacity, MEMF_ANY);
    size = xpm_make_header(&w.encoder, name, width, height, hot_x, hot_y,
                           header, sizeof header);
    if (w.out != NULL && size != 0)
        success = dt_write(w.file, header, (LONG)size) &&
                  write_colors(&w) &&
                  dt_each_row(cl, obj, write_row, &w);
    FreeVec(w.out);
    xpm_encoder_free(&w.encoder);
    return success;
}
