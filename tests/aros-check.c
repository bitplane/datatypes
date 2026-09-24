#include <datatypes/datatypes.h>
#include <datatypes/pictureclass.h>
#include <dos/dos.h>
#include <proto/datatypes.h>
#include <proto/dos.h>
#include <proto/exec.h>

#include <stdio.h>

int main(int argc, char **argv)
{
    Object *obj;
    struct BitMapHeader *header = NULL;
    struct DataType *datatype;
    struct Library *library;
    BPTR lock;
    if (argc != 2 && argc != 3)
        return 20;
    lock = Lock((CONST_STRPTR)argv[1], ACCESS_READ);
    if (lock == BNULL) {
        printf("FAIL lock %s IoErr=%ld\n", argv[1], (long)IoErr());
        return 10;
    }
    datatype = ObtainDataTypeA(DTST_FILE, (APTR)lock, NULL);
    printf("descriptor %s base %s\n",
           datatype ? (char *)datatype->dtn_Header->dth_Name : "none",
           datatype ? (char *)datatype->dtn_Header->dth_BaseName : "none");
    if (datatype)
        ReleaseDataType(datatype);
    UnLock(lock);
    library = OpenLibrary((CONST_STRPTR)"datatypes/targa.datatype", 0);
    printf("library %s IoErr=%ld\n", library ? "open" : "closed", (long)IoErr());
    if (library)
        CloseLibrary(library);
    obj = NewDTObject(argv[1], TAG_END);
    if (obj == NULL) {
        printf("FAIL %s IoErr=%ld\n", argv[1], (long)IoErr());
        return 10;
    }
    GetDTAttrs(obj, PDTA_BitMapHeader, &header, TAG_END);
    if (header == NULL) {
        printf("FAIL %s missing header\n", argv[1]);
        DisposeDTObject(obj);
        return 10;
    }
    printf("OK %s %u x %u depth %u\n", argv[1],
           header->bmh_Width, header->bmh_Height, header->bmh_Depth);
    if (argc == 3) {
        if (!SaveDTObjectA(obj, NULL, NULL, (STRPTR)argv[2], DTWM_RAW, FALSE, NULL)) {
            printf("FAIL save %s IoErr=%ld\n", argv[2], (long)IoErr());
            DisposeDTObject(obj);
            return 10;
        }
        printf("SAVED %s\n", argv[2]);
    }
    DisposeDTObject(obj);
    return 0;
}
