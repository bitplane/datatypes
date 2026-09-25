# Pixar

Reads 8-bit Pixar Image Computer (picio) pictures: the `.pxr` files Photoshop writes, and the `.pic` files from Pixar's own software and tools like Altamira Composer. RGB and RGBA load as they are, one channel as grey, and red plus alpha as grey with alpha. Pixels can be raw or run-length encoded, in one tile or many.

Saves one uncompressed 8-bit tile, the layout Photoshop writes: RGB when opaque, RGBA otherwise.

Not supported: 12-bit storage and other channel combinations.

Our `PIXAR` descriptor matches the magic `80 E8 00 00` in files named `.pxr`, `.pic`, `.picio` or `.pixar`.
