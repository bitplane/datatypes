#include <datatypes/datatypesclass.h>
#include <datatypes/pictureclass.h>
#include <proto/datatypes.h>

unsigned long datatype_sdk_probe(void)
{
    return (unsigned long)PDTM_WRITEPIXELARRAY;
}
