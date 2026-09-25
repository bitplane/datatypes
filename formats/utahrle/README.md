# Utah RLE

Reads Utah Raster Toolkit RLE images with 8-bit pixels: grey, RGB and pseudocolour, each with or without alpha. A file can hold several images. The first loads by default, and `PDTA_WhichPicture` picks another.

Saves one image: grey when every pixel is grey and opaque, RGB when opaque, and RGB with alpha otherwise.

Not supported: pixels other than 8 bits, channel counts other than 1 or 3, and map counts other than 0 or 3.

Our `UTAHRLE` descriptor matches the `52 CC` magic and 8-bit pixels, whatever the file's name.
