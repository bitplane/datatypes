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

/* Header plus 16M pixels of 4 bytes each. */
#define MAX_KISSCEL_FILE (32L + 64L * 1024L * 1024L)
/* Configurations are text; real palette files hold a few groups. Only this
   much of a palette file is read; a group beyond it falls back to the first. */
#define MAX_CNF_FILE (1024L * 1024L)
#define MAX_KCF_FILE (1024L * 1024L)
#define NAME_SIZE 256

ADD2LIBS((const UBYTE *)"datatypes/picture.datatype", 0, struct Library *, PictureBase);

struct writer {
    BPTR file;
    UBYTE *out;
};

/* Read up to max bytes of a file in the current directory. */
static UBYTE *read_small(CONST_STRPTR name, LONG max, LONG *size)
{
    BPTR file = Open(name, MODE_OLDFILE);
    UBYTE *data = NULL;
    LONG length;

    if (file == BNULL)
        return NULL;
    if (Seek(file, 0, OFFSET_END) != -1 &&
        (length = Seek(file, 0, OFFSET_BEGINNING)) > 0) {
        if (length > max)
            length = max;
        data = AllocVec((ULONG)length, MEMF_ANY);
        if (data != NULL && Read(file, data, length) != length) {
            FreeVec(data);
            data = NULL;
        }
        *size = length;
    }
    Close(file);
    return data;
}

static BOOL load_kcf(CONST_STRPTR name, unsigned group,
                     struct kisscel_palette *palette)
{
    LONG size;
    UBYTE *data = read_small(name, MAX_KCF_FILE, &size);
    BOOL ok;

    if (data == NULL)
        return FALSE;
    ok = kisscel_palette(data, (size_t)size, group, palette) == CODEC_OK;
    FreeVec(data);
    return ok;
}

static BOOL has_suffix(CONST_STRPTR name, CONST_STRPTR suffix)
{
    ULONG n = strlen((const char *)name), s = strlen((const char *)suffix);
    return n > s && Stricmp(name + n - s, suffix) == 0;
}

/* Ask a configuration which palette file and group the cel uses. */
static BOOL ask_cnf(CONST_STRPTR cnf, CONST_STRPTR cel, UBYTE *kcf,
                    unsigned *group)
{
    const char *found;
    size_t length;
    LONG size;
    UBYTE *data = read_small(cnf, MAX_CNF_FILE, &size);
    BOOL ok = FALSE;

    if (data == NULL)
        return FALSE;
    if (kisscel_cnf_palette((const char *)data, (size_t)size,
                            (const char *)cel, &found, &length, group) &&
        length < NAME_SIZE) {
        memcpy(kcf, found, length);
        kcf[length] = '\0';
        ok = TRUE;
    }
    FreeVec(data);
    return ok;
}

/* KiSS keeps palettes in separate files. Look in the cel's directory for a
   configuration that names its palette, then a palette with the cel's name,
   then the directory's only palette. FALSE leaves the grey ramp GIMP uses. */
static BOOL find_palette(Object *obj, struct kisscel_palette *palette)
{
    STRPTR path = NULL;
    BPTR file = BNULL, dir, old;
    struct FileInfoBlock *fib;
    UBYTE cel[NAME_SIZE], kcf[NAME_SIZE], only[NAME_SIZE];
    unsigned group = 0, kcfs = 0;
    BOOL from_cnf = FALSE, ok = FALSE;
    char *dot;

    GetDTAttrs(obj, DTA_Name, &path, DTA_Handle, &file, TAG_END);
    if (path == NULL || file == BNULL ||
        strlen((const char *)FilePart(path)) >= NAME_SIZE - 4)
        return FALSE;
    strcpy((char *)cel, (const char *)FilePart(path));
    fib = AllocDosObject(DOS_FIB, NULL);
    if (fib == NULL)
        return FALSE;
    dir = ParentOfFH(file);
    if (dir == BNULL) {
        FreeDosObject(DOS_FIB, fib);
        return FALSE;
    }
    old = CurrentDir(dir);
    if (Examine(dir, fib)) {
        while (ExNext(dir, fib)) {
            if (fib->fib_DirEntryType >= 0)
                continue;
            if (!from_cnf && has_suffix((CONST_STRPTR)fib->fib_FileName,
                                        (CONST_STRPTR)".cnf"))
                from_cnf = ask_cnf((CONST_STRPTR)fib->fib_FileName, cel,
                                   kcf, &group);
            else if (has_suffix((CONST_STRPTR)fib->fib_FileName,
                                (CONST_STRPTR)".kcf") && kcfs++ == 0)
                strcpy((char *)only, (const char *)fib->fib_FileName);
        }
    }
    if (from_cnf)
        ok = load_kcf(kcf, group, palette);
    if (!ok) {
        strcpy((char *)kcf, (const char *)cel);
        dot = strrchr((char *)kcf, '.');
        strcpy(dot != NULL ? dot : (char *)kcf + strlen((char *)kcf), ".kcf");
        ok = load_kcf(kcf, 0, palette);
    }
    if (!ok && kcfs == 1)
        ok = load_kcf(only, 0, palette);
    CurrentDir(old);
    UnLock(dir);
    FreeDosObject(DOS_FIB, fib);
    return ok;
}

static LONG load_kisscel(Class *cl, Object *obj)
{
    struct kisscel_info info;
    struct kisscel_image image;
    struct kisscel_palette *palette = NULL;
    UBYTE *input;
    LONG size, error;

    error = dt_read_file(obj, 4, MAX_KISSCEL_FILE, &input, &size);
    if (error != 0 || input == NULL)
        return error;
    error = dt_error(kisscel_info(input, (size_t)size, &info));
    if (error == 0 && info.bpp != 32) {
        palette = AllocVec(sizeof *palette, MEMF_ANY);
        if (palette != NULL && !find_palette(obj, palette)) {
            FreeVec(palette);
            palette = NULL;
        }
    }
    if (error == 0)
        error = dt_error(kisscel_decode(input, (size_t)size, palette, &image));
    FreeVec(palette);
    FreeVec(input);
    if (error != 0)
        return error;
    error = dt_put_rgba(cl, obj, image.rgba, image.width, image.height);
    kisscel_free(&image);
    if (error == 0)
        dt_set_name(obj);
    return error;
}

static BOOL write_row(void *state, const UBYTE *rgba, ULONG width)
{
    struct writer *w = state;
    kisscel_encode_row(rgba, width, w->out);
    return dt_write(w->file, w->out, (LONG)(width * 4u));
}

IPTR Kisscel__OM_NEW(Class *cl, Object *obj, struct opSet *msg)
{
    return dt_new(cl, obj, msg, load_kisscel);
}

IPTR Kisscel__DTM_WRITE(Class *cl, Object *obj, struct dtWrite *msg)
{
    UBYTE header[KISSCEL_HEADER_SIZE];
    struct writer w;
    ULONG width, height;
    IPTR success;

    if (msg->dtw_Mode != DTWM_RAW)
        return DoSuperMethodA(cl, obj, (Msg)msg);
    /* MultiView probes RAW support without opening an output file. */
    if (msg->dtw_FileHandle == BNULL)
        return TRUE;
    if (!dt_picture_size(obj, &width, &height) ||
        !kisscel_make_header(width, height, header))
        return FALSE;
    w.file = msg->dtw_FileHandle;
    w.out = AllocVec(width * 4u, MEMF_ANY);
    if (w.out == NULL)
        return FALSE;
    success = dt_write(w.file, header, sizeof header) &&
              dt_each_row(cl, obj, write_row, &w);
    FreeVec(w.out);
    return success;
}
