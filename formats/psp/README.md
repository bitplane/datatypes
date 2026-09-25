# Paint Shop Pro

Reads Paint Shop Pro 5 to 2023 files: images (`.psp`, `.pspimage`), tubes (`.tub`, `.psptube`), frames, brushes, shapes, masks and selections. Colour is 1, 4 or 8-bit paletted, 8 or 16-bit grey, or 24 or 48-bit RGB, stored raw, RLE or LZ77. Tubes show every cell.

Raster layers are composited as Paint Shop Pro shows them, with transparency, opacity, blend modes, layer masks, mask layers and groups. Vector, adjustment and art media layers come from the composite Paint Shop Pro saves with the file. Files from Paint Shop Pro 6 and 7 store only a JPEG one, so those are rejected when such a layer is visible.

Saves one 24-bit layer, with a transparency mask when the picture has alpha, in Paint Shop Pro 7 format.

`PSP` matches the `Paint Shop Pro Image File` signature in files named `.tub` or with an extension starting `.psp`.
