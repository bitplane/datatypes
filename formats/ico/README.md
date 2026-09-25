# ICO

Reads Windows icons (`.ico`) and cursors (`.cur`). Entries may be BMP at 1, 4, 8, 16, 24 or 32 bits per pixel, with any header from OS/2 to V5, or PNG. The AND mask makes pixels transparent, and a cursor's hotspot becomes the picture's grab point. PNG entries load through `png.datatype` via a temporary file, so they need a writable `T:`.

The largest entry loads, and the deepest of equal sizes. `PDTA_WhichPicture` picks another by its position in the file.

Not supported: RLE, JPEG or PNG compression inside BMP entries, and top-down BMP entries.

Saves an icon with one BMP entry up to 256×256: 24-bit if every pixel is opaque, 32-bit with alpha otherwise. A grab point other than 0,0 saves a cursor instead.

The `ICO` descriptor matches the `00 00 ?? 00` header on files named `.ico` or `.cur`, at priority -10.
