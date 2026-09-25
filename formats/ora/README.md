# OpenRaster and Krita

Reads OpenRaster (`.ora`) files from GIMP, MyPaint, Krita, Pinta, Paint.NET and others, and Krita documents (`.kra`). Both are zip archives holding a composite, `mergedimage.png`, and that is what loads: every layer, blend mode and opacity merged, with its transparency. Needs `z1.library`.

Layers aren't loaded one by one. Files without a composite are rejected: OpenRaster 0.0.1 files, and Krita documents from before Krita 2.8. So are encrypted archives and compression other than store and deflate.

Saves a one-layer OpenRaster file with a thumbnail, in 8-bit RGB or RGBA.

Our `OpenRaster` and `Krita` descriptors match a zip whose first member is `mimetype`, and need an `.ora` or `.kra` name. They sit above AROS's `ZIP` descriptor, so other zips keep going there.
