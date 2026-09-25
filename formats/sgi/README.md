# SGI

Reads SGI (IRIS) images, verbatim or RLE, with 8 or 16 bits per channel: grayscale, gray with alpha, RGB and RGBA. Also reads the old dithered 3-3-2 RGB images, and screen images as grayscale, since the file doesn't store their palette.

Saves 8-bit RLE: grayscale when every pixel is gray, RGB otherwise, and RGBA when pixels have transparency.

Our `SGI` descriptor matches the `01 DA` magic on files named `.rgb`, `.rgba`, `.bw`, `.sgi`, `.int` or `.inta`.
