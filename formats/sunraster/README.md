# Sun Raster

Reads 1, 8, 24 and 32-bit Sun Raster files of the old, standard, RLE and RGB types, with or without an RGB colormap. 1-bit images without a colormap load black on white, and 8-bit ones load as grayscale. Images load opaque.

Saves uncompressed 24-bit Sun Raster, with transparency composited over white.

Our `SUNRASTER` descriptor matches the magic number, whatever the file's name.
