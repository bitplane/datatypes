# Palm bitmap

Reads Palm OS bitmaps, versions 0 to 3: 1, 2, 4 and 8-bit indexed, and 16-bit RGB565, uncompressed or with scanline, RLE or PackBits compression. Indexed bitmaps without a colour table load as gray at 1, 2 and 4 bits, and in the Palm system palette at 8 bits. The transparent index or colour becomes transparent.

A bitmap family holds several bitmaps. The largest, then deepest, loads by default, and `PDTA_WhichPicture` picks another.

Not supported: little-endian version 3 bitmaps, and direct colour other than 5:6:5.

Saves an uncompressed 8-bit bitmap with a colour table when the picture has at most 256 colours, and 16-bit RGB565 otherwise.

The `PALM` descriptor matches files named `.palm` of at least 16 bytes, at priority -10, since the format has no magic number.
