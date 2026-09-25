# DCX

Reads DCX files, which hold up to 1024 PCX pages. Pages can be 1, 2 or 4-bit packed, 1-bit planar with 2 to 4 planes, 8-bit indexed or gray, 24-bit RGB, or 32-bit RGBA with alpha. `PDTA_WhichPicture` picks a page, the first by default, and `PDTA_GetNumPictures` returns the page count.

Saves a one-page DCX holding a 24-bit RGB PCX, with transparency composited over white.

Not supported: other depth and plane combinations, such as 16-bit or 2-bit planar pages.

Our `DCX` descriptor matches the `B1 68 DE 3A` magic, whatever the file's name.
