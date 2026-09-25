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

/* An image pack with 16Base is about 6 MB; an overview pack of a full disc
   is a few MB. */
#define MAX_PCD_FILE (32L * 1024L * 1024L)

ADD2LIBS((const UBYTE *)"datatypes/picture.datatype", 0, struct Library *, PictureBase);

/* Load picture index, or the default for PCD_DEFAULT; count, if given,
   receives the number of pictures. */
static LONG load_pcd(Class *cl, Object *obj, unsigned long index, ULONG *count)
{
    struct pcd_image image;
    unsigned long pictures = 0;
    UBYTE *input;
    LONG size, error;

    error = dt_read_file(obj, 12, MAX_PCD_FILE, &input, &size);
    if (error != 0 || input == NULL)
        return error;
    pcd_count(input, (size_t)size, &pictures);
    if (count != NULL)
        *count = pictures;
    /* The superclass kept the count pointer from the tags; store the count. */
    SetDTAttrs(obj, NULL, NULL, PDTA_GetNumPictures, pictures, TAG_END);
    error = dt_error(pcd_decode(input, (size_t)size, index, &image));
    FreeVec(input);
    if (error != 0)
        return error;
    error = dt_put_rgba(cl, obj, image.rgba, image.width, image.height);
    pcd_free(&image);
    if (error == 0)
        dt_set_name(obj);
    return error;
}

IPTR Pcd__OM_NEW(Class *cl, Object *obj, struct opSet *msg)
{
    /* dt_new doesn't pass tags on, so the picture index is read here. */
    struct TagItem *which = FindTagItem(PDTA_WhichPicture, msg->ops_AttrList);
    unsigned long index = which != NULL ? (ULONG)which->ti_Data : PCD_DEFAULT;
    ULONG *count = (ULONG *)GetTagData(PDTA_GetNumPictures, 0, msg->ops_AttrList);
    IPTR created = DoSuperMethodA(cl, obj, (Msg)msg);
    LONG error;

    if (created == 0)
        return 0;
    error = load_pcd(cl, (Object *)created, index, count);
    if (error != 0) {
        CoerceMethod(cl, (Object *)created, OM_DISPOSE);
        SetIoErr(error);
        return 0;
    }
    return created;
}
