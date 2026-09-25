#include <aros/symbolsets.h>
#include <datatypes/datatypes.h>
#include <datatypes/datatypesclass.h>
#include <datatypes/pictureclass.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <exec/memory.h>
#include <proto/datatypes.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/utility.h>

#include <string.h>

#include "common/dtfile.h"
#include "common/dtpicture.h"
#include "decode.h"
#include "encode.h"

/* The largest picture is a 108544-byte interlaced Graph Saurus screen;
   anything after the picture is ignored. */
#define MAX_MSX_FILE (256L * 1024L)
#define NAME_SIZE 256

ADD2LIBS((const UBYTE *)"datatypes/picture.datatype", 0, struct Library *, PictureBase);

struct collector {
    UBYTE *rgba;
    ULONG row;
};

/* Graph Saurus keeps the palette in a file with the picture's name and the
   extension ext, in the same directory. The number of bytes read, or 0. */
static LONG find_palette(Object *obj, const char *ext, UBYTE palette[MSX_PALETTE_SIZE])
{
    STRPTR path = NULL;
    BPTR file = BNULL, dir, old, pal;
    UBYTE name[NAME_SIZE];
    LONG size = 0;
    char *dot;

    GetDTAttrs(obj, DTA_Name, &path, DTA_Handle, &file, TAG_END);
    if (path == NULL || file == BNULL ||
        strlen((const char *)FilePart(path)) >= NAME_SIZE - 4)
        return 0;
    strcpy((char *)name, (const char *)FilePart(path));
    dot = strrchr((char *)name, '.');
    if (dot == NULL)
        return 0;
    strcpy(dot + 1, ext);
    dir = ParentOfFH(file);
    if (dir == BNULL)
        return 0;
    old = CurrentDir(dir);
    pal = Open(name, MODE_OLDFILE);
    if (pal != BNULL) {
        size = Read(pal, palette, MSX_PALETTE_SIZE);
        if (size < 0)
            size = 0;
        Close(pal);
    }
    CurrentDir(old);
    UnLock(dir);
    return size;
}

static LONG load_msx(Class *cl, Object *obj)
{
    struct msx_image image;
    UBYTE palette[MSX_PALETTE_SIZE];
    STRPTR name = NULL;
    const char *ext;
    UBYTE *input;
    LONG size, error, palette_size = 0;

    error = dt_read_file(obj, 7, MAX_MSX_FILE, &input, &size);
    if (error != 0 || input == NULL)
        return error;
    /* Only the extension tells the screen modes apart. */
    GetDTAttrs(obj, DTA_Name, &name, TAG_END);
    ext = msx_palette_ext((const char *)name);
    if (ext != NULL)
        palette_size = find_palette(obj, ext, palette);
    error = dt_error(msx_decode(input, (size_t)size, (const char *)name,
                                palette_size > 0 ? palette : NULL,
                                (size_t)palette_size, &image));
    FreeVec(input);
    if (error != 0)
        return error;
    error = dt_put_rgba(cl, obj, image.rgba, image.width, image.height);
    msx_free(&image);
    if (error == 0)
        dt_set_name(obj);
    return error;
}

static BOOL collect_row(void *state, const UBYTE *rgba, ULONG width)
{
    struct collector *c = state;
    memcpy(c->rgba + (size_t)c->row++ * width * 4u, rgba, width * 4u);
    return TRUE;
}

IPTR Msx__OM_NEW(Class *cl, Object *obj, struct opSet *msg)
{
    return dt_new(cl, obj, msg, load_msx);
}

IPTR Msx__DTM_WRITE(Class *cl, Object *obj, struct dtWrite *msg)
{
    struct collector c;
    UBYTE *output;
    ULONG width, height;
    size_t size = 0;
    IPTR success = FALSE;

    if (msg->dtw_Mode != DTWM_RAW)
        return DoSuperMethodA(cl, obj, (Msg)msg);
    /* MultiView probes RAW support without opening an output file. */
    if (msg->dtw_FileHandle == BNULL)
        return TRUE;
    /* Only screens 5, 7 and 8 can be saved; check before allocating. */
    if (!dt_picture_size(obj, &width, &height) || height != 212 ||
        (width != 256 && width != 512)) {
        SetIoErr(DTERROR_INVALID_DATA);
        return FALSE;
    }
    c.rgba = AllocVec(width * height * 4u, MEMF_ANY);
    output = AllocVec(MSX_MAX_OUTPUT, MEMF_ANY);
    c.row = 0;
    if (c.rgba != NULL && output != NULL && dt_each_row(cl, obj, collect_row, &c)) {
        LONG error = dt_error(msx_encode(c.rgba, width, height, output, &size));
        if (error == 0)
            success = dt_write(msg->dtw_FileHandle, output, (LONG)size);
        else
            SetIoErr(error);
    }
    FreeVec(output);
    FreeVec(c.rgba);
    return success;
}
