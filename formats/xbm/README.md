# XBM

Reads X11 bitmaps (`char` arrays) and older X10 bitmaps (`short` arrays). Images load as one-plane pictures, black on white. A hotspot becomes the picture's grab point.

Saves X11 bitmaps. Dark pixels become set bits, the C names come from the file name, and a non-zero grab point is written as the hotspot.

XBM files have no fixed header, so our `XBM` descriptor matches files named `.xbm`.
