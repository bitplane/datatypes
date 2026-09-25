# XWD

Reads X11 window dumps (version 7) in every visual class. ZPixmap images can have 1, 4, 8, 16, 24 or 32 bits per pixel, XYBitmap images have depth 1, and XYPixmap images can be any depth up to 32. The header can be big or little-endian. Images load opaque.

Saves 24-bit TrueColor in 32-bit big-endian pixels with no colormap, with transparency composited over white.

Not supported: X10 dumps (version 6), and 2 or 12 bits per pixel.

Our `XWD` descriptor matches files named `.xwd`.
