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

#include <string.h>

#include "common/dtfile.h"
#include "common/dtpicture.h"
#include "decode.h"

/* A pixel costs at most two bytes, as a run or literal of one, and each row
   adds its size word and end code. */
#define MAX_HALOCUT_FILE (6L + 32L * 1024L * 1024L + 3L * 65535L)
/* 256 entries in 512-byte blocks fit in 2K; allow for larger palettes. */
#define MAX_PAL_FILE (64L * 1024L)
#define NAME_SIZE 256

ADD2LIBS((const UBYTE *)"datatypes/picture.datatype", 0, struct Library *, PictureBase);

/* Dr. Halo keeps the palette in a .PAL file with the picture's name, in the
   same directory. FALSE when there is none, or it isn't a palette. */
static BOOL find_palette(Object *obj, struct halocut_palette *palette)
{
    STRPTR path = NULL;
    BPTR file = BNULL, dir, old, pal;
    UBYTE name[NAME_SIZE];
    UBYTE *data;
    LONG size;
    char *dot;
    BOOL ok = FALSE;

    GetDTAttrs(obj, DTA_Name, &path, DTA_Handle, &file, TAG_END);
    if (path == NULL || file == BNULL ||
        strlen((const char *)FilePart(path)) >= NAME_SIZE - 4)
        return FALSE;
    strcpy((char *)name, (const char *)FilePart(path));
    dot = strrchr((char *)name, '.');
    strcpy(dot != NULL ? dot : (char *)name + strlen((char *)name), ".pal");
    dir = ParentOfFH(file);
    if (dir == BNULL)
        return FALSE;
    old = CurrentDir(dir);
    pal = Open(name, MODE_OLDFILE);
    if (pal != BNULL) {
        if (Seek(pal, 0, OFFSET_END) != -1 &&
            (size = Seek(pal, 0, OFFSET_BEGINNING)) > 0) {
            if (size > MAX_PAL_FILE)
                size = MAX_PAL_FILE;
            data = AllocVec((ULONG)size, MEMF_ANY);
            if (data != NULL) {
                ok = Read(pal, data, size) == size &&
                     halocut_palette(data, (size_t)size, palette) == CODEC_OK;
                FreeVec(data);
            }
        }
        Close(pal);
    }
    CurrentDir(old);
    UnLock(dir);
    return ok;
}

static LONG load_halocut(Class *cl, Object *obj)
{
    struct halocut_image image;
    struct halocut_palette *palette;
    UBYTE *input;
    LONG size, error;

    error = dt_read_file(obj, 6, MAX_HALOCUT_FILE, &input, &size);
    if (error != 0 || input == NULL)
        return error;
    palette = AllocVec(sizeof *palette, MEMF_ANY);
    if (palette != NULL && !find_palette(obj, palette)) {
        FreeVec(palette);
        palette = NULL;
    }
    error = dt_error(halocut_decode(input, (size_t)size, palette, &image));
    FreeVec(palette);
    FreeVec(input);
    if (error != 0)
        return error;
    error = dt_put_rgba(cl, obj, image.rgba, image.width, image.height);
    halocut_free(&image);
    if (error == 0)
        dt_set_name(obj);
    return error;
}

IPTR Halocut__OM_NEW(Class *cl, Object *obj, struct opSet *msg)
{
    return dt_new(cl, obj, msg, load_halocut);
}
