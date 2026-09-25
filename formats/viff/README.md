# Khoros VIFF

Reads Khoros Visualization Image File Format images, as written by Khoros, ImageMagick and other science and imaging tools, in either byte order:

- 8-bit gray, RGB and RGB with alpha
- 8-bit single-band images with a colour map of 1-byte entries
- 1-bit bitmaps, where 1 is black

A file of several concatenated images is multi-image: pick one with `PDTA_WhichPicture`.

Saves 8-bit RGB, or RGB with alpha when the picture has transparency.

Not supported: 16-bit, 32-bit, float, double and complex data; colour maps with wider entries or over several bands; two-band or more than four-band images; compressed or RLE-encoded data; explicit location data; several images under one header.

Our `VIFF` descriptor matches files that start with the VIFF magic `AB 01` and are named `.viff` or `.xv`. XV thumbnails in `.xvpics` have no extension and a different header, so they don't clash.
